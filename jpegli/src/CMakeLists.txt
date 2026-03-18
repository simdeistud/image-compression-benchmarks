cmake_minimum_required(VERSION 3.20)
project(jpegli_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(JPEG REQUIRED)

add_executable(jpegli_encode jpegli_encode.cpp)
add_executable(jpegli_decode jpegli_decode.cpp)

target_link_libraries(jpegli_encode PRIVATE JPEG::JPEG)
target_link_libraries(jpegli_decode PRIVATE JPEG::JPEG)

if(MSVC)
  target_compile_options(jpegli_encode PRIVATE /W4 /EHsc)
  target_compile_options(jpegli_decode PRIVATE /W4 /EHsc)
else()
  target_compile_options(jpegli_encode PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(jpegli_decode PRIVATE -Wall -Wextra -Wpedantic)
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/jpegli_benchmark.py
               ${CMAKE_CURRENT_BINARY_DIR}/jpegli_benchmark.py
               COPYONLY)
