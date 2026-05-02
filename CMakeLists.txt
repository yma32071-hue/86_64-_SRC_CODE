cmake_minimum_required(VERSION 3.16)
project(x86_64_emulator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(x86emu srcemulator.cpp)
target_include_directories(x86emu PRIVATE ${CMAKE_SOURCE_DIR})
