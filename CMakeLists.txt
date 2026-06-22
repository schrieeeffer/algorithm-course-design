cmake_minimum_required(VERSION 3.22)
project(gpu_scheduler)

set(CMAKE_CXX_STANDARD 17)

add_executable(execname
        src/main.cpp
        src/parser.cpp
        src/machine_state.cpp
        src/scheduler.cpp
        src/output.cpp)

target_compile_options(execname PRIVATE -O2 -pipe)
