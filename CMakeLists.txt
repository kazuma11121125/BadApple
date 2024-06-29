cmake_minimum_required(VERSION 3.18)

set(CMAKE_CXX_COMPILER "g++")

project(Bad-Apple)

find_package(OpenCV REQUIRED)

include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(Bad-Apple main.cpp)

target_link_libraries(Bad-Apple ${OpenCV_LIBS})

target_compile_options(Bad-Apple PRIVATE -O0)