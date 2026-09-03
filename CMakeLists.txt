cmake_minimum_required(VERSION 3.10)
project(GTA_Custom_Online)

# Собираем Сервер (обычный EXE)
add_executable(Server server.cpp)
target_link_libraries(Server ws2_32) # Подключаем библиотеку сети Windows

# Собираем Клиент (DLL, которая станет .asi плагином)
add_library(Client SHARED client.cpp)
target_link_libraries(Client ws2_32)
set_target_properties(Client PROPERTIES SUFFIX ".asi") # Меняем расширение на .asi
