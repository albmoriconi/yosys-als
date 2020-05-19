cmake_minimum_required(VERSION 3.12)
project(yosys-als)

set(CMAKE_CXX_STANDARD 11)

execute_process(COMMAND yosys-config --cxx OUTPUT_VARIABLE YOSYS_CXX)
string(STRIP ${YOSYS_CXX} YOSYS_CXX)
execute_process(COMMAND yosys-config --cxxflags OUTPUT_VARIABLE YOSYS_CXXFLAGS)
string(STRIP ${YOSYS_CXXFLAGS} YOSYS_CXXFLAGS)

set(CMAKE_CXX_COMPILER ${YOSYS_CXX})
set(CMAKE_CXX_FLAGS ${YOSYS_CXXFLAGS})

find_package(Boost REQUIRED)
find_package(Boolector REQUIRED)

set(TARGET als)
set(SRC_DIR passes/${TARGET})

add_executable(${TARGET}
        ${SRC_DIR}/als.cc
        ${SRC_DIR}/smtsynth.cc
        ${SRC_DIR}/smtsynth.h
        ${SRC_DIR}/smt_utils.cc
        ${SRC_DIR}/smt_utils.h
        ${SRC_DIR}/yosys_utils.cc
        ${SRC_DIR}/yosys_utils.h
        ${SRC_DIR}/graph.cc
        ${SRC_DIR}/graph.h
        ${SRC_DIR}/Optimizer.cc
        ${SRC_DIR}/Optimizer.h)

target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wpedantic)

set(CMAKE_CXX_LINK_EXECUTABLE "yosys-config --build ${TARGET}.so <OBJECTS> -lboolector")
