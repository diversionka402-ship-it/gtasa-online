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

static const DWORD PLAYER_BASE_POINTER = 0xB6F5F0;

// ============================================================
// CFont
// ============================================================

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
typedef void(__cdecl* tCFont_SetJustify)(bool);
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

tCFont_SetJustify CFont_SetJustify =
    reinterpret_cast<tCFont_SetJustify>(0x719600);

tCFont_SetDropShadowPosition CFont_SetDropShadowPosition =
    reinterpret_cast<tCFont_SetDropShadowPosition>(0x719570);

tCFont_SetDropColor CFont_SetDropColor =
    reinterpret_cast<tCFont_SetDropColor>(0x719510);

tCFont_PrintString CFont_PrintString =
    reinterpret_cast<tCFont_PrintString>(0x71A700);

// ============================================================
// HUD
// ============================================================

typedef void(__cdecl* tCHud_Draw)();
tCHud_Draw original_CHud_Draw = nullptr;

// ============================================================
// GTA PED
// ============================================================

typedef void(__cdecl* tRequestModel)(int, int);
typedef void(__cdecl* tLoadAllRequestedModels)(bool);

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

tRequestModel RequestModel =
    reinterpret_cast<tRequestModel>(0x4087E0);

tLoadAllRequestedModels LoadAllRequestedModels =
    reinterpret_cast<tLoadAllRequestedModels>(0x40EA10);

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

// ============================================================
// DATA
// ============================================================

std::mutex dataMutex;

PlayerData latestBotData{};
bool hasBotData = false;

PlayerData localData{};
bool hasLocalPosition = false;

bool gRunning = true;

SOCKET gSocket = INVALID_SOCKET;

// ============================================================
// BOT
// ============================================================

void* gBotPed = nullptr;
bool gBotCreated = false;

float gBotX = 0.0f;
float gBotY = 0.0f;
float gBotZ = 0.0f;
float gBotHeading = 0.0f;

// ============================================================
// LOGGING
// ============================================================

void Log(const char* text)
{
    FILE* f = nullptr;

    fopen_s(&f, "custom_online.log", "a");

    if (!f)
        return;

    std::fprintf(f, "%s\n", text);

    std::fclose(f);
}

void LogBotPosition()
{
    char buffer[256];

    std::snprintf(
        buffer,
        sizeof(buffer),
        "[BOT] Spawn position: X=%.3f Y=%.3f Z=%.3f",
        gBotX,
        gBotY,
        gBotZ
    );

    Log(buffer);
}

// ============================================================
// LOCAL PLAYER POSITION
// ============================================================

bool UpdateLocalPlayerData()
{
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

    float x =
        *reinterpret_cast<float*>(
            matrixPtr + 0x30
        );

    float y =
        *reinterpret_cast<float*>(
            matrixPtr + 0x34
        );

    float z =
        *reinterpret_cast<float*>(
            matrixPtr + 0x38
        );

    if (!std::isfinite(x) ||
        !std::isfinite(y) ||
        !std::isfinite(z))
    {
        return false;
    }

    localData.playerId = 0;

    std::strncpy(
        localData.name,
        "LocalPlayer",
        sizeof(localData.name) - 1
    );

    localData.name[
        sizeof(localData.name) - 1
    ] = '\0';

    localData.x = x;
    localData.y = y;
    localData.z = z;
    localData.rotation = 0.0f;

    hasLocalPosition = true;

    return true;
}

// ============================================================
// CLEAN FONT STATE
// ============================================================

void PrepareFont()
{
    CFont_SetJustify(false);
    CFont_SetOrientation(0);
    CFont_SetProportional(true);

    CFont_SetBackground(false, false);

    // Не позволяем тексту уходить в огромную область HUD.
    CFont_SetWrapx(630.0f);
    CFont_SetCentreSize(0.0f);

    CFont_SetFontStyle(1);

    // Нормальный одинаковый масштаб по X/Y.
    CFont_SetScale(0.4f, 0.8f);

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
}

// ============================================================
// TEXT
// ============================================================

void PrintText(
    float x,
    float y,
    const char* text)
{
    if (!text)
        return;

    PrepareFont();

    CFont_PrintString(
        x,
        y,
        text
    );
}

// ============================================================
// CREATE BOT
// ============================================================

bool CreateBotNearPlayer()
{
    if (gBotCreated)
        return true;

    if (!hasLocalPosition)
        return false;

    // ========================================================
    // ВАЖНО:
    //
    // Фиксируем координаты ОДИН РАЗ.
    // После этого позиция игрока больше на них не влияет.
    // ========================================================

    gBotX = localData.x + 3.0f;
    gBotY = localData.y;
    gBotZ = localData.z;

    gBotHeading = 0.0f;

    LogBotPosition();

    // ========================================================
    // ЗАПРОС МОДЕЛИ
    // ========================================================

    RequestModel(7, 2);
    LoadAllRequestedModels(false);

    // ========================================================
    // СОЗДАНИЕ PED
    // ========================================================

    void* pedMemory =
        PedOperatorNew(0x79C);

    if (!pedMemory)
    {
        Log("[BOT] ERROR: Ped allocation failed.");
        return false;
    }

    CivilianPedConstructor(
        pedMemory,
        4,
        7
    );

    Log("[BOT] Constructor finished.");

    // ========================================================
    // WORLD POSITION
    // ========================================================

    PedSetPosn(
        pedMemory,
        gBotX,
        gBotY,
        gBotZ
    );

    PedSetHeading(
        pedMemory,
        gBotHeading
    );

    Log("[BOT] Position assigned.");

    // ========================================================
    // WORLD ADD
    // ========================================================

    WorldAdd(pedMemory);

    gBotPed = pedMemory;
    gBotCreated = true;

    Log("[BOT] WorldAdd finished.");
    Log("[BOT] Test_Bot created successfully.");

    return true;
}

