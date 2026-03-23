cmake_minimum_required(VERSION 3.12)

project(libjpeg_benchmarks)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find system libjpeg (provides target JPEG::JPEG on modern CMake)
find_package(JPEG REQUIRED)


# Encoder
add_executable(libjpeg_encode libjpeg_encode.c)
target_link_libraries(libjpeg_encode PRIVATE JPEG::JPEG)
target_include_directories(libjpeg_encode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Decoder
add_executable(libjpeg_decode libjpeg_decode.c)
target_link_libraries(libjpeg_decode PRIVATE JPEG::JPEG)
target_include_directories(libjpeg_decode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})