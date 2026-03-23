cmake_minimum_required(VERSION 3.12)

project(mozjpeg_benchmarks)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find system mozjpeg (provides target JPEG::JPEG on modern CMake)
find_package(JPEG REQUIRED)


# Encoder
add_executable(mozjpeg_encode mozjpeg_encode.c)
target_link_libraries(mozjpeg_encode PRIVATE JPEG::JPEG)
target_include_directories(mozjpeg_encode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Decoders
add_executable(mozjpeg_decode mozjpeg_decode.c)
target_link_libraries(mozjpeg_decode PRIVATE JPEG::JPEG)
target_include_directories(mozjpeg_decode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})