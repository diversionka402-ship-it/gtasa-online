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

void Log(const char* msg) {
    FILE* f = fopen("client_log.txt", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

const DWORD PLAYER_BASE_POINTER = 0xB6F5F0; 

struct CRGBA { unsigned char r, g, b, a; };
struct CVector { float x, y, z; };

// --- ФУНКЦИИ ИГРЫ ---
typedef void(__cdecl* tCFont_SetScale)(float x, float y);
tCFont_SetScale CFont_SetScale = (tCFont_SetScale)0x719380;

typedef void(__cdecl* tCFont_SetColor)(CRGBA color);
tCFont_SetColor CFont_SetColor = (tCFont_SetColor)0x719430;

typedef void(__cdecl* tCFont_SetFontStyle)(short style);
tCFont_SetFontStyle CFont_SetFontStyle = (tCFont_SetFontStyle)0x719490;

typedef void(__cdecl* tCFont_SetProportional)(bool prop);
tCFont_SetProportional CFont_SetProportional = (tCFont_SetProportional)0x7195B0;

typedef void(__cdecl* tCFont_SetDropShadowPosition)(short pos);
tCFont_SetDropShadowPosition CFont_SetDropShadowPosition = (tCFont_SetDropShadowPosition)0x719570;

typedef void(__cdecl* tCFont_SetDropColor)(CRGBA color);
tCFont_SetDropColor CFont_SetDropColor = (tCFont_SetDropColor)0x719510;

typedef void(__cdecl* tAsciiToGxtChar)(const char* src, unsigned short* dst);
tAsciiToGxtChar AsciiToGxtChar = (tAsciiToGxtChar)0x718600;

typedef void(__cdecl* tCFont_PrintString)(float x, float y, unsigned short* text);
tCFont_PrintString CFont_PrintString = (tCFont_PrintString)0x71A700;

typedef bool(__cdecl* tCSprite_CalcScreenCoors)(const CVector* vecPos, CVector* vecOut, float* w, float* h, bool checkMaxVisible, bool checkMinVisible);
tCSprite_CalcScreenCoors CSprite_CalcScreenCoors = (tCSprite_CalcScreenCoors)0x70CE30;

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ---
PlayerData myData = {0, "", 0.0f, 0.0f, 0.0f, 0.0f};

struct RemotePlayer {
    PlayerData data;
    DWORD lastUpdateTick;
};

std::map<int, RemotePlayer> remotePlayers;
std::mutex playersMutex;

typedef void(__cdecl* tCHud_Draw)();
tCHud_Draw original_CHud_Draw = nullptr;

void PrintTextOnScreen(float x, float y, const char* text, CRGBA color, float scaleX = 0.4f, float scaleY = 0.8f) {
    unsigned short gxtString[256];
    AsciiToGxtChar(text, gxtString);
    
    CFont_SetScale(scaleX, scaleY); 
    CFont_SetColor(color);
    CFont_SetFontStyle(1);
    CFont_SetProportional(true);
    
    CFont_SetDropShadowPosition(1);
    CRGBA shadow = {0, 0, 0, 255};
    CFont_SetDropColor(shadow);
    
    CFont_PrintString(x, y, gxtString);
}

// --- ОТРИСОВКА ИНТЕРФЕЙСА И ИГРОКОВ ---
void DrawAllTexts() {
    // Читаем высоту экрана из памяти игры (GTA SA 1.0 US)
    int screenHeight = *(int*)0xC17048; 
    
    // Ставим начальную точку по Y так, чтобы текст был над радаром (радар внизу слева)
    float startY = screenHeight - 300.0f; 
    
    PrintTextOnScreen(20.0f, startY, "--- ONLINE PLAYERS ---", {255, 200, 0, 255});
    startY += 20.0f;

    // ВОЗВРАЩАЕМ ТВОИ КООРДИНАТЫ
    char myBuf[256];
    snprintf(myBuf, sizeof(myBuf), "%s | X:%.1f Y:%.1f Z:%.1f", myData.name, myData.x, myData.y, myData.z);
    PrintTextOnScreen(20.0f, startY, myBuf, {0, 255, 0, 255});
    startY += 20.0f;

    std::lock_guard<std::mutex> lock(playersMutex);
    DWORD currentTick = GetTickCount();

    for (auto it = remotePlayers.begin(); it != remotePlayers.end(); ) {
        if (currentTick - it->second.lastUpdateTick > 3000) {
            it = remotePlayers.erase(it);
            continue;
        }

        // 1. Рисуем игрока в списке слева на экране
        char pBuf[256];
        snprintf(pBuf, sizeof(pBuf), "%s (ID: %d)", it->second.data.name, it->second.data.playerId);
        PrintTextOnScreen(20.0f, startY, pBuf, {255, 255, 255, 255});
        startY += 20.0f;

        // 2. Рисуем ИМЯ НАД ГОЛОВОЙ (Nametag)
        CVector playerPos = { it->second.data.x, it->second.data.y, it->second.data.z + 1.0f };
        CVector screenPos;
        float w, h;
        
        if (CSprite_CalcScreenCoors(&playerPos, &screenPos, &w, &h, false, false)) {
            CRGBA nameColor = (it->second.data.playerId == 999) ? CRGBA{0, 150, 255, 255} : CRGBA{255, 0, 0, 255};
            PrintTextOnScreen(screenPos.x - 20.0f, screenPos.y, it->second.data.name, nameColor, 0.3f, 0.6f);
        }

        ++it;
    }
}

void __cdecl Hooked_CHud_Draw() {
    if (original_CHud_Draw) original_CHud_Draw();
    __try { DrawAllTexts(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("CRASH in Hooked_CHud_Draw"); }
}

// --- СЕТЕВОЙ ПОТОК ---
void NetworkThread() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    
    srand(GetTickCount());
    snprintf(myData.name, sizeof(myData.name), "Player_%d", rand() % 9999);

    SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    u_long mode = 1;
    ioctlsocket(clientSocket, FIONBIO, &mode);

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    while (true) {
        DWORD* playerBase = (DWORD*)PLAYER_BASE_POINTER;
        if (playerBase && *playerBase != 0) {
            DWORD matrixPtr = *(DWORD*)(*playerBase + 0x14);
            if (matrixPtr != 0) {
                myData.x = *(float*)(matrixPtr + 0x30);
                myData.y = *(float*)(matrixPtr + 0x34);
                myData.z = *(float*)(matrixPtr + 0x38);
                myData.rotation = *(float*)(*playerBase + 0x558); 
                
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
            RemotePlayer rp;
            rp.data = *pData;
            rp.lastUpdateTick = GetTickCount();
            remotePlayers[pData->playerId] = rp;
        }

        Sleep(30);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        FILE* f = fopen("client_log.txt", "w"); if (f) fclose(f);
        Log("Client Loaded!");
        
        if (MH_Initialize() == MH_OK) {
            MH_CreateHook((LPVOID)0x58FAE0, &Hooked_CHud_Draw, (LPVOID*)&original_CHud_Draw);
            MH_EnableHook(MH_ALL_HOOKS);
            Log("Hooks installed successfully.");
        }
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)NetworkThread, NULL, 0, NULL);
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}
