cmake_minimum_required(VERSION 3.12)

project(libavif-svt_benchmarks)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Encoder
add_executable(libavif-svt_encode libavif-svt_encode.c)
target_link_libraries(libavif-svt_encode PRIVATE avif)
target_include_directories(libavif-svt_encode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} avif)
