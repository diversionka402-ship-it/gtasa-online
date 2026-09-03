cmake_minimum_required(VERSION 3.14)

project(GTA_Custom_Online)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG master
)

FetchContent_MakeAvailable(minhook)

add_executable(Server
    server.cpp
)

target_include_directories(Server PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Server PRIVATE
    ws2_32
)

add_library(Client SHARED
    client.cpp
)

target_include_directories(Client PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Client PRIVATE
    ws2_32
    minhook
)

set_target_properties(Client PROPERTIES
    SUFFIX ".asi"
)
