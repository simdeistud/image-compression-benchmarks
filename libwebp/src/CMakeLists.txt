cmake_minimum_required(VERSION 3.12)

project(libwebp_benchmarks C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(MATH_LIBRARY m)
SET(LIBSHARPYUV sharpyuv)


# Find system libwebp
find_package(WebP REQUIRED)

# Encoder
add_executable(libwebp_encode libwebp_encode.c)
target_link_libraries(libwebp_encode PRIVATE ${WebP_LIBRARIES} ${MATH_LIBRARY} ${LIBSHARPYUV})
target_include_directories(libwebp_encode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${WebP_INCLUDE_DIRS})

# Decoders
add_executable(libwebp_decode libwebp_decode.c)
target_link_libraries(libwebp_decode PRIVATE ${WebP_LIBRARIES} ${MATH_LIBRARY} ${LIBSHARPYUV})
target_include_directories(libwebp_decode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${WebP_INCLUDE_DIRS})