// ============================================================
// BOT UPDATE
// ============================================================

void UpdateBot()
{
    if (!gBotCreated)
        return;

    if (!gBotPed)
        return;

    // Пока вообще НЕ двигаем ped.
    //
    // Он должен стоять в одной мировой точке.
    //
    // Это специально.
}

// ============================================================
// HUD
// ============================================================

void __cdecl Hooked_CHud_Draw()
{
    if (original_CHud_Draw)
        original_CHud_Draw();

    UpdateLocalPlayerData();

    // ========================================================
    // ЛОКАЛЬНЫЙ СТАТУС
    // ========================================================

    PrintText(
        20.0f,
        20.0f,
        "GTA ONLINE TEST"
    );

    // ========================================================
    // BOT STATUS
    // ========================================================

    bool botReceived = false;

    {
        std::lock_guard<std::mutex> lock(dataMutex);

        botReceived = hasBotData;
    }

    if (!botReceived)
    {
        PrintText(
            20.0f,
            42.0f,
            "BOT: WAITING"
        );
    }
    else if (!gBotCreated)
    {
        PrintText(
            20.0f,
            42.0f,
            "BOT: CREATING"
        );

        if (hasLocalPosition)
        {
            CreateBotNearPlayer();
        }
    }
    else
    {
        PrintText(
            20.0f,
            42.0f,
            "BOT: ONLINE"
        );
    }

    // Больше никаких координат,
    // расстояний и прочего мусора в HUD.

    UpdateBot();
}

// ============================================================
// NETWORK
// ============================================================

DWORD WINAPI NetworkThread(LPVOID)
{
    Log("[NET] Network thread started.");

    WSADATA wsa{};

    if (WSAStartup(
        MAKEWORD(2, 2),
        &wsa
    ) != 0)
    {
        Log("[NET] ERROR: WSAStartup failed.");
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
        Log("[NET] ERROR: socket failed.");

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

    while (gRunning)
    {
        // ====================================================
        // ОТПРАВКА ЛОКАЛЬНОГО ИГРОКА
        // ====================================================

        PlayerData snapshot{};
        bool sendSnapshot = false;

        {
            std::lock_guard<std::mutex> lock(dataMutex);

            if (hasLocalPosition)
            {
                snapshot = localData;
                sendSnapshot = true;
            }
        }

        if (sendSnapshot)
        {
            sendto(
                gSocket,
                reinterpret_cast<const char*>(&snapshot),
                sizeof(PlayerData),
                0,
                reinterpret_cast<const sockaddr*>(&serverAddr),
                sizeof(serverAddr)
            );
        }

        // ====================================================
        // ПРИЁМ ВСЕХ ПАКЕТОВ
        // ====================================================

        while (true)
        {
            PlayerData received{};

            sockaddr_in from{};
            int fromLen =
                sizeof(from);

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
                int error =
                    WSAGetLastError();

                if (error == WSAEWOULDBLOCK)
                    break;

                break;
            }

            if (bytes != sizeof(PlayerData))
                continue;

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

            // Пишем в лог только первый полученный пакет.
            static bool loggedFirstBotPacket = false;

            if (!loggedFirstBotPacket)
            {
                loggedFirstBotPacket = true;

                char buffer[256];

                std::snprintf(
                    buffer,
                    sizeof(buffer),
                    "[NET] First bot packet: ID=%d NAME=%s X=%.3f Y=%.3f Z=%.3f",
                    received.playerId,
                    received.name,
                    received.x,
                    received.y,
                    received.z
                );

                Log(buffer);
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

// ============================================================
// MAIN THREAD
// ============================================================

DWORD WINAPI MainThread(LPVOID)
{
    Sleep(1000);

    Log("Client Loaded!");

    if (MH_Initialize() != MH_OK)
    {
        Log("[HOOK] ERROR: MH_Initialize failed.");
        return 0;
    }

    if (MH_CreateHook(
        reinterpret_cast<LPVOID>(0x58FAE0),
        reinterpret_cast<LPVOID>(&Hooked_CHud_Draw),
        reinterpret_cast<LPVOID*>(&original_CHud_Draw)
    ) != MH_OK)
    {
        Log("[HOOK] ERROR: MH_CreateHook failed.");

        MH_Uninitialize();

        return 0;
    }

    if (MH_EnableHook(
        reinterpret_cast<LPVOID>(0x58FAE0)
    ) != MH_OK)
    {
        Log("[HOOK] ERROR: MH_EnableHook failed.");

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

// ============================================================
// DLLMAIN
// ============================================================

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID reserved)
{
    UNREFERENCED_PARAMETER(
        reserved
    );

    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(
            hModule
        );

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
