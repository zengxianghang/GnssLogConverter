cmake_minimum_required(VERSION 3.16)
project(GnssLogConverter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(gnsslog_core
    src/byte_io.cpp
    src/crc32.cpp
    src/range_converter.cpp
    src/novatel/novatel_protocol.cpp
    src/novatel/novatel_range.cpp
    src/unicore/unicore_protocol.cpp
    src/unicore/unicore_obsvm.cpp
)

target_include_directories(gnsslog_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
    target_compile_options(gnsslog_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(gnsslog_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(GnssLogConverter src/main.cpp)
target_link_libraries(GnssLogConverter PRIVATE gnsslog_core)

if(MSVC)
    target_compile_options(GnssLogConverter PRIVATE /W4 /permissive-)
else()
    target_compile_options(GnssLogConverter PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(gnsslog_tests tests/test_core.cpp)
target_link_libraries(gnsslog_tests PRIVATE gnsslog_core)
add_test(NAME gnsslog_tests COMMAND gnsslog_tests)
