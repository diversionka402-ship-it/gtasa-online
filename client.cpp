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

// --- ЛОГИРОВАНИЕ ---
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
struct CVector { float x, y, z; }; // Структура для 3D координат

typedef void(__cdecl* tCFont_SetScale)(float x, float y);
tCFont_SetScale CFont_SetScale = (tCFont_SetScale)0x719380;

typedef void(__cdecl* tCFont_SetColor)(CRGBA color);
tCFont_SetColor CFont_SetColor = (tCFont_SetColor)0x719430;

typedef void(__cdecl* tCFont_SetFontStyle)(short style);
tCFont_SetFontStyle CFont_SetFontStyle = (tCFont_SetFontStyle)0x719490;

typedef void(__cdecl* tCFont_SetProportional)(bool prop);
tCFont_SetProportional CFont_SetProportional = (tCFont_SetProportional)0x7195B0;

typedef void(__cdecl* tAsciiToGxtChar)(const char* src, unsigned short* dst);
tAsciiToGxtChar AsciiToGxtChar = (tAsciiToGxtChar)0x718600;

typedef void(__cdecl* tCFont_PrintString)(float x, float y, unsigned short* text);
tCFont_PrintString CFont_PrintString = (tCFont_PrintString)0x71A700;

// НОВОЕ: Функция игры для создания 3D-маркеров
typedef void(__cdecl* tC3dMarkers_PlaceMarkerSet)(unsigned int id, unsigned short type, CVector* pos, float size, unsigned char r, unsigned char g, unsigned char b, unsigned char a, unsigned short pulsePeriod, float pulseFraction, short rotateRate);
tC3dMarkers_PlaceMarkerSet C3dMarkers_PlaceMarkerSet = (tC3dMarkers_PlaceMarkerSet)0x725AF0;

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ---
PlayerData myData = {0, 0.0f, 0.0f, 0.0f};

// НОВОЕ: Структура для хранения времени последнего обновления от игрока
struct RemotePlayer {
    PlayerData data;
    DWORD lastUpdateTick;
};

std::map<int, RemotePlayer> remotePlayers;
std::mutex playersMutex;

typedef void(__cdecl* tCHud_Draw)();
tCHud_Draw original_CHud_Draw = nullptr;

void PrintTextOnScreen(float x, float y, const char* text) {
    unsigned short gxtString[256];
    AsciiToGxtChar(text, gxtString);
    CFont_SetScale(0.4f, 1.2f);
    CRGBA color = {255, 255, 0, 255};
    CFont_SetColor(color);
    CFont_SetFontStyle(1);
    CFont_SetProportional(true);
    CFont_PrintString(x, y, gxtString);
}

// --- ОТРИСОВКА И ЛОГИКА ---
void DrawAllTexts() {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "My Pos: X: %.1f Y: %.1f Z: %.1f", myData.x, myData.y, myData.z);
    PrintTextOnScreen(20.0f, 200.0f, buffer);

    std::lock_guard<std::mutex> lock(playersMutex);
    float startY = 230.0f;
    DWORD currentTick = GetTickCount();

    // Используем итератор, чтобы можно было удалять отключившихся игроков
    for (auto it = remotePlayers.begin(); it != remotePlayers.end(); ) {
        // Если от игрока нет вестей больше 3 секунд (3000 мс) - удаляем его
        if (currentTick - it->second.lastUpdateTick > 3000) {
            it = remotePlayers.erase(it);
            continue;
        }

        // 1. Рисуем текст на экране (как раньше)
        char pBuf[256];
        snprintf(pBuf, sizeof(pBuf), "Player %d: X: %.1f Y: %.1f Z: %.1f", 
                 it->second.data.playerId, 
                 it->second.data.x, 
                 it->second.data.y, 
                 it->second.data.z);
        PrintTextOnScreen(20.0f, startY, pBuf);
        startY += 25.0f;

        // 2. НОВОЕ: Рисуем 3D-маркер (цилиндр) на координатах игрока!
        // Опускаем Z на 1.0f, чтобы маркер был на уровне ног
        CVector pos = { it->second.data.x, it->second.data.y, it->second.data.z - 1.0f };
        
        // id = ID игрока, type = 1 (цилиндр), size = 1.0f, RGBA = 255,0,0,255 (Красный)
        C3dMarkers_PlaceMarkerSet(it->first, 1, &pos, 1.0f, 255, 0, 0, 255, 1024, 0.2f, 5);

        ++it;
    }
}

// --- ПЕРЕХВАТЧИК ---
void __cdecl Hooked_CHud_Draw() {
    if (original_CHud_Draw) {
        original_CHud_Draw();
    }
    __try {
        DrawAllTexts();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log(">>> CRASH CAUGHT inside Hooked_CHud_Draw custom drawing code! <<<");
    }
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
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Для теста с другом замени на IP сервера

    while (true) {
        // Отправка своих координат
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

        // Прием координат других игроков
        char buffer[512];
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);
        int bytesIn = recvfrom(clientSocket, buffer, sizeof(buffer), 0, (sockaddr*)&fromAddr, &fromLen);
        
        if (bytesIn == sizeof(PlayerData)) {
            PlayerData* pData = (PlayerData*)buffer;
            std::lock_guard<std::mutex> lock(playersMutex);
            
            // Сохраняем данные и текущее время (чтобы знать, когда игрок завис)
            RemotePlayer rp;
            rp.data = *pData;
            rp.lastUpdateTick = GetTickCount();
            remotePlayers[pData->playerId] = rp;
        }

        Sleep(30);
    }
}

// --- ИНИЦИАЛИЗАЦИЯ ---
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        FILE* f = fopen("client_log.txt", "w");
        if (f) fclose(f);
        Log("DllMain: DLL_PROCESS_ATTACH");

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
