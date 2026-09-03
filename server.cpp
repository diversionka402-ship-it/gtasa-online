#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <string>  // <-- Этого не хватало для работы std::to_string
#include <winsock2.h>
#include <map>
#include "shared.h"

#pragma comment(lib, "ws2_32.lib")

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));

    std::cout << "[SERVER] Started on port 7777. Waiting for players..." << std::endl;

    std::map<std::string, sockaddr_in> clients;
    int nextPlayerId = 1;

    char buffer[512];
    sockaddr_in clientAddr;
    int clientLength = sizeof(clientAddr);

    while (true) {
        int bytesIn = recvfrom(serverSocket, buffer, sizeof(buffer), 0, (sockaddr*)&clientAddr, &clientLength);
        if (bytesIn == sizeof(PlayerData)) {
            PlayerData* data = (PlayerData*)buffer;
            
            std::string clientKey = std::to_string(clientAddr.sin_addr.s_addr) + ":" + std::to_string(clientAddr.sin_port);
            
            if (clients.find(clientKey) == clients.end()) {
                clients[clientKey] = clientAddr;
                std::cout << "[SERVER] New player connected! Assigned ID: " << nextPlayerId << std::endl;
                data->playerId = nextPlayerId++;
            }

            for (auto const& [key, addr] : clients) {
                if (key != clientKey) {
                    sendto(serverSocket, (char*)data, sizeof(PlayerData), 0, (sockaddr*)&addr, sizeof(addr));
                }
            }
        }
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
