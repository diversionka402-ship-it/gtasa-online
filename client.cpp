#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <mutex>

#include <MinHook.h>
#include "shared.h"

#pragma comment(lib, "ws2_32.lib")

// ============================================================
// GTA SA 1.0 US
// ============================================================

// ------------------------------------------------------------
// CFont
// ------------------------------------------------------------

struct CRGBA
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

typedef void(__cdecl* tCFont_SetScale)(float, float);
typedef void(__cdecl* tCFont_SetColor)(CRGBA);
typedef void(__cdecl* tCFont_SetFontStyle)(short);
typedef void(__cdecl* tCFont_SetProportional)(bool);
typedef void(__cdecl* tCFont_SetOrientation)(int);
typedef void(__cdecl* tCFont_SetWrapx)(float);
typedef void(__cdecl* tCFont_SetCentreSize)(float);
typedef void(__cdecl* tCFont_SetBackground)(bool, bool);
typedef void(__cdecl* tCFont_SetDropShadowPosition)(short);
typedef void(__cdecl* tCFont_SetDropColor)(CRGBA);
typedef void(__cdecl* tCFont_PrintString)(float, float, const char*);

tCFont_SetScale CFont_SetScale =
    reinterpret_cast<tCFont_SetScale>(0x719380);

tCFont_SetColor CFont_SetColor =
    reinterpret_cast<tCFont_SetColor>(0x719430);

tCFont_SetFontStyle CFont_SetFontStyle =
    reinterpret_cast<tCFont_SetFontStyle>(0x719490);

tCFont_SetProportional CFont_SetProportional =
    reinterpret_cast<tCFont_SetProportional>(0x7195B0);

tCFont_SetOrientation CFont_SetOrientation =
    reinterpret_cast<tCFont_SetOrientation>(0x719610);

tCFont_SetWrapx CFont_SetWrapx =
    reinterpret_cast<tCFont_SetWrapx>(0x7194D0);

tCFont_SetCentreSize CFont_SetCentreSize =
    reinterpret_cast<tCFont_SetCentreSize>(0x7194E0);

tCFont_SetBackground CFont_SetBackground =
    reinterpret_cast<tCFont_SetBackground>(0x7195C0);

tCFont_SetDropShadowPosition CFont_SetDropShadowPosition =
    reinterpret_cast<tCFont_SetDropShadowPosition>(0x719570);

tCFont_SetDropColor CFont_SetDropColor =
    reinterpret_cast<tCFont_SetDropColor>(0x719510);

tCFont_PrintString CFont_PrintString =
    reinterpret_cast<tCFont_PrintString>(0x71A700);

// ------------------------------------------------------------
// CHud::Draw
// ------------------------------------------------------------

typedef void(__cdecl* tCHud_Draw)();

tCHud_Draw original_CHud_Draw = nullptr;

// ------------------------------------------------------------
// Игровые функции
// ------------------------------------------------------------

typedef void(__cdecl* tRequestModel)(int, int);
typedef void(__cdecl* tLoadAllRequestedModels)(bool);

tRequestModel RequestModel =
    reinterpret_cast<tRequestModel>(0x4087E0);

tLoadAllRequestedModels LoadAllRequestedModels =
    reinterpret_cast<tLoadAllRequestedModels>(0x40EA10);

// ------------------------------------------------------------
// Ped
// ------------------------------------------------------------

typedef void*(__cdecl* tPedOperatorNew)(unsigned int);
typedef void(__thiscall* tCivilianPedConstructor)(
    void*,
    int,
    unsigned int
);

typedef void(__thiscall* tSetPosn)(
    void*,
    float,
    float,
    float
);

typedef void(__thiscall* tSetHeading)(
    void*,
    float
);

typedef void(__cdecl* tWorldAdd)(void*);

tPedOperatorNew PedOperatorNew =
    reinterpret_cast<tPedOperatorNew>(0x5E4720);

tCivilianPedConstructor CivilianPedConstructor =
    reinterpret_cast<tCivilianPedConstructor>(0x5DDB70);

tSetPosn PedSetPosn =
    reinterpret_cast<tSetPosn>(0x420B80);

tSetHeading PedSetHeading =
    reinterpret_cast<tSetHeading>(0x43E0C0);

tWorldAdd WorldAdd =
    reinterpret_cast<tWorldAdd>(0x563220);

// ------------------------------------------------------------
// Размер CCivilianPed
// ------------------------------------------------------------

static const unsigned int BOT_PED_SIZE = 0x79C;

// Обычная модель NPC
static const int BOT_MODEL_ID = 7;

// PED_TYPE_CIVMALE
static const int BOT_PED_TYPE = 4;

// ------------------------------------------------------------
// Данные
// ------------------------------------------------------------

std::mutex dataMutex;

PlayerData latestBotData{};
bool hasBotData = false;

PlayerData localData{};
bool hasLocalPosition = false;

bool gRunning = true;

SOCKET gSocket = INVALID_SOCKET;

void* gBotPed = nullptr;
bool gBotCreated = false;

// ------------------------------------------------------------
// Лог
// ------------------------------------------------------------

