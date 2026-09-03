#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <windows.h>

#include <iostream>
#include <map>
#include <string>
#include <cstring>
#include <cmath>

#include "shared.h"

#pragma comment(lib, "ws2_32.lib")

struct ClientInfo
{
    sockaddr_in addr{};
    DWORD lastSeenTick = 0;

    float lastX = 0.0f;
    float lastY = 0.0f;
    float lastZ = 0.0f;
};

int main()
{
    WSADATA wsa{};

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        std::cout << "[SERVER] WSAStartup failed.\n";
        return 1;
    }

    SOCKET serverSocket =
        socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (serverSocket == INVALID_SOCKET)
    {
        std::cout << "[SERVER] socket() failed.\n";
        WSACleanup();
        return 1;
    }

    u_long nonBlocking = 1;

    ioctlsocket(
        serverSocket,
        FIONBIO,
        &nonBlocking
    );

    sockaddr_in serverAddr{};

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(7777);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(
        serverSocket,
        reinterpret_cast<sockaddr*>(&serverAddr),
        sizeof(serverAddr)
    ) == SOCKET_ERROR)
    {
        std::cout << "[SERVER] bind() failed.\n";

        closesocket(serverSocket);
        WSACleanup();

        return 1;
    }

    std::cout
        << "[SERVER] Started on port 7777.\n";

    std::map<std::string, ClientInfo> clients;

    char buffer[512];

    DWORD lastBotTick = 0;

    while (true)
    {
        DWORD now = GetTickCount();

        // ====================================================
        // УДАЛЕНИЕ МЁРТВЫХ КЛИЕНТОВ
        // ====================================================

        for (auto it = clients.begin();
             it != clients.end();)
        {
            if (now - it->second.lastSeenTick > 5000)
            {
                std::cout
                    << "[SERVER] Player timeout: "
                    << it->first
                    << "\n";

                it = clients.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // ====================================================
        // ПРИНИМАЕМ ВСЕ ПАКЕТЫ
        // ====================================================

        while (true)
        {
            sockaddr_in clientAddr{};

            int clientLength =
                sizeof(clientAddr);

            int bytesIn = recvfrom(
                serverSocket,
                buffer,
                sizeof(buffer),
                0,
                reinterpret_cast<sockaddr*>(&clientAddr),
                &clientLength
            );

            if (bytesIn == SOCKET_ERROR)
            {
                int error = WSAGetLastError();

                if (error == WSAEWOULDBLOCK)
                    break;

                break;
            }

            if (bytesIn != sizeof(PlayerData))
                continue;

            PlayerData incoming{};

            std::memcpy(
                &incoming,
                buffer,
                sizeof(PlayerData)
            );

            if (!std::isfinite(incoming.x) ||
                !std::isfinite(incoming.y) ||
                !std::isfinite(incoming.z))
            {
                continue;
            }

            std::string clientKey =
                std::to_string(clientAddr.sin_addr.s_addr) +
                ":" +
                std::to_string(clientAddr.sin_port);

            auto found = clients.find(clientKey);

            if (found == clients.end())
            {
                ClientInfo info{};

                info.addr = clientAddr;
                info.lastSeenTick = now;

                info.lastX = incoming.x;
                info.lastY = incoming.y;
                info.lastZ = incoming.z;

                clients[clientKey] = info;

                std::cout
                    << "[SERVER] New player: "
                    << incoming.name
                    << " from "
                    << clientKey
                    << "\n";
            }
            else
            {
                found->second.addr = clientAddr;
                found->second.lastSeenTick = now;

                found->second.lastX = incoming.x;
                found->second.lastY = incoming.y;
                found->second.lastZ = incoming.z;
            }
        }

        // ====================================================
        // БОТ
        // ====================================================

        if (!clients.empty() &&
            now - lastBotTick >= 50)
        {
            lastBotTick = now;

            auto first = clients.begin();

            float px = first->second.lastX;
            float py = first->second.lastY;
            float pz = first->second.lastZ;

            static float angle = 0.0f;

            angle += 0.035f;

            if (angle > 6.2831853f)
                angle -= 6.2831853f;

            PlayerData bot{};

            bot.playerId = 999;

            std::strncpy(
                bot.name,
                "Test_Bot",
                sizeof(bot.name) - 1
            );

            bot.name[
                sizeof(bot.name) - 1
            ] = '\0';

            // Кружим вокруг игрока.
            bot.x =
                px + std::cos(angle) * 3.0f;

            bot.y =
                py + std::sin(angle) * 3.0f;

            bot.z = pz;

            bot.rotation =
                angle + 3.1415926f;

            // Отправляем бота всем клиентам.
            for (const auto& pair : clients)
            {
                const ClientInfo& client =
                    pair.second;

                sendto(
                    serverSocket,
                    reinterpret_cast<const char*>(&bot),
                    sizeof(PlayerData),
                    0,
                    reinterpret_cast<const sockaddr*>(&client.addr),
                    sizeof(client.addr)
                );
            }
        }

        Sleep(5);
    }

    closesocket(serverSocket);
    WSACleanup();

    return 0;
}
