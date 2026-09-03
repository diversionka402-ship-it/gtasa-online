#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <mutex>
#include <stdio.h>
#include <stdarg.h>
#include <MinHook.h>
#include "shared.h"

#pragma comment(lib, "ws2_32.lib")

void Log(const char* fmt, ...) {
    FILE* f = fopen("client_log.txt", "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fprintf(f, "\n");
        fclose(f);
    }
}

const DWORD PLAYER_BASE_POINTER = 0xB6F5F0; 

#pragma pack(push, 1)
struct CRGBA { unsigned char r, g, b, a; };
struct CVector { float x, y, z; };
#pragma pack(pop)

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
typedef void(__cdecl* tCFont_SetOrientation)(unsigned char align);
tCFont_SetOrientation CFont_SetOrientation = (tCFont_SetOrientation)0x7194F0;
typedef void(__cdecl* tCFont_SetJustify)(unsigned char on);
tCFont_SetJustify CFont_SetJustify = (tCFont_SetJustify)0x7195A0;

// --- ФУНКЦИИ ИГРЫ (Загрузка моделей и Спавн Педов) ---
typedef void(__cdecl* tCStreaming_RequestModel)(int id, int flags);
tCStreaming_RequestModel CStreaming_RequestModel = (tCStreaming_RequestModel)0x4087E0;
typedef void(__cdecl* tCStreaming_LoadAllRequestedModels)(bool onlyPriority);
tCStreaming_LoadAllRequestedModels CStreaming_LoadAllRequestedModels = (tCStreaming_LoadAllRequestedModels)0x40EA10;
typedef bool(__cdecl* tCStreaming_HasModelLoaded)(int id);
tCStreaming_HasModelLoaded CStreaming_HasModelLoaded = (tCStreaming_HasModelLoaded)0x4044C0;
typedef DWORD(__cdecl* tCPopulation_AddPed)(int pedType, int modelIndex, CVector* pos, bool makeMissionPed);
tCPopulation_AddPed CPopulation_AddPed = (tCPopulation_AddPed)0x612710;
typedef void(__cdecl* tCPopulation_RemovePed)(DWORD ped);
tCPopulation_RemovePed CPopulation_RemovePed = (tCPopulation_RemovePed)0x611550;
typedef void(__thiscall* tCEntity_UpdateRwFrame)(DWORD entity);
tCEntity_UpdateRwFrame CEntity_UpdateRwFrame = (tCEntity_UpdateRwFrame)0x532B00;
typedef void(__cdecl* tCWorld_Add)(DWORD entity);
tCWorld_Add CWorld_Add = (tCWorld_Add)0x563220;
typedef void(__cdecl* tCWorld_Remove)(DWORD entity);
tCWorld_Remove CWorld_Remove = (tCWorld_Remove)0x563280;

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ---
PlayerData myData = {0, "", 0.0f, 0.0f, 0.0f, 0.0f};

struct RemotePlayer {
    PlayerData data;
    DWORD lastUpdateTick;
    DWORD pedPointer; 
};

std::map<int, RemotePlayer> remotePlayers;
std::mutex playersMutex;

typedef void(__cdecl* tCHud_Draw)();
tCHud_Draw original_CHud_Draw = nullptr;

typedef void(__cdecl* tCGame_Process)();
tCGame_Process original_CGame_Process = nullptr;

// --- УПРОЩЕННАЯ И УЛУЧШЕННАЯ ОТРИСОВКА ТЕКСТА ---
void PrintTextOnScreen(float x, float y, const char* text, CRGBA color, float scaleX = 0.5f, float scaleY = 1.0f) {
    unsigned short gxtString[256];
    AsciiToGxtChar(text, gxtString);
    
    CFont_SetScale(scaleX, scaleY); 
    CFont_SetColor(color);
    CFont_SetFontStyle(1); 
    CFont_SetProportional(true);
    CFont_SetOrientation(1); 
    CFont_SetJustify(0); 
    CFont_SetDropShadowPosition(1);
    CFont_SetDropColor({0, 0, 0, 255});
    
    CFont_PrintString(x, y, gxtString);
}

