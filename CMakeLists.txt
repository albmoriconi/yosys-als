cmake_minimum_required(VERSION 3.14)
project(yosys_als)

set(CMAKE_CXX_STANDARD 11)

include_directories(/usr/local/share/yosys/include
        /usr/local/include
        /usr/local/include/eigen3)

add_executable(yosys_als passes/als/als.cc passes/als/smtsynth.cc passes/als/smtsynth.h passes/als/smt_utils.cc passes/als/smt_utils.h passes/als/yosys_utils.cc passes/als/yosys_utils.h passes/als/graph.cc passes/als/graph.h passes/als/Optimizer.cc passes/als/Optimizer.h)