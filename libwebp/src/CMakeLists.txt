cmake_minimum_required(VERSION 3.12)

project(libwebp_benchmarks C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find system libwebp
set(WebP_ROOT "C:/Users/s248508/Documents/benchmark-libraries/libwebp")
set(WebP_LIBRARIES "${WebP_ROOT}/lib")
set(WebP_INCLUDE_DIRS "${WebP_ROOT}/include")

# Encoder
add_executable(libwebp_encode libwebp_encode.c getopt.c)
target_link_libraries(libwebp_encode PRIVATE "${WebP_LIBRARIES}/libwebp.lib")
target_include_directories(libwebp_encode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${WebP_INCLUDE_DIRS})

# Decoders
add_executable(libwebp_decode libwebp_decode.c getopt.c)
target_link_libraries(libwebp_decode PRIVATE "${WebP_LIBRARIES}/libwebp.lib")
target_include_directories(libwebp_decode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${WebP_INCLUDE_DIRS})
