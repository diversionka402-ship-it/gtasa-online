#pragma once

// Пакет, который отправляется по сети
struct PlayerData {
    int playerId;
    char name[32]; // Имя игрока
    float x;
    float y;
    float z;
    float rotation; // Куда смотрит игрок (угол поворота)
};
