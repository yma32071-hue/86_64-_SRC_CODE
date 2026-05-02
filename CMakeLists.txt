cmake_minimum_required(VERSION 3.16)
project(x86_64_emulator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# httplib needs pthreads and OpenSSL stubs
find_package(Threads REQUIRED)

add_executable(x86emu srcemulator.cpp)

target_include_directories(x86emu PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

target_compile_options(x86emu PRIVATE -O2 -Wno-unused-variable -Wno-misleading-indentation -DCPPHTTPLIB_NO_COMPRESS=1)
target_link_libraries(x86emu PRIVATE Threads::Threads)
