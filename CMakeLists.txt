# The PC side: the game's logic, checked against the original. The GameCube
# build (Octave) compiles the same src/game/ files.
cmake_minimum_required(VERSION 3.20)
project(BJ2GC CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(bj2game STATIC
    src/game/mtrand.cpp
    src/game/board.cpp
    src/game/game.cpp
)
target_include_directories(bj2game PUBLIC src)

add_executable(bj2check tools/check/main.cpp)
target_link_libraries(bj2check PRIVATE bj2game)
