cmake_minimum_required(VERSION 3.12)

project(gpujpeg_benchmarks C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Encoder
add_executable(gpujpeg_encode gpujpeg_encode.c)
target_link_libraries(gpujpeg_encode PRIVATE gpujpeg)

# Decoders
add_executable(gpujpeg_decode gpujpeg_decode.c)
target_link_libraries(gpujpeg_decode PRIVATE gpujpeg)
