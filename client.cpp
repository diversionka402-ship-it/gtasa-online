#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <map>
#include <string>
#include <mutex>
#include <stdio.h>
#include <MinHook.h>
#include "shared.h"

#pragma comment(lib, "ws2_32.lib")

// --- ЛОГИРОВАНИЕ В ФАЙЛ (чтобы видеть, где именно крашится) ---
void Log(const char* msg) {
    FILE* f = fopen("client_log.txt", "a");
    if (f) {
        fprintf(f, "%s\n", msg);
        fclose(f);
    }
}

// --- АДРЕСА ФУНКЦИЙ ИГРЫ (GTA SA 1.0 US) ---
const DWORD PLAYER_BASE_POINTER = 0xB6F5F0; 

struct CRGBA { unsigned char r, g, b, a; };

typedef void(__cdecl* tCFont_SetScale)(float x, float y);
tCFont_SetScale CFont_SetScale = (tCFont_SetScale)0x719380;

typedef void(__cdecl* tCFont_SetColor)(CRGBA* color);
tCFont_SetColor CFont_SetColor = (tCFont_SetColor)0x715FA0;

typedef void(__cdecl* tCFont_SetFontStyle)(unsigned char style);
tCFont_SetFontStyle CFont_SetFontStyle = (tCFont_SetFontStyle)0x719490;

typedef void(__cdecl* tCFont_SetProportional)(bool prop);
tCFont_SetProportional CFont_SetProportional = (tCFont_SetProportional)0x719450;

typedef void(__cdecl* tAsciiToGxtChar)(const char* src, unsigned short* dst);
tAsciiToGxtChar AsciiToGxtChar = (tAsciiToGxtChar)0x718600;

typedef void(__cdecl* tCFont_PrintString)(float x, float y, unsigned short* text);
tCFont_PrintString CFont_PrintString = (tCFont_PrintString)0x71A700;

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ---
PlayerData myData = {0, 0.0f, 0.0f, 0.0f};
std::map<int, PlayerData> remotePlayers;
std::mutex playersMutex;

typedef void(__cdecl* tCHud_Draw)();
tCHud_Draw original_CHud_Draw = nullptr;

void PrintTextOnScreen(float x, float y, const char* text) {
    Log("  PrintTextOnScreen: start");

    unsigned short gxtString[256];

    Log("  PrintTextOnScreen: before AsciiToGxtChar");
    AsciiToGxtChar(text, gxtString);
    Log("  PrintTextOnScreen: after AsciiToGxtChar");

    Log("  PrintTextOnScreen: before SetScale");
    CFont_SetScale(0.4f, 1.2f);
    Log("  PrintTextOnScreen: after SetScale");

    CRGBA color = {255, 255, 0, 255};
    Log("  PrintTextOnScreen: before SetColor");
    CFont_SetColor(&color);
    Log("  PrintTextOnScreen: after SetColor");

    Log("  PrintTextOnScreen: before SetFontStyle");
    CFont_SetFontStyle(1);
    Log("  PrintTextOnScreen: after SetFontStyle");

    Log("  PrintTextOnScreen: before SetProportional");
    CFont_SetProportional(true);
    Log("  PrintTextOnScreen: after SetProportional");

    Log("  PrintTextOnScreen: before PrintString");
    CFont_PrintString(x, y, gxtString);
    Log("  PrintTextOnScreen: after PrintString");
}

// --- ОТДЕЛЬНАЯ ФУНКЦИЯ С РИСОВАНИЕМ (нужна отдельно от __try, т.к. lock_guard
// имеет деструктор, а C++ объекты с деструкторами нельзя мешать с __try/__except
// в одной и той же функции - иначе ошибка компиляции C2712) ---
void DrawAllTexts() {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "My Pos: X: %.1f Y: %.1f Z: %.1f", myData.x, myData.y, myData.z);

    Log("DrawAllTexts: before PrintTextOnScreen (my pos)");
    PrintTextOnScreen(20.0f, 200.0f, buffer);
    Log("DrawAllTexts: after PrintTextOnScreen (my pos)");

    std::lock_guard<std::mutex> lock(playersMutex);
    float startY = 230.0f;

    for (auto const& playerPair : remotePlayers) {
        char pBuf[256];
        snprintf(pBuf, sizeof(pBuf), "Player %d: X: %.1f Y: %.1f Z: %.1f", 
                 playerPair.second.playerId, 
                 playerPair.second.x, 
                 playerPair.second.y, 
                 playerPair.second.z);
        PrintTextOnScreen(20.0f, startY, pBuf);
        startY += 25.0f;
    }
}

// --- НАШ ПЕРЕХВАТЧИК (с защитой от краша через SEH) ---
void __cdecl Hooked_CHud_Draw() {
    Log("Hooked_CHud_Draw: entry");

    if (original_CHud_Draw) {
        Log("Hooked_CHud_Draw: before original_CHud_Draw()");
        original_CHud_Draw();
        Log("Hooked_CHud_Draw: after original_CHud_Draw()");
    }

    // Оборачиваем ВЫЗОВ функции в SEH, чтобы даже при ошибке
    // чтения памяти игра не вылетала целиком, а просто пропускала кадр
    __try {
        DrawAllTexts();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log(">>> CRASH CAUGHT inside Hooked_CHud_Draw custom drawing code! <<<");
    }

    Log("Hooked_CHud_Draw: exit");
}

// --- СЕТЕВОЙ ПОТОК ---
void NetworkThread() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    u_long mode = 1;
    ioctlsocket(clientSocket, FIONBIO, &mode);

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    while (true) {
        DWORD* playerBase = (DWORD*)PLAYER_BASE_POINTER;
        if (*playerBase != 0) {
            DWORD matrixPtr = *(DWORD*)(*playerBase + 0x14);
            if (matrixPtr != 0) {
                myData.x = *(float*)(matrixPtr + 0x30);
                myData.y = *(float*)(matrixPtr + 0x34);
                myData.z = *(float*)(matrixPtr + 0x38);
                sendto(clientSocket, (char*)&myData, sizeof(PlayerData), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
            }
        }

        char buffer[512];
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);
        int bytesIn = recvfrom(clientSocket, buffer, sizeof(buffer), 0, (sockaddr*)&fromAddr, &fromLen);
        
        if (bytesIn == sizeof(PlayerData)) {
            PlayerData* pData = (PlayerData*)buffer;
            std::lock_guard<std::mutex> lock(playersMutex);
            remotePlayers[pData->playerId] = *pData;
        }

        Sleep(30);
    }
}

// --- ИНИЦИАЛИЗАЦИЯ ---
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // Очищаем старый лог при каждом запуске
        FILE* f = fopen("client_log.txt", "w");
        if (f) fclose(f);
        Log("DllMain: DLL_PROCESS_ATTACH");

        if (MH_Initialize() == MH_OK) {
            Log("DllMain: MH_Initialize OK");
            MH_STATUS hookStatus = MH_CreateHook((LPVOID)0x58FAE0, &Hooked_CHud_Draw, (LPVOID*)&original_CHud_Draw);
            if (hookStatus == MH_OK) {
                Log("DllMain: MH_CreateHook OK");
            } else {
                char buf[128];
                snprintf(buf, sizeof(buf), "DllMain: MH_CreateHook FAILED, status=%d", hookStatus);
                Log(buf);
            }
            MH_EnableHook(MH_ALL_HOOKS);
            Log("DllMain: MH_EnableHook called");
        } else {
            Log("DllMain: MH_Initialize FAILED");
        }

        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)NetworkThread, NULL, 0, NULL);
        Log("DllMain: NetworkThread created");
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}
