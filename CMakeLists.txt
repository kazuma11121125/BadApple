cmake_minimum_required(VERSION 3.18)

set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -O3 -march=native -mtune=native -ffast-math -fomit-frame-pointer -flto -funroll-loops -fpeel-loops -ftracer -finline-functions -finline-limit=10000")

project(Bad-Apple)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)
find_package(SFML COMPONENTS audio REQUIRED)
find_package(fmt REQUIRED)
find_package(OpenMP REQUIRED)

include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(Bad-Apple main.cpp)

target_link_libraries(Bad-Apple ${OpenCV_LIBS} sfml-audio fmt::fmt OpenMP::OpenMP_CXX)

# Aggressive optimization for latency reduction
target_compile_options(Bad-Apple PRIVATE 
    $<$<CONFIG:Release>:-O3 -O3 -march=native -mtune=native -ffast-math -fomit-frame-pointer -flto -funroll-loops -fpeel-loops -ftracer -finline-functions -finline-limit=10000 -fwhole-program>
    -Wno-unused-result
    -fno-strict-aliasing
    -fbranch-target-load-optimize
)

# Link-time optimization
set_target_properties(Bad-Apple PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)