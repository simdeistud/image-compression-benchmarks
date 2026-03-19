cmake_minimum_required(VERSION 3.22)
project(libavif_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "" FORCE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVIF REQUIRED IMPORTED_TARGET libavif)

add_executable(libavif_encode libavif_encode.cpp)
add_executable(libavif_decode libavif_decode.cpp)

foreach(target IN ITEMS libavif_encode libavif_decode)
  target_link_libraries(${target} PRIVATE PkgConfig::AVIF)
  target_compile_options(${target} PRIVATE -O3 -DNDEBUG -march=native -mtune=native)
endforeach()
