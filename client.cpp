#include <windows.h>
#include <winsock2.h>
#include <iostream>
#include "shared.h"

#pragma comment(lib, "ws2_32.lib")

// АДРЕСА ДЛЯ GTA SA 1.0 US (ДЛЯ STEAM НУЖНО ИСКАТЬ ДРУГИЕ!)
const DWORD PLAYER_BASE_POINTER = 0xB6F5F0; 
const DWORD OFFSET_X = 0x30; // Условные оффсеты матрицы координат
const DWORD OFFSET_Y = 0x34;
const DWORD OFFSET_Z = 0x38;

void NetworkThread() {
    // Открываем консоль для отладки прямо в игре
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    std::cout << "[CLIENT] Custom GTA Online Client Started!" << std::endl;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    // Делаем сокет неблокирующим (чтобы игра не зависала при ожидании данных)
    u_long mode = 1;
    ioctlsocket(clientSocket, FIONBIO, &mode);

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // IP ТВОЕГО СЕРВЕРА

    PlayerData myData = {0, 0.0f, 0.0f, 0.0f};

    while (true) {
        // 1. ЧТЕНИЕ КООРДИНАТ ИЗ ПАМЯТИ ИГРЫ
        DWORD* playerBase = (DWORD*)PLAYER_BASE_POINTER;
        if (*playerBase != 0) { // Если игрок заспавнился
            DWORD matrixPtr = *(DWORD*)(*playerBase + 0x14); // Указатель на матрицу
            if (matrixPtr != 0) {
                myData.x = *(float*)(matrixPtr + OFFSET_X);
                myData.y = *(float*)(matrixPtr + OFFSET_Y);
                myData.z = *(float*)(matrixPtr + OFFSET_Z);

                // Отправляем свои координаты на сервер
                sendto(clientSocket, (char*)&myData, sizeof(PlayerData), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
            }
        }

        // 2. ПОЛУЧЕНИЕ КООРДИНАТ ДРУГИХ ИГРОКОВ
        char buffer[512];
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);
        int bytesIn = recvfrom(clientSocket, buffer, sizeof(buffer), 0, (sockaddr*)&fromAddr, &fromLen);
        
        if (bytesIn == sizeof(PlayerData)) {
            PlayerData* remoteData = (PlayerData*)buffer;
            // Пока мы не умеем спавнить NPC, просто выводим их координаты в консоль
            std::cout << "Player " << remoteData->playerId << " is at X: " 
                      << remoteData->x << " Y: " << remoteData->y << std::endl;
            
            // ТУТ В БУДУЩЕМ БУДЕТ КОД СПАВНА И УПРАВЛЕНИЯ ПЕДАМИ (ДРУГИМИ ИГРОКАМИ)
        }

        Sleep(30); // 30 мс задержка (около 33 тиков в секунду)
    }
}

// Точка входа в DLL (Вызывается игрой при загрузке .asi)
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // Создаем отдельный поток для сети, чтобы не тормозить саму игру
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)NetworkThread, NULL, 0, NULL);
    }
    return TRUE;
}
