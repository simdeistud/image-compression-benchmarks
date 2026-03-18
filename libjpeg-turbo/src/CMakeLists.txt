cmake_minimum_required(VERSION 3.20)
project(libjpeg_turbo_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(JPEG REQUIRED)

add_executable(libjpeg_turbo_encode libjpeg_turbo_encode.cpp)
add_executable(libjpeg_turbo_decode libjpeg_turbo_decode.cpp)

target_link_libraries(libjpeg_turbo_encode PRIVATE JPEG::JPEG)
target_link_libraries(libjpeg_turbo_decode PRIVATE JPEG::JPEG)

if(MSVC)
  target_compile_options(libjpeg_turbo_encode PRIVATE /W4 /EHsc)
  target_compile_options(libjpeg_turbo_decode PRIVATE /W4 /EHsc)
else()
  target_compile_options(libjpeg_turbo_encode PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(libjpeg_turbo_decode PRIVATE -Wall -Wextra -Wpedantic)
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/libjpeg_turbo_benchmark.py
               ${CMAKE_CURRENT_BINARY_DIR}/libjpeg_turbo_benchmark.py
               COPYONLY)
