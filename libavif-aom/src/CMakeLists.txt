cmake_minimum_required(VERSION 3.12)

project(libavif-aom_benchmarks)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find system libavif
#find_package(AVIF REQUIRED)

# Encoder
add_executable(libavif-aom_encode libavif-aom_encode.c)
target_link_libraries(libavif-aom_encode PRIVATE avif)
target_include_directories(libavif-aom_encode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} avif)

# Decoders
add_executable(libavif-aom_decode libavif-aom_decode.c)
target_link_libraries(libavif-aom_decode PRIVATE avif)
target_include_directories(libavif-aom_decode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} avif)
