cmake_minimum_required(VERSION 3.18)

# CMakeポリシーを設定してCUDA警告を抑制
# cmake_policy(SET CMP0146 OLD)

# 使用するコンパイラの指定
set(CMAKE_CXX_COMPILER "/usr/bin/g++")

# プロジェクト名の設定
project(Bad-Apple)

# CUDAを明示的に無効にする
set(WITH_CUDA OFF CACHE BOOL "Disable CUDA" FORCE)
set(OpenCV_FIND_QUIETLY ON)

# OpenCVのCUDA検索も無効にする
set(OpenCV_FIND_CUDA OFF CACHE BOOL "Disable OpenCV CUDA search" FORCE)

# pkg-configを使ってOpenCVを見つける
find_package(PkgConfig REQUIRED)
pkg_check_modules(OpenCV REQUIRED opencv4)

find_package(Threads REQUIRED)
find_package(SFML COMPONENTS audio REQUIRED)
find_package(OpenMP REQUIRED)
find_package(fmt REQUIRED)

include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(Bad-Apple main.cpp)

target_link_libraries(Bad-Apple ${OpenCV_LIBRARIES})
target_link_libraries(Bad-Apple sfml-audio)
target_link_libraries(Bad-Apple Threads::Threads)
target_link_libraries(Bad-Apple OpenMP::OpenMP_CXX)
target_link_libraries(Bad-Apple fmt::fmt)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fsanitize=address")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")

    target_compile_options(Bad-Apple PRIVATE 
        -O0                 
        -march=native       
        -ffast-math         
        -fomit-frame-pointer
        -Wno-unused-result  
        -fsanitize=address
    )
else()
    target_compile_options(Bad-Apple PRIVATE 
        -O3                 
        -march=native      
        -ffast-math       
        -fomit-frame-pointer
        -flto              
        -Wno-unused-result
    )
endif()