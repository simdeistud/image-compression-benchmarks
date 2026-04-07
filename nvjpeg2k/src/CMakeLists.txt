cmake_minimum_required(VERSION 3.20)
project(nvjpeg2k_benchmarksl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


find_package(CUDAToolkit REQUIRED)

if (DEFINED NVJPEG2K_PATH)
    find_path(NVJPEG2K_INCLUDE_DIR
        NAMES nvjpeg2k.h
        HINTS "${NVJPEG2K_PATH}"
        PATH_SUFFIXES
            include
            include/libnvjpeg2k/13
        NO_DEFAULT_PATH
    )

    find_library(NVJPEG2K_LIBRARY
        NAMES nvjpeg2k
        HINTS "${NVJPEG2K_PATH}"
        PATH_SUFFIXES
            lib
            lib64
            lib/x86_64-linux-gnu
            lib/x86_64-linux-gnu/libnvjpeg2k/13
            libnvjpeg2k/13
        NO_DEFAULT_PATH
    )
else()
    find_path(NVJPEG2K_INCLUDE_DIR
        NAMES nvjpeg2k.h
        PATH_SUFFIXES
            libnvjpeg2k/13
    )

    find_library(NVJPEG2K_LIBRARY
        NAMES nvjpeg2k
        PATH_SUFFIXES
            libnvjpeg2k/13
            x86_64-linux-gnu/libnvjpeg2k/13
    )
endif()


if (NOT NVJPEG2K_INCLUDE_DIR OR NOT NVJPEG2K_LIBRARY)
    message(FATAL_ERROR "Could not find nvjpeg2k. Pass -DNVJPEG2K_PATH=/path/to/nvjpeg2k.")
endif()

message(STATUS "nvJPEG2K include dir: ${NVJPEG2K_INCLUDE_DIR}")
message(STATUS "nvJPEG2K library    : ${NVJPEG2K_LIBRARY}")

add_executable(nvjpeg2k_encode nvjpeg2k_encode.cpp)
target_include_directories(nvjpeg2k_encode BEFORE PRIVATE "${NVJPEG2K_INCLUDE_DIR}")
target_link_libraries(nvjpeg2k_encode PRIVATE CUDA::cudart "${NVJPEG2K_LIBRARY}")

add_executable(nvjpeg2k_decode nvjpeg2k_decode.cpp)
target_include_directories(nvjpeg2k_decode BEFORE PRIVATE "${NVJPEG2K_INCLUDE_DIR}")
target_link_libraries(nvjpeg2k_decode PRIVATE CUDA::cudart "${NVJPEG2K_LIBRARY}")

add_executable(nvjpeg2k_test nvjpeg2k_test.cpp)
target_include_directories(nvjpeg2k_test BEFORE PRIVATE "${NVJPEG2K_INCLUDE_DIR}")
target_link_libraries(nvjpeg2k_test PRIVATE CUDA::cudart "${NVJPEG2K_LIBRARY}")

if (UNIX AND NOT APPLE)
    target_link_libraries(nvjpeg2k_encode PRIVATE m)
    target_link_libraries(nvjpeg2k_decode PRIVATE m)
    target_link_libraries(nvjpeg2k_test PRIVATE m)
endif()
