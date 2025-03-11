cmake_minimum_required(VERSION 3.18)

# 使用するコンパイラの指定
set(CMAKE_CXX_COMPILER "g++")

# プロジェクト名の設定
project(Bad-Apple)

# 必要なパッケージを探す
find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)
find_package(SFML COMPONENTS audio REQUIRED)
find_package(OpenMP REQUIRED)

include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(Bad-Apple main.cpp)

target_link_libraries(Bad-Apple ${OpenCV_LIBS})
target_link_libraries(Bad-Apple sfml-audio)
target_link_libraries(Bad-Apple Threads::Threads)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fsanitize=address")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")

    # デバッグ時は最適化を抑える
    target_compile_options(Bad-Apple PRIVATE 
        -O0                 
        -march=native       
        -ffast-math         
        -fomit-frame-pointer
        -Wno-unused-result  
        -fsanitize=address
    )
else()
    # リリース用オプション
    target_compile_options(Bad-Apple PRIVATE 
        -O3                 
        -march=native      
        -ffast-math       
        -fomit-frame-pointer
        -flto              
        -Wno-unused-result
    )
endif()
