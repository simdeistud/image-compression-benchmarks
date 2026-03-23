cmake_minimum_required(VERSION 3.12)

project(jpegli_benchmarks)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find system jpegli (provides target JPEG::JPEG on modern CMake)
find_package(JPEG REQUIRED)


# Encoder
add_executable(jpegli_encode jpegli_encode.c)
target_link_libraries(jpegli_encode PRIVATE JPEG::JPEG)
target_include_directories(jpegli_encode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Decoders
add_executable(jpegli_decode jpegli_decode.c)
target_link_libraries(jpegli_decode PRIVATE JPEG::JPEG)
target_include_directories(jpegli_decode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})