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
    float lastX, lastY, lastZ; // Запоминаем позицию игрока, чтобы бот бегал вокруг него
};

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    // Делаем сокет неблокирующим (чтобы сервер мог параллельно управлять ботом)
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

        // 1. Очистка отключившихся клиентов (таймаут 5 секунд)
        for (auto it = clients.begin(); it != clients.end(); ) {
            if (currentTick - it->second.lastSeenTick > 5000) {
                std::cout << "[SERVER] Player timeout/disconnected. IP: " << it->first << std::endl;
                it = clients.erase(it);
            } else {
                ++it;
            }
        }

        // 2. Логика тестового БОТА (чтобы тебе было с кем тестить)
        if (currentTick - lastBotUpdate > 30) { // Обновляем бота каждые 30 мс
            lastBotUpdate = currentTick;
            
            if (!clients.empty()) {
                // Берем первого попавшегося реального игрока
                auto firstClient = clients.begin();
                float cx = firstClient->second.lastX;
                float cy = firstClient->second.lastY;
                float cz = firstClient->second.lastZ;
                
                // Создаем бота, который бегает по кругу радиусом 4 метра вокруг игрока
                PlayerData botData;
                botData.playerId = 999; // ID бота
                strcpy(botData.name, "Test_Bot");
                float t = currentTick / 500.0f; // Скорость бега
                botData.x = cx + cos(t) * 4.0f;
                botData.y = cy + sin(t) * 4.0f;
                botData.z = cz;
                botData.rotation = t;
                
                // Рассылаем бота всем игрокам
                for (auto const& clientPair : clients) {
                    sendto(serverSocket, (char*)&botData, sizeof(PlayerData), 0, (sockaddr*)&clientPair.second.addr, sizeof(clientPair.second.addr));
                }
            }
        }

        // 3. Чтение входящих пакетов от реальных игроков
        sockaddr_in clientAddr;
        int clientLength = sizeof(clientAddr);
        int bytesIn = recvfrom(serverSocket, buffer, sizeof(buffer), 0, (sockaddr*)&clientAddr, &clientLength);
        
        if (bytesIn == sizeof(PlayerData)) {
            PlayerData* data = (PlayerData*)buffer;
            std::string clientKey = std::to_string(clientAddr.sin_addr.s_addr) + ":" + std::to_string(clientAddr.sin_port);
            
            if (clients.find(clientKey) == clients.end()) {
                std::cout << "[SERVER] New player connected: " << data->name << " (Assigned ID: " << nextPlayerId << ")" << std::endl;
                data->playerId = nextPlayerId++;
            }

            // Обновляем данные клиента
            clients[clientKey].addr = clientAddr;
            clients[clientKey].lastSeenTick = currentTick;
            clients[clientKey].lastX = data->x;
            clients[clientKey].lastY = data->y;
            clients[clientKey].lastZ = data->z;

            // Рассылаем пакет этого игрока всем остальным
            for (auto const& clientPair : clients) {
                if (clientPair.first != clientKey) {
                    sendto(serverSocket, (char*)data, sizeof(PlayerData), 0, (sockaddr*)&clientPair.second.addr, sizeof(clientPair.second.addr));
                }
            }
        }

        Sleep(5); // Небольшая пауза, чтобы не сжечь процессор
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
