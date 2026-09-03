#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <string>
#include <winsock2.h>
#include <map>
#include <windows.h>
#include <math.h>
#include "shared.h"

#pragma comment(lib, "ws2_32.lib")

struct ClientInfo {
    sockaddr_in addr;
    DWORD lastSeenTick;
    float lastX, lastY, lastZ;
};

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    u_long mode = 1;
    ioctlsocket(serverSocket, FIONBIO, &mode);
    
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    std::cout << "[SERVER] Started on port 7777. Waiting for players..." << std::endl;

    std::map<std::string, ClientInfo> clients;
    int nextPlayerId = 1;
    char buffer[512];
    DWORD lastBotUpdate = 0;

    while (true) {
        DWORD currentTick = GetTickCount();

        for (auto it = clients.begin(); it != clients.end(); ) {
            if (currentTick - it->second.lastSeenTick > 5000) {
                std::cout << "[SERVER] Player disconnected. IP: " << it->first << std::endl;
                it = clients.erase(it);
            } else {
                ++it;
            }
        }

        // Логика БОТА
        if (currentTick - lastBotUpdate > 30) {
            lastBotUpdate = currentTick;
            
            if (!clients.empty()) {
                auto firstClient = clients.begin();
                float cx = firstClient->second.lastX;
                float cy = firstClient->second.lastY;
                float cz = firstClient->second.lastZ;
                
                PlayerData botData;
                botData.playerId = 999;
                strcpy(botData.name, "Test_Bot");
                
                float t = currentTick / 500.0f;
                botData.x = cx + cos(t) * 4.0f;
                botData.y = cy + sin(t) * 4.0f;
                botData.z = cz;
                
                // ИСПРАВЛЕНИЕ: Бот теперь смотрит туда, куда бежит!
                botData.rotation = atan2(cos(t), -sin(t));
                
                for (auto const& clientPair : clients) {
                    sendto(serverSocket, (char*)&botData, sizeof(PlayerData), 0, (sockaddr*)&clientPair.second.addr, sizeof(clientPair.second.addr));
                }
            }
        }

        sockaddr_in clientAddr;
        int clientLength = sizeof(clientAddr);
        int bytesIn = recvfrom(serverSocket, buffer, sizeof(buffer), 0, (sockaddr*)&clientAddr, &clientLength);
        
        if (bytesIn == sizeof(PlayerData)) {
            PlayerData* data = (PlayerData*)buffer;
            std::string clientKey = std::to_string(clientAddr.sin_addr.s_addr) + ":" + std::to_string(clientAddr.sin_port);
            
            if (clients.find(clientKey) == clients.end()) {
                std::cout << "[SERVER] New player: " << data->name << " (ID: " << nextPlayerId << ")" << std::endl;
                data->playerId = nextPlayerId++;
            }

            clients[clientKey].addr = clientAddr;
            clients[clientKey].lastSeenTick = currentTick;
            clients[clientKey].lastX = data->x;
            clients[clientKey].lastY = data->y;
            clients[clientKey].lastZ = data->z;

            for (auto const& clientPair : clients) {
                if (clientPair.first != clientKey) {
                    sendto(serverSocket, (char*)data, sizeof(PlayerData), 0, (sockaddr*)&clientPair.second.addr, sizeof(clientPair.second.addr));
                }
            }
        }
        Sleep(5);
    }
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
