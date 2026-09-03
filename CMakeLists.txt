cmake_minimum_required(VERSION 3.14)

project(GTA_Custom_Online)

# ============================================================
# C++ standard
# ============================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# MinHook
# ============================================================

include(FetchContent)

FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG master
)

FetchContent_MakeAvailable(minhook)

# ============================================================
# Plugin-SDK
# ============================================================

set(PLUGIN_SDK_DIR
    ${CMAKE_SOURCE_DIR}/plugin-sdk
)

# ============================================================
# SERVER
# ============================================================

add_executable(Server
    server.cpp
)

target_include_directories(Server PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Server
    ws2_32
)

# ============================================================
# CLIENT
# ============================================================

add_library(Client SHARED
    client.cpp
)

target_include_directories(Client PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PLUGIN_SDK_DIR}/shared
    ${PLUGIN_SDK_DIR}/plugin_sa
    ${PLUGIN_SDK_DIR}/plugin_sa/game_sa
)

target_link_libraries(Client PRIVATE
    ws2_32
    minhook
)

set_target_properties(Client PROPERTIES
    SUFFIX ".asi"
)