void Log(const char* text)
{
    FILE* f = nullptr;

    fopen_s(&f, "custom_online.log", "a");

    if (!f)
        return;

    std::fprintf(f, "%s\n", text);

    std::fclose(f);
}

// ------------------------------------------------------------
// Текст
// ------------------------------------------------------------

void PrintText(float x, float y, const char* text)
{
    if (!text)
        return;

    // Сбрасываем состояние GTA HUD перед нашим текстом.

    CFont_SetOrientation(0);
    CFont_SetWrapx(600.0f);
    CFont_SetCentreSize(600.0f);
    CFont_SetBackground(false, false);

    CFont_SetProportional(true);
    CFont_SetFontStyle(1);

    CFont_SetScale(0.5f, 1.0f);

    CFont_SetColor({
        255,
        255,
        0,
        255
    });

    CFont_SetDropShadowPosition(1);

    CFont_SetDropColor({
        0,
        0,
        0,
        255
    });

    CFont_PrintString(x, y, text);
}

// ------------------------------------------------------------
// Получение позиции игрока
// ------------------------------------------------------------

bool UpdateLocalPlayerData()
{
    const DWORD PLAYER_BASE_POINTER = 0xB6F5F0;

    DWORD playerBase =
        *reinterpret_cast<DWORD*>(PLAYER_BASE_POINTER);

    if (!playerBase)
        return false;

    DWORD matrixPtr =
        *reinterpret_cast<DWORD*>(
            playerBase + 0x14
        );

    if (!matrixPtr)
        return false;

    localData.playerId = 0;

    std::strncpy(
        localData.name,
        "LocalPlayer",
        sizeof(localData.name) - 1
    );

    localData.name[
        sizeof(localData.name) - 1
    ] = '\0';

    localData.x =
        *reinterpret_cast<float*>(matrixPtr + 0x30);

    localData.y =
        *reinterpret_cast<float*>(matrixPtr + 0x34);

    localData.z =
        *reinterpret_cast<float*>(matrixPtr + 0x38);

    localData.rotation = 0.0f;

    if (!std::isfinite(localData.x) ||
        !std::isfinite(localData.y) ||
        !std::isfinite(localData.z))
    {
        return false;
    }

    hasLocalPosition = true;

    return true;
}

// ------------------------------------------------------------
// Создание бота
// ------------------------------------------------------------

bool CreateBot(const PlayerData& data)
{
    if (gBotCreated)
        return true;

    RequestModel(BOT_MODEL_ID, 2);

    LoadAllRequestedModels(false);

    void* pedMemory =
        PedOperatorNew(BOT_PED_SIZE);

    if (!pedMemory)
    {
        Log("[BOT] Allocation failed.");
        return false;
    }

    CivilianPedConstructor(
        pedMemory,
        BOT_PED_TYPE,
        BOT_MODEL_ID
    );

    PedSetPosn(
        pedMemory,
        data.x,
        data.y,
        data.z
    );

    PedSetHeading(
        pedMemory,
        data.rotation
    );

    WorldAdd(pedMemory);

    gBotPed = pedMemory;
    gBotCreated = true;

    Log("[BOT] Test_Bot created.");

    return true;
}

// ------------------------------------------------------------
// Обновление бота
// ------------------------------------------------------------

void UpdateBot()
{
    PlayerData bot;

    {
        std::lock_guard<std::mutex> lock(dataMutex);

        if (!hasBotData)
            return;

        bot = latestBotData;
    }

    if (!gBotCreated)
    {
        if (!CreateBot(bot))
            return;
    }

    // Плавное движение.

    static float smoothX = bot.x;
    static float smoothY = bot.y;
    static float smoothZ = bot.z;
    static float smoothRot = bot.rotation;

    const float factor = 0.15f;

    smoothX +=
        (bot.x - smoothX) * factor;

    smoothY +=
        (bot.y - smoothY) * factor;

    smoothZ +=
        (bot.z - smoothZ) * factor;

    smoothRot +=
        (bot.rotation - smoothRot) * factor;

    PedSetPosn(
        gBotPed,
        smoothX,
        smoothY,
        smoothZ
    );

    PedSetHeading(
        gBotPed,
        smoothRot
    );
}

// ------------------------------------------------------------
// HUD
// ------------------------------------------------------------

void __cdecl Hooked_CHud_Draw()
{
    if (original_CHud_Draw)
        original_CHud_Draw();

    UpdateLocalPlayerData();

    char localBuffer[256];

    if (hasLocalPosition)
    {
        std::snprintf(
            localBuffer,
            sizeof(localBuffer),
            "LOCAL  X: %.1f  Y: %.1f  Z: %.1f",
            localData.x,
            localData.y,
            localData.z
        );
    }
    else
    {
        std::snprintf(
            localBuffer,
            sizeof(localBuffer),
            "LOCAL: waiting..."
        );
    }

    PrintText(
        25.0f,
        25.0f,
        localBuffer
    );

    bool botOnline = false;
    PlayerData bot{};

    {
        std::lock_guard<std::mutex> lock(dataMutex);

        if (hasBotData)
        {
            botOnline = true;
            bot = latestBotData;
        }
    }

    if (botOnline)
    {
        char botBuffer[256];

        std::snprintf(
            botBuffer,
            sizeof(botBuffer),
            "BOT [%s]  X: %.1f  Y: %.1f  Z: %.1f",
            bot.name,
            bot.x,
            bot.y,
            bot.z
        );

        PrintText(
            25.0f,
            50.0f,
            botBuffer
        );

        UpdateBot();
    }
    else
    {
        PrintText(
            25.0f,
            50.0f,
            "BOT: waiting for server..."
        );
    }
}