// --- ИГРОВАЯ ЛОГИКА (БЕЗОПАСНЫЙ ПОТОК ДЛЯ СПАВНА И МАТРИЦ) ---
void ProcessGameLogic() {
    // 1. ПРОВЕРКА НАЖАТИЯ F1 (ТЕЛЕПОРТ К БОТУ)
    static bool f1Pressed = false;
    if (GetAsyncKeyState(VK_F1) & 0x8000) {
        if (!f1Pressed) {
            f1Pressed = true;
            std::lock_guard<std::mutex> lock(playersMutex);
            if (!remotePlayers.empty()) {
                auto it = remotePlayers.begin();
                DWORD localPed = *(DWORD*)PLAYER_BASE_POINTER;
                if (localPed) {
                    // Удаляем локального игрока из мира перед ТП
                    CWorld_Remove(localPed);
                    
                    DWORD matrix = *(DWORD*)(localPed + 0x14);
                    if (matrix) {
                        *(float*)(matrix + 0x30) = it->second.data.x;
                        *(float*)(matrix + 0x34) = it->second.data.y;
                        *(float*)(matrix + 0x38) = it->second.data.z + 1.0f; // чуть выше земли
                    } else {
                        *(float*)(localPed + 0x4) = it->second.data.x;
                        *(float*)(localPed + 0x8) = it->second.data.y;
                        *(float*)(localPed + 0xC) = it->second.data.z + 1.0f;
                    }
                    
                    CEntity_UpdateRwFrame(localPed);
                    CWorld_Add(localPed); // Возвращаем в мир
                    Log("[TELEPORT] Teleported local player to bot %s", it->second.data.name);
                }
            }
        }
    } else {
        f1Pressed = false;
    }

    // 2. ОБНОВЛЕНИЕ БОТОВ
    std::vector<int> activeIds;
    {
        std::lock_guard<std::mutex> lock(playersMutex);
        DWORD currentTick = GetTickCount();

        for (auto it = remotePlayers.begin(); it != remotePlayers.end(); ) {
            if (currentTick - it->second.lastUpdateTick > 10000) {
                if (it->second.pedPointer != 0) {
                    CPopulation_RemovePed(it->second.pedPointer);
                }
                it = remotePlayers.erase(it);
            } else {
                activeIds.push_back(it->first);
                ++it;
            }
        }
    }

    for (int id : activeIds) {
        RemotePlayer rp;
        {
            std::lock_guard<std::mutex> lock(playersMutex);
            if (remotePlayers.find(id) == remotePlayers.end()) continue;
            rp = remotePlayers[id];
        }

        if (rp.pedPointer == 0) {
            int modelId = 137; 
            if (!CStreaming_HasModelLoaded(modelId)) {
                CStreaming_RequestModel(modelId, 2);
                CStreaming_LoadAllRequestedModels(false); // ПРИНУДИТЕЛЬНАЯ ЗАГРУЗКА (Безопасно здесь!)
            } 
            if (CStreaming_HasModelLoaded(modelId)) {
                CVector pos = { rp.data.x, rp.data.y, rp.data.z };
                DWORD newPed = CPopulation_AddPed(1, modelId, &pos, 1);
                
                if (newPed) {
                    std::lock_guard<std::mutex> lock(playersMutex);
                    if (remotePlayers.find(id) != remotePlayers.end()) {
                        remotePlayers[id].pedPointer = newPed;
                    } else {
                        CPopulation_RemovePed(newPed);
                    }
                }
            }
        } else {
            DWORD ped = rp.pedPointer;
            *(float*)(ped + 0x540) = 1000.0f; // Бессмертие
            
            // ВЕРНУЛ Remove и Add. Без этого движок игры делает бота НЕВИДИМЫМ!
            CWorld_Remove(ped);

            DWORD matrix = *(DWORD*)(ped + 0x14);
            if (matrix != 0) {
                *(float*)(matrix + 0x30) = rp.data.x;
                *(float*)(matrix + 0x34) = rp.data.y;
                *(float*)(matrix + 0x38) = rp.data.z;
            } else {
                *(float*)(ped + 0x4) = rp.data.x;
                *(float*)(ped + 0x8) = rp.data.y;
                *(float*)(ped + 0xC) = rp.data.z;
            }
            *(float*)(ped + 0x558) = rp.data.rotation;
            
            CEntity_UpdateRwFrame(ped);
            CWorld_Add(ped); // Добавляем обратно в сектор отрисовки
        }
    }
}

