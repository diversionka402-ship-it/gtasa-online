cmake_minimum_required(VERSION 3.14)
project(GTA_Custom_Online)

# Скачиваем библиотеку MinHook автоматически
include(FetchContent)
FetchContent_Declare(
  minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(minhook)

# Собираем Сервер
add_executable(Server server.cpp)
target_link_libraries(Server ws2_32)

# Собираем Клиент
add_library(Client SHARED client.cpp)
target_link_libraries(Client ws2_32 minhook) # Подключаем minhook к клиенту
set_target_properties(Client PROPERTIES SUFFIX ".asi")
