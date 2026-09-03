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

// --- ФУНКЦИИ ИГРЫ (Шрифты и 2D) ---
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

// --- НОВЫЕ ФУНКЦИИ ИГРЫ (Загрузка моделей и Спавн Педов) ---
typedef void(__cdecl* tCStreaming_RequestModel)(int id, int flags);
tCStreaming_RequestModel CStreaming_RequestModel = (tCStreaming_RequestModel)0x4087E0;

typedef void(__cdecl* tCStreaming_LoadAllRequestedModels)(bool onlyPriority);
tCStreaming_LoadAllRequestedModels CStreaming_LoadAllRequestedModels = (tCStreaming_LoadAllRequestedModels)0x40EA10;

typedef bool(__cdecl* tCStreaming_HasModelLoaded)(int id);
tCStreaming_HasModelLoaded CStreaming_HasModelLoaded = (tCStreaming_HasModelLoaded)0x4044C0;

typedef DWORD(__cdecl* tCPopulation_AddPed)(int pedType, int modelIndex, CVector* pos, bool a4);
tCPopulation_AddPed CPopulation_AddPed = (tCPopulation_AddPed)0x612710;

typedef void(__cdecl* tCPopulation_RemovePed)(DWORD ped);
tCPopulation_RemovePed CPopulation_RemovePed = (tCPopulation_RemovePed)0x611550;

// __thiscall означает, что функция вызывается от лица объекта (this передается в регистр ecx)
typedef void(__thiscall* tCEntity_UpdateRwFrame)(DWORD entity);
tCEntity_UpdateRwFrame CEntity_UpdateRwFrame = (tCEntity_UpdateRwFrame)0x532B00;

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ---
PlayerData myData = {0, "", 0.0f, 0.0f, 0.0f, 0.0f};

struct RemotePlayer {
    PlayerData data;
    DWORD lastUpdateTick;
    DWORD pedPointer; // Указатель на 3D-модельку в памяти игры
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
    CFont_SetDropColor({0, 0, 0, 255});
    CFont_PrintString(x, y, gxtString);
}

// --- ОТРИСОВКА И ЛОГИКА СПАВНА ---
void DrawAllTexts() {
    int screenHeight = *(int*)0xC17048; 
    float startY = screenHeight - 300.0f; 
    
    PrintTextOnScreen(20.0f, startY, "--- ONLINE PLAYERS ---", {255, 200, 0, 255});
    startY += 20.0f;

    char myBuf[256];
    snprintf(myBuf, sizeof(myBuf), "%s | X:%.1f Y:%.1f Z:%.1f", myData.name, myData.x, myData.y, myData.z);
    PrintTextOnScreen(20.0f, startY, myBuf, {0, 255, 0, 255});
    startY += 20.0f;

    std::lock_guard<std::mutex> lock(playersMutex);
    DWORD currentTick = GetTickCount();

    for (auto it = remotePlayers.begin(); it != remotePlayers.end(); ) {
        // 1. Если игрок отключился (таймаут)
        if (currentTick - it->second.lastUpdateTick > 3000) {
            if (it->second.pedPointer != 0) {
                CPopulation_RemovePed(it->second.pedPointer); // Удаляем модельку из мира
            }
            it = remotePlayers.erase(it);
            continue;
        }

        // 2. ЛОГИКА СПАВНА И ОБНОВЛЕНИЯ ПЕДА
        if (it->second.pedPointer == 0) {
            int modelId = 137; // ID скина (137 = Бездомный с коробкой)
            
            if (!CStreaming_HasModelLoaded(modelId)) {
                // Просим игру загрузить модель
                CStreaming_RequestModel(modelId, 2);
                CStreaming_LoadAllRequestedModels(false);
            } else {
                // Модель загружена! Спавним педа.
                CVector pos = { it->second.data.x, it->second.data.y, it->second.data.z };
                it->second.pedPointer = CPopulation_AddPed(4, modelId, &pos, 0);
            }
        } else {
            // Пед уже существует, обновляем его координаты
            DWORD ped = it->second.pedPointer;
            DWORD matrix = *(DWORD*)(ped + 0x14);
            
            if (matrix != 0) {
                *(float*)(matrix + 0x30) = it->second.data.x;
                *(float*)(matrix + 0x34) = it->second.data.y;
                *(float*)(matrix + 0x38) = it->second.data.z;
                
                // Обновляем угол поворота
                *(float*)(ped + 0x558) = it->second.data.rotation;
                
                // Говорим движку RenderWare перерисовать модельку на новых координатах
                CEntity_UpdateRwFrame(ped);
            }
        }

        // 3. Отрисовка интерфейса
        char pBuf[256];
        snprintf(pBuf, sizeof(pBuf), "%s (ID: %d)", it->second.data.name, it->second.data.playerId);
        PrintTextOnScreen(20.0f, startY, pBuf, {255, 255, 255, 255});
        startY += 20.0f;

        // 4. Отрисовка Nametag над головой
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
            
            // Если игрок новый, инициализируем pedPointer нулем
            if (remotePlayers.find(pData->playerId) == remotePlayers.end()) {
                RemotePlayer rp;
                rp.data = *pData;
                rp.lastUpdateTick = GetTickCount();
                rp.pedPointer = 0; 
                remotePlayers[pData->playerId] = rp;
            } else {
                remotePlayers[pData->playerId].data = *pData;
                remotePlayers[pData->playerId].lastUpdateTick = GetTickCount();
            }
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
        // При выходе из игры удаляем всех созданных педов, чтобы игра не крашнулась
        std::lock_guard<std::mutex> lock(playersMutex);
        for (auto& pair : remotePlayers) {
            if (pair.second.pedPointer != 0) {
                CPopulation_RemovePed(pair.second.pedPointer);
            }
        }
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}
