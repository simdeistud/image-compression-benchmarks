cmake_minimum_required(VERSION 3.12)

project(libjpeg-turbo_benchmarks C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find system libjpeg-turbo (provides target JPEG::JPEG on modern CMake)
find_package(JPEG REQUIRED)


# Encoder
add_executable(libjpeg-turbo_encode libjpeg-turbo_encode.c)
target_link_libraries(libjpeg-turbo_encode PRIVATE JPEG::JPEG)
target_include_directories(libjpeg-turbo_encode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Decoders
add_executable(libjpeg-turbo_decode libjpeg-turbo_decode.c)
target_link_libraries(libjpeg-turbo_decode PRIVATE JPEG::JPEG)
target_include_directories(libjpeg-turbo_decode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})