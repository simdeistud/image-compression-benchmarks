cmake_minimum_required(VERSION 3.12)

project(gpujpeg_benchmarks C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find system libjpeg-turbo (provides target JPEG::JPEG on modern CMake)
set(GPUJPEG_ROOT "C:/Users/s248508/Documents/benchmark-libraries/gpujpeg")

set(GPUJPEG_DLL "C:/Users/s248508/Documents/benchmark-libraries/gpujpeg/bin")

function(copy_jpeg_dll target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPUJPEG_DLL}"
            $<TARGET_FILE_DIR:${target}>
    )
endfunction()

# Encoder
add_executable(gpujpeg_encode gpujpeg_encode.c getopt.c)
target_link_directories(gpujpeg_encode PRIVATE "${GPUJPEG_ROOT}/lib")
target_link_libraries(gpujpeg_encode PRIVATE gpujpeg)
target_include_directories(gpujpeg_encode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${GPUJPEG_ROOT}/include")

# Decoders
add_executable(gpujpeg_decode gpujpeg_decode.c getopt.c)
target_link_directories(gpujpeg_decode PRIVATE "${GPUJPEG_ROOT}/lib")
target_link_libraries(gpujpeg_decode PRIVATE gpujpeg)
target_include_directories(gpujpeg_decode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${GPUJPEG_ROOT}/include")

copy_jpeg_dll(gpujpeg_encode)
copy_jpeg_dll(gpujpeg_decode)
