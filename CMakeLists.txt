cmake_minimum_required(VERSION 3.18)

set(CMAKE_CXX_COMPILER "g++")

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

project(Bad-Apple)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)
find_package(SFML COMPONENTS audio REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})
find_package(OpenMP REQUIRED)


add_executable(Bad-Apple main.cpp)

target_link_libraries(Bad-Apple ${OpenCV_LIBS})
target_link_libraries(Bad-Apple sfml-audio)
target_link_libraries(Bad-Apple Threads::Threads)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")

# 最適化オプションの設定
target_compile_options(Bad-Apple PRIVATE 
    -O3 
    -march=native 
    -ffast-math 
    -fomit-frame-pointer 
    -flto 
    # -Wno-sign-compare
    -Wno-unused-result
    -fsanitize=address
)
