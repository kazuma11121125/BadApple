cmake_minimum_required(VERSION 3.18)

set(CMAKE_CXX_COMPILER "g++")

project(Bad-Apple)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)
find_package(SFML COMPONENTS audio REQUIRED)

include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(Bad-Apple main.cpp)

target_link_libraries(Bad-Apple ${OpenCV_LIBS})
target_link_libraries(Bad-Apple sfml-audio)

# 最適化オプションの設定
target_compile_options(Bad-Apple PRIVATE 
    -O3 
    -march=native 
    -ffast-math 
    -fomit-frame-pointer 
    -flto 
    # -Wno-sign-compare
    -Wno-unused-result
)