// ------------------------------------------------------------
// NETWORK THREAD
// ------------------------------------------------------------

DWORD WINAPI NetworkThread(LPVOID)
{
    Log("[NET] Network thread started.");

    WSADATA wsa{};

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        Log("[NET] WSAStartup failed.");
        return 0;
    }

    gSocket =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP
        );

    if (gSocket == INVALID_SOCKET)
    {
        Log("[NET] socket() failed.");

        WSACleanup();

        return 0;
    }

    u_long nonBlocking = 1;

    ioctlsocket(
        gSocket,
        FIONBIO,
        &nonBlocking
    );

    sockaddr_in serverAddr{};

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    serverAddr.sin_addr.s_addr =
        inet_addr("127.0.0.1");

    Sleep(1000);

    while (gRunning)
    {
        // ----------------------------------------------------
        // Отправляем свою позицию
        // ----------------------------------------------------

        if (UpdateLocalPlayerData())
        {
            PlayerData snapshot;

            {
                std::lock_guard<std::mutex> lock(dataMutex);

                snapshot = localData;
            }

            sendto(
                gSocket,
                reinterpret_cast<const char*>(&snapshot),
                sizeof(PlayerData),
                0,
                reinterpret_cast<const sockaddr*>(&serverAddr),
                sizeof(serverAddr)
            );
        }

        // ----------------------------------------------------
        // Читаем ВСЕ доступные UDP-пакеты
        // ----------------------------------------------------

        while (true)
        {
            PlayerData received{};

            sockaddr_in from{};
            int fromLen = sizeof(from);

            int bytes =
                recvfrom(
                    gSocket,
                    reinterpret_cast<char*>(&received),
                    sizeof(received),
                    0,
                    reinterpret_cast<sockaddr*>(&from),
                    &fromLen
                );

            if (bytes == SOCKET_ERROR)
            {
                int error = WSAGetLastError();

                if (error == WSAEWOULDBLOCK)
                    break;

                break;
            }

            if (bytes != sizeof(PlayerData))
                continue;

            // Нам сейчас нужен только Test_Bot.

            if (received.playerId != 999)
                continue;

            if (!std::isfinite(received.x) ||
                !std::isfinite(received.y) ||
                !std::isfinite(received.z))
            {
                continue;
            }

            received.name[
                sizeof(received.name) - 1
            ] = '\0';

            {
                std::lock_guard<std::mutex> lock(dataMutex);

                latestBotData = received;
                hasBotData = true;
            }
        }

        Sleep(20);
    }

    if (gSocket != INVALID_SOCKET)
    {
        closesocket(gSocket);
        gSocket = INVALID_SOCKET;
    }

    WSACleanup();

    return 0;
}

// ------------------------------------------------------------
// MAIN THREAD
// ------------------------------------------------------------

DWORD WINAPI MainThread(LPVOID)
{
    Sleep(1000);

    Log("Client Loaded!");

    if (MH_Initialize() != MH_OK)
    {
        Log("[HOOK] MH_Initialize failed.");
        return 0;
    }

    if (MH_CreateHook(
        reinterpret_cast<LPVOID>(0x58FAE0),
        reinterpret_cast<LPVOID>(&Hooked_CHud_Draw),
        reinterpret_cast<LPVOID*>(&original_CHud_Draw)
    ) != MH_OK)
    {
        Log("[HOOK] MH_CreateHook failed.");

        MH_Uninitialize();

        return 0;
    }

    if (MH_EnableHook(
        reinterpret_cast<LPVOID>(0x58FAE0)
    ) != MH_OK)
    {
        Log("[HOOK] MH_EnableHook failed.");

        MH_RemoveHook(
            reinterpret_cast<LPVOID>(0x58FAE0)
        );

        MH_Uninitialize();

        return 0;
    }

    Log("Hooks installed successfully.");

    HANDLE networkThread =
        CreateThread(
            nullptr,
            0,
            NetworkThread,
            nullptr,
            0,
            nullptr
        );

    if (networkThread)
        CloseHandle(networkThread);

    return 0;
}

// ------------------------------------------------------------
// DLL MAIN
// ------------------------------------------------------------

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID reserved)
{
    UNREFERENCED_PARAMETER(hModule);
    UNREFERENCED_PARAMETER(reserved);

    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        HANDLE thread =
            CreateThread(
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
    else if (reason == DLL_PROCESS_DETACH)
    {
        gRunning = false;
    }

    return TRUE;
}
