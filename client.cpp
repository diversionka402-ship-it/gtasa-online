#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <map>
#include <string>
#include <mutex>
#include <stdio.h>
#include <MinHook.h> // Наша библиотека для перехвата
#include "shared.h"

#pragma comment(lib, "ws2_32.lib")

// --- АДРЕСА ФУНКЦИЙ ИГРЫ (GTA SA 1.0 US) ---
const DWORD PLAYER_BASE_POINTER = 0xB6F5F0; 

// Структура цвета
struct CRGBA { unsigned char r, g, b, a; };

// Описываем функции GTA SA для работы с текстом
typedef void(__cdecl* tCFont_SetScale)(float x, float y);
tCFont_SetScale CFont_SetScale = (tCFont_SetScale)0x719380;

typedef void(__cdecl* tCFont_SetColor)(CRGBA color);
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
std::mutex playersMutex; // Защита от краша при одновременном доступе

// Указатель на оригинальную функцию отрисовки HUD
typedef void(__cdecl* tCHud_Draw)();
tCHud_Draw original_CHud_Draw = nullptr;

// Наша вспомогательная функция для удобного вывода текста
void PrintTextOnScreen(float x, float y, const char* text) {
    unsigned short gxtString[256];
    AsciiToGxtChar(text, gxtString); // Игра понимает только свою кодировку GXT
    CFont_SetScale(0.4f, 1.2f);
    CFont_SetColor({255, 255, 0, 255}); // Желтый цвет (R, G, B, A)
    CFont_SetFontStyle(1); // Шрифт GTA
    CFont_SetProportional(true);
    CFont_PrintString(x, y, gxtString);
}

// --- НАШ ПЕРЕХВАТЧИК (ВЫЗЫВАЕТСЯ ИГРОЙ 60 РАЗ В СЕКУНДУ) ---
void __cdecl Hooked_CHud_Draw() {
    // 1. Сначала даем игре нарисовать обычный интерфейс (радар, деньги)
    if (original_CHud_Draw) original_CHud_Draw();

    // 2. Теперь рисуем наши координаты прямо на экране!
    char buffer[256];
    sprintf_s(buffer, "My Pos: X: %.1f Y: %.1f Z: %.1f", myData.x, myData.y, myData.z);
    PrintTextOnScreen(20.0f, 200.0f, buffer); // Рисуем слева, чуть ниже радара
    
    // 3. Рисуем координаты других игроков
    std::lock_guard<std::mutex> lock(playersMutex);
    float startY = 230.0f;
    for (auto const& [id, data] : remotePlayers) {
        char pBuf[256];
        sprintf_s(pBuf, "Player %d: X: %.1f Y: %.1f Z: %.1f", data.playerId, data.x, data.y, data.z);
        PrintTextOnScreen(20.0f, startY, pBuf);
        startY += 25.0f;
    }
}

// --- СЕТЕВОЙ ПОТОК (Работает в фоне) ---
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
        // Читаем свои координаты из памяти
        DWORD* playerBase = (DWORD*)PLAYER_BASE_POINTER;
        if (*playerBase != 0) {
            DWORD matrixPtr = *(DWORD*)(*playerBase + 0x14);
            if (matrixPtr != 0) {
                myData.x = *(float*)(matrixPtr + 0x30);
                myData.y = *(float*)(matrixPtr + 0x34);
                myData.z = *(float*)(matrixPtr + 0x38);
                // Отправляем на сервер
                sendto(clientSocket, (char*)&myData, sizeof(PlayerData), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
            }
        }

        // Получаем координаты других игроков
        char buffer[512];
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);
        int bytesIn = recvfrom(clientSocket, buffer, sizeof(buffer), 0, (sockaddr*)&fromAddr, &fromLen);
        
        if (bytesIn == sizeof(PlayerData)) {
            PlayerData* pData = (PlayerData*)buffer;
            std::lock_guard<std::mutex> lock(playersMutex); // Блокируем, чтобы игра не крашнулась
            remotePlayers[pData->playerId] = *pData; // Обновляем данные друга
        }

        Sleep(30);
    }
}

// --- ИНИЦИАЛИЗАЦИЯ ---
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        
        // 1. Инициализируем перехватчик
        if (MH_Initialize() == MH_OK) {
            // Перехватываем функцию CHud::Draw (адрес 0x58FAE0)
            MH_CreateHook((LPVOID)0x58FAE0, &Hooked_CHud_Draw, (LPVOID*)&original_CHud_Draw);
            MH_EnableHook(MH_ALL_HOOKS); // Включаем хук
        }

        // 2. Запускаем сеть в отдельном потоке
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)NetworkThread, NULL, 0, NULL);
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}
