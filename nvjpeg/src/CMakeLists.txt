cmake_minimum_required(VERSION 3.12)

project(nvjpeg_benchmarks C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CUDA_ROOT "/usr/local/cuda")
set(CUDA_INCLUDE_DIR "${CUDA_ROOT}/include")
set(CUDA_LIB_DIR "${CUDA_ROOT}/lib64")
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CUDA_INCLUDE_DIR})
link_directories(${CUDA_LIB_DIR})


# Encoder
add_executable(nvjpeg_encode nvjpeg_encode.c)
target_link_libraries(nvjpeg_encode PRIVATE nvjpeg cudart)

# Decoders
add_executable(nvjpeg_decode nvjpeg_decode.c)
target_link_libraries(nvjpeg_decode PRIVATE nvjpeg cudart)
