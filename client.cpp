#define WIN32_LEAN_AND_MEAN             // Отключает конфликт старых и новых сокетов
#define _WINSOCK_DEPRECATED_NO_WARNINGS // Разрешает использовать inet_addr

#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include "shared.h"

#pragma comment(lib, "ws2_32.lib")

const DWORD PLAYER_BASE_POINTER = 0xB6F5F0; 
const DWORD OFFSET_X = 0x30;
const DWORD OFFSET_Y = 0x34;
const DWORD OFFSET_Z = 0x38;

void NetworkThread() {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    std::cout << "[CLIENT] Custom GTA Online Client Started!" << std::endl;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    u_long mode = 1;
    ioctlsocket(clientSocket, FIONBIO, &mode);

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    PlayerData myData = {0, 0.0f, 0.0f, 0.0f};

    while (true) {
        DWORD* playerBase = (DWORD*)PLAYER_BASE_POINTER;
        if (*playerBase != 0) {
            DWORD matrixPtr = *(DWORD*)(*playerBase + 0x14);
            if (matrixPtr != 0) {
                myData.x = *(float*)(matrixPtr + OFFSET_X);
                myData.y = *(float*)(matrixPtr + OFFSET_Y);
                myData.z = *(float*)(matrixPtr + OFFSET_Z);

                sendto(clientSocket, (char*)&myData, sizeof(PlayerData), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
            }
        }

        char buffer[512];
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);
        int bytesIn = recvfrom(clientSocket, buffer, sizeof(buffer), 0, (sockaddr*)&fromAddr, &fromLen);
        
        if (bytesIn == sizeof(PlayerData)) {
            PlayerData* remoteData = (PlayerData*)buffer;
            std::cout << "Player " << remoteData->playerId << " is at X: " 
                      << remoteData->x << " Y: " << remoteData->y << std::endl;
        }

        Sleep(30);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)NetworkThread, NULL, 0, NULL);
    }
    return TRUE;
}