// --- ОТРИСОВКА ИНТЕРФЕЙСА (ТОЛЬКО ТЕКСТ) ---
void DrawAllTexts() {
    int screenHeight = *(int*)0xC17048; 
    float startX = 30.0f; 
    float startY = screenHeight / 3.0f; 
    
    PrintTextOnScreen(startX, startY, "--- ONLINE PLAYERS ---", {255, 200, 0, 255}, 0.6f, 1.2f);
    startY += 30.0f;

    char myBuf[256];
    snprintf(myBuf, sizeof(myBuf), "%s | X:%.1f Y:%.1f Z:%.1f", myData.name, myData.x, myData.y, myData.z);
    PrintTextOnScreen(startX, startY, myBuf, {0, 255, 0, 255}, 0.5f, 1.0f);
    startY += 30.0f;

    std::vector<RemotePlayer> playersToDraw;
    {
        std::lock_guard<std::mutex> lock(playersMutex);
        for (auto const& pair : remotePlayers) {
            playersToDraw.push_back(pair.second);
        }
    }

    for (const auto& rp : playersToDraw) {
        // Отрисовка в левом списке С КООРДИНАТАМИ БОТА
        char pBuf[256];
        snprintf(pBuf, sizeof(pBuf), "%s (ID: %d) | X:%.1f Y:%.1f Z:%.1f", rp.data.name, rp.data.playerId, rp.data.x, rp.data.y, rp.data.z);
        PrintTextOnScreen(startX, startY, pBuf, {255, 255, 255, 255}, 0.5f, 1.0f);
        startY += 30.0f;

        if (rp.pedPointer != 0) {
            CVector playerPos = { rp.data.x, rp.data.y, rp.data.z + 1.1f };
            CVector screenPos;
            float w, h;
            
            if (CSprite_CalcScreenCoors(&playerPos, &screenPos, &w, &h, false, false)) {
                CRGBA nameColor = (rp.data.playerId == 999) ? CRGBA{0, 150, 255, 255} : CRGBA{255, 0, 0, 255};
                PrintTextOnScreen(screenPos.x - 30.0f, screenPos.y, rp.data.name, nameColor, 0.4f, 0.8f);
            }
        }
    }
}

void __cdecl Hooked_CGame_Process() {
    if (original_CGame_Process) original_CGame_Process();
    __try { ProcessGameLogic(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("[LOGIC] CRASH in ProcessGameLogic!"); }
}

void __cdecl Hooked_CHud_Draw() {
    if (original_CHud_Draw) original_CHud_Draw();
    __try { DrawAllTexts(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("[HUD] CRASH in DrawAllTexts!"); }
}

void ProcessIncomingPacket(PlayerData* pData) {
    std::lock_guard<std::mutex> lock(playersMutex);

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

    Log("[NET] Network thread started.");

    while (true) {
        __try {
            DWORD ped = *(DWORD*)PLAYER_BASE_POINTER;
            if (ped != 0) {
                DWORD matrix = *(DWORD*)(ped + 0x14);
                if (matrix != 0) {
                    myData.x = *(float*)(matrix + 0x30);
                    myData.y = *(float*)(matrix + 0x34);
                    myData.z = *(float*)(matrix + 0x38);
                } else {
                    myData.x = *(float*)(ped + 0x4);
                    myData.y = *(float*)(ped + 0x8);
                    myData.z = *(float*)(ped + 0xC);
                }
                myData.rotation = *(float*)(ped + 0x558); 
                
                sendto(clientSocket, (char*)&myData, sizeof(PlayerData), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
            }

            char buffer[512];
            sockaddr_in fromAddr;
            int fromLen = sizeof(fromAddr);
            
            while (true) {
                int bytesIn = recvfrom(clientSocket, buffer, sizeof(buffer), 0, (sockaddr*)&fromAddr, &fromLen);
                if (bytesIn <= 0) break; 
                
                if (bytesIn == sizeof(PlayerData)) {
                    ProcessIncomingPacket((PlayerData*)buffer);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("[NET] CRASH in NetworkThread loop!");
        }

        Sleep(30);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        FILE* f = fopen("client_log.txt", "w"); if (f) { fprintf(f, "--- NEW SESSION START ---\n"); fclose(f); }
        Log("Client Loaded!");
        
        if (MH_Initialize() == MH_OK) {
            MH_CreateHook((LPVOID)0x58FAE0, &Hooked_CHud_Draw, (LPVOID*)&original_CHud_Draw);
            MH_CreateHook((LPVOID)0x53BEE0, &Hooked_CGame_Process, (LPVOID*)&original_CGame_Process);
            MH_EnableHook(MH_ALL_HOOKS);
            Log("Hooks installed successfully.");
        }
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)NetworkThread, NULL, 0, NULL);
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
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
