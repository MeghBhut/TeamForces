# CMakeLists.txt — cross-platform build for TeamForces.
#   cmake -S . -B build
#   cmake --build build
#
# Requires a build tool (make/ninja). If you don't have one on Windows, use
# build.ps1 instead, which drives g++ directly.
cmake_minimum_required(VERSION 3.15)
project(teamforces CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
add_executable(teamforces ${SOURCES})

target_include_directories(teamforces PRIVATE "${CMAKE_SOURCE_DIR}/third_party")

find_package(Threads REQUIRED)
target_link_libraries(teamforces PRIVATE Threads::Threads)

# Windows needs the Winsock library for cpp-httplib's sockets.
if (WIN32)
    target_link_libraries(teamforces PRIVATE ws2_32)
endif()
