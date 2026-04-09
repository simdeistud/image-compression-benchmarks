cmake_minimum_required(VERSION 3.12)

project(libjpeg-turbo_benchmarks)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find system libjpeg-turbo (provides target JPEG::JPEG on modern CMake)
set(JPEG_ROOT "C:/Users/s248508/Documents/benchmark-libraries/libjpeg-turbo64")
find_package(JPEG REQUIRED)

set(JPEG_DLL "C:/Users/s248508/Documents/benchmark-libraries/libjpeg-turbo64/bin")

function(copy_jpeg_dll target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${JPEG_DLL}"
            $<TARGET_FILE_DIR:${target}>
    )
endfunction()

# Encoder
add_executable(libjpeg-turbo_encode libjpeg-turbo_encode.c getopt.c)
target_link_libraries(libjpeg-turbo_encode PRIVATE JPEG::JPEG)
target_include_directories(libjpeg-turbo_encode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Decoders
add_executable(libjpeg-turbo_decode libjpeg-turbo_decode.c getopt.c)
target_link_libraries(libjpeg-turbo_decode PRIVATE JPEG::JPEG)
target_include_directories(libjpeg-turbo_decode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})


copy_jpeg_dll(libjpeg-turbo_encode)
copy_jpeg_dll(libjpeg-turbo_decode)