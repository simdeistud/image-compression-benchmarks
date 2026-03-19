cmake_minimum_required(VERSION 3.20)
project(libjxl_benchmarks LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  message(FATAL_ERROR "This project must be configured in Release mode only.")
endif()

find_path(JXL_INCLUDE_DIR NAMES jxl/encode.h jxl/decode.h REQUIRED)
find_library(JXL_LIBRARY NAMES jxl REQUIRED)

add_library(jxl::jxl UNKNOWN IMPORTED)
set_target_properties(jxl::jxl PROPERTIES
  IMPORTED_LOCATION "${JXL_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${JXL_INCLUDE_DIR}"
)

add_executable(libjxl_encode libjxl_encode.cpp)
target_link_libraries(libjxl_encode PRIVATE jxl::jxl)

add_executable(libjxl_decode libjxl_decode.cpp)
target_link_libraries(libjxl_decode PRIVATE jxl::jxl)
