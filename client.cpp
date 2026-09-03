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

// НОВЫЕ ФУНКЦИИ ШРИФТА (Для красивой тени)
typedef void(__cdecl* tCFont_SetDropShadowPosition)(short pos);
tCFont_SetDropShadowPosition CFont_SetDropShadowPosition = (tCFont_SetDropShadowPosition)0x719570;

typedef void(__cdecl* tCFont_SetDropColor)(CRGBA color);
tCFont_SetDropColor CFont_SetDropColor = (tCFont_SetDropColor)0x719510;

typedef void(__cdecl* tAsciiToGxtChar)(const char* src, unsigned short* dst);
tAsciiToGxtChar AsciiToGxtChar = (tAsciiToGxtChar)0x718600;

typedef void(__cdecl* tCFont_PrintString)(float x, float y, unsigned short* text);
tCFont_PrintString CFont_PrintString = (tCFont_PrintString)0x71A700;

typedef void(__cdecl* tC3dMarkers_PlaceMarkerSet)(unsigned int id, unsigned short type, CVector* pos, float size, unsigned char r, unsigned char g, unsigned char b, unsigned char a, unsigned short pulsePeriod, float pulseFraction, short rotateRate);
tC3dMarkers_PlaceMarkerSet C3dMarkers_PlaceMarkerSet = (tC3dMarkers_PlaceMarkerSet)0x725AF0;

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

// --- ИСПРАВЛЕННАЯ ФУНКЦИЯ ОТРИСОВКИ ТЕКСТА ---
void PrintTextOnScreen(float x, float y, const char* text, CRGBA color) {
    unsigned short gxtString[256];
    AsciiToGxtChar(text, gxtString);
    
    // Исправлены пропорции (было 0.4, 1.2 - поэтому текст был кривой)
    CFont_SetScale(0.4f, 0.8f); 
    CFont_SetColor(color);
    CFont_SetFontStyle(1);
    CFont_SetProportional(true);
    
    // Добавляем черную тень, чтобы текст читался на любом фоне!
    CFont_SetDropShadowPosition(1);
    CRGBA shadow = {0, 0, 0, 255};
    CFont_SetDropColor(shadow);
    
    CFont_PrintString(x, y, gxtString);
}

// --- ОТРИСОВКА ИНТЕРФЕЙСА И ИГРОКОВ ---
void DrawAllTexts() {
    float startY = 150.0f;
    
    // Заголовок списка игроков
    PrintTextOnScreen(20.0f, startY, "--- ONLINE PLAYERS ---", {255, 200, 0, 255});
    startY += 20.0f;

    // Отрисовка себя
    char myBuf[256];
    snprintf(myBuf, sizeof(myBuf), "%s (Me)", myData.name);
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

        // 2. Рисуем 3D-маркер в мире
        CVector pos = { it->second.data.x, it->second.data.y, it->second.data.z - 1.0f };
        
        // Если это наш тестовый Бот (ID 999), делаем маркер СИНИМ, остальных КРАСНЫМИ
        if (it->second.data.playerId == 999) {
            C3dMarkers_PlaceMarkerSet(it->first, 1, &pos, 1.0f, 0, 150, 255, 255, 1024, 0.2f, 5);
        } else {
            C3dMarkers_PlaceMarkerSet(it->first, 1, &pos, 1.0f, 255, 0, 0, 255, 1024, 0.2f, 5);
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
    
    // Генерируем случайное имя при входе
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
        if (*playerBase != 0) {
            DWORD matrixPtr = *(DWORD*)(*playerBase + 0x14);
            if (matrixPtr != 0) {
                myData.x = *(float*)(matrixPtr + 0x30);
                myData.y = *(float*)(matrixPtr + 0x34);
                myData.z = *(float*)(matrixPtr + 0x38);
                // Считываем угол поворота персонажа (понадобится для синхронизации моделек)
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
        
        if (MH_Initialize() == MH_OK) {
            MH_CreateHook((LPVOID)0x58FAE0, &Hooked_CHud_Draw, (LPVOID*)&original_CHud_Draw);
            MH_EnableHook(MH_ALL_HOOKS);
        }
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)NetworkThread, NULL, 0, NULL);
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}
