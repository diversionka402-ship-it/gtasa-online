#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <map>
#include <mutex>

#include <MinHook.h>
#include "shared.h"

#pragma comment(lib, "ws2_32.lib")

// ============================================================
// GTA SA 1.0 US
// ============================================================

const DWORD PLAYER_BASE_POINTER = 0xB6F5F0;

struct CRGBA {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

// ---------------- CFont ----------------

typedef void(__cdecl* tCFont_SetScale)(float w, float h);
typedef void(__cdecl* tCFont_SetColor)(CRGBA color);
typedef void(__cdecl* tCFont_SetFontStyle)(short style);
typedef void(__cdecl* tCFont_SetProportional)(bool on);
typedef void(__cdecl* tCFont_PrintString)(float x, float y, const char* text);

tCFont_SetScale CFont_SetScale =
    reinterpret_cast<tCFont_SetScale>(0x719380);

tCFont_SetColor CFont_SetColor =
    reinterpret_cast<tCFont_SetColor>(0x719430);

tCFont_SetFontStyle CFont_SetFontStyle =
    reinterpret_cast<tCFont_SetFontStyle>(0x719490);

tCFont_SetProportional CFont_SetProportional =
    reinterpret_cast<tCFont_SetProportional>(0x7195B0);

tCFont_PrintString CFont_PrintString =
    reinterpret_cast<tCFont_PrintString>(0x71A700);

// ---------------- CHud::Draw ----------------

typedef void(__cdecl* tCHud_Draw)();
tCHud_Draw original_CHud_Draw = nullptr;

// ---------------- Данные ----------------

PlayerData myData = {};
std::map<int, PlayerData> remotePlayers;
std::mutex playersMutex;

volatile bool g_running = true;
SOCKET g_clientSocket = INVALID_SOCKET;

// ============================================================
// ТЕКСТ НА ЭКРАНЕ
// ============================================================

void PrintTextOnScreen(float x, float y, const char* text)
{
    if (!text)
        return;

    CFont_SetFontStyle(1);
    CFont_SetScale(0.5f, 1.0f);
    CFont_SetColor({255, 255, 0, 255});
    CFont_SetProportional(true);

    CFont_PrintString(x, y, text);
}

// ============================================================
// HUD HOOK
// ============================================================

void __cdecl Hooked_CHud_Draw()
{
    if (original_CHud_Draw)
        original_CHud_Draw();

    char buffer[256];

    std::snprintf(
        buffer,
        sizeof(buffer),
        "My Pos: X: %.1f Y: %.1f Z: %.1f",
        myData.x,
        myData.y,
        myData.z
    );

    PrintTextOnScreen(20.0f, 200.0f, buffer);

    std::lock_guard<std::mutex> lock(playersMutex);

    float startY = 230.0f;

    for (const auto& entry : remotePlayers)
    {
        const PlayerData& player = entry.second;

        char playerBuffer[256];

        std::snprintf(
            playerBuffer,
            sizeof(playerBuffer),
            "Player %d [%s]: X: %.1f Y: %.1f Z: %.1f",
            player.playerId,
            player.name,
            player.x,
            player.y,
            player.z
        );

        PrintTextOnScreen(20.0f, startY, playerBuffer);

        startY += 25.0f;

        // Чтобы случайно не залить весь экран
        if (startY > 700.0f)
            break;
    }
}

// ============================================================
// ЧТЕНИЕ ПОЗИЦИИ ИГРОКА
// ============================================================

bool UpdateMyPosition()
{
    DWORD playerPtrAddress = PLAYER_BASE_POINTER;

    if (!IsBadReadPtr(reinterpret_cast<void*>(playerPtrAddress), sizeof(DWORD)))
    {
        DWORD playerBase = *reinterpret_cast<DWORD*>(playerPtrAddress);

        if (playerBase != 0 &&
            !IsBadReadPtr(reinterpret_cast<void*>(playerBase + 0x14), sizeof(DWORD)))
        {
            DWORD matrixPtr =
                *reinterpret_cast<DWORD*>(playerBase + 0x14);

            if (matrixPtr != 0 &&
                !IsBadReadPtr(
                    reinterpret_cast<void*>(matrixPtr + 0x30),
                    sizeof(float) * 3))
            {
                myData.x = *reinterpret_cast<float*>(matrixPtr + 0x30);
                myData.y = *reinterpret_cast<float*>(matrixPtr + 0x34);
                myData.z = *reinterpret_cast<float*>(matrixPtr + 0x38);

                return true;
            }
        }
    }

    return false;
}

// ============================================================
// СЕТЬ
// ============================================================

void NetworkThread()
{
    WSADATA wsa{};

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return;

    g_clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (g_clientSocket == INVALID_SOCKET)
    {
        WSACleanup();
        return;
    }

    u_long nonBlocking = 1;

    if (ioctlsocket(g_clientSocket, FIONBIO, &nonBlocking) != 0)
    {
        closesocket(g_clientSocket);
        g_clientSocket = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // --------------------------------------------------------
    // Ждём нормального запуска игры
    // --------------------------------------------------------

    Sleep(2000);

    while (g_running)
    {
        // ====================================================
        // ОТПРАВКА СЕБЯ
        // ====================================================

        if (UpdateMyPosition())
        {
            myData.playerId = 0;

            std::snprintf(
                myData.name,
                sizeof(myData.name),
                "Player"
            );

            myData.rotation = 0.0f;

            sendto(
                g_clientSocket,
                reinterpret_cast<const char*>(&myData),
                sizeof(PlayerData),
                0,
                reinterpret_cast<const sockaddr*>(&serverAddr),
                sizeof(serverAddr)
            );
        }

        // ====================================================
        // ПРИЁМ ВСЕХ ДОСТУПНЫХ ПАКЕТОВ
        // ====================================================

        while (true)
        {
            PlayerData received{};
            sockaddr_in fromAddr{};
            int fromLen = sizeof(fromAddr);

            int bytesIn = recvfrom(
                g_clientSocket,
                reinterpret_cast<char*>(&received),
                sizeof(received),
                0,
                reinterpret_cast<sockaddr*>(&fromAddr),
                &fromLen
            );

            if (bytesIn == SOCKET_ERROR)
            {
                int error = WSAGetLastError();

                if (error == WSAEWOULDBLOCK)
                    break;

                break;
            }

            if (bytesIn != sizeof(PlayerData))
                continue;

            // ----------------------------------------------
            // Защита от мусорного ID
            // ----------------------------------------------

            if (received.playerId <= 0)
                continue;

            // ----------------------------------------------
            // Сохраняем игрока / бота
            // ----------------------------------------------

            {
                std::lock_guard<std::mutex> lock(playersMutex);

                remotePlayers[received.playerId] = received;
            }
        }

        Sleep(30);
    }

    closesocket(g_clientSocket);
    g_clientSocket = INVALID_SOCKET;

    WSACleanup();
}

// ============================================================
// DLL THREAD
// ============================================================

DWORD WINAPI MainThread(LPVOID)
{
    // ---------------- MinHook ----------------

    if (MH_Initialize() != MH_OK)
        return 0;

    if (MH_CreateHook(
        reinterpret_cast<LPVOID>(0x58FAE0),
        reinterpret_cast<LPVOID>(&Hooked_CHud_Draw),
        reinterpret_cast<LPVOID*>(&original_CHud_Draw)
    ) != MH_OK)
    {
        MH_Uninitialize();
        return 0;
    }

    if (MH_EnableHook(reinterpret_cast<LPVOID>(0x58FAE0)) != MH_OK)
    {
        MH_RemoveHook(reinterpret_cast<LPVOID>(0x58FAE0));
        MH_Uninitialize();
        return 0;
    }

    // ---------------- Сеть ----------------

    NetworkThread();

    // ---------------- Завершение ----------------

    MH_DisableHook(reinterpret_cast<LPVOID>(0x58FAE0));
    MH_RemoveHook(reinterpret_cast<LPVOID>(0x58FAE0));
    MH_Uninitialize();

    return 0;
}

// ============================================================
// DLLMAIN
// ============================================================

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD ul_reason_for_call,
    LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(hModule);
    UNREFERENCED_PARAMETER(lpReserved);

    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        HANDLE thread = CreateThread(
            nullptr,
            0,
            MainThread,
            nullptr,
            0,
            nullptr
        );

        if (thread)
            CloseHandle(thread);
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        g_running = false;
    }

    return TRUE;
}
