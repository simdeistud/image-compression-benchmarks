cmake_minimum_required(VERSION 3.16)

project(libwebp_benchmarks LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  message(FATAL_ERROR "This project must be configured with -DCMAKE_BUILD_TYPE=Release")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)

add_executable(libwebp_encode libwebp_encode.cpp)
add_executable(libwebp_decode libwebp_decode.cpp)

target_link_libraries(libwebp_encode PRIVATE PkgConfig::WEBP)
target_link_libraries(libwebp_decode PRIVATE PkgConfig::WEBP)

target_compile_options(libwebp_encode PRIVATE -O3 -DNDEBUG)
target_compile_options(libwebp_decode PRIVATE -O3 -DNDEBUG)
