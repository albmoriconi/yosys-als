cmake_minimum_required(VERSION 3.14.7)
project(yosys-als)

set(CMAKE_CXX_STANDARD 11)

execute_process(COMMAND yosys-config --cxx OUTPUT_VARIABLE YOSYS_CXX)
string(STRIP ${YOSYS_CXX} YOSYS_CXX)
execute_process(COMMAND yosys-config --cxxflags OUTPUT_VARIABLE YOSYS_CXXFLAGS)
string(STRIP ${YOSYS_CXXFLAGS} YOSYS_CXXFLAGS)
execute_process(COMMAND yosys-config --datdir OUTPUT_VARIABLE YOSYS_DATDIR)
string(STRIP ${YOSYS_DATDIR} YOSYS_DATDIR)

set(CMAKE_CXX_COMPILER ${YOSYS_CXX})
set(CMAKE_CXX_FLAGS ${YOSYS_CXXFLAGS})

find_package(Boost REQUIRED COMPONENTS filesystem)
find_package(Boolector REQUIRED)
find_package(SQLite3 REQUIRED)

set(TARGET als)
set(SRC_DIR passes/${TARGET}/source)
set(INC_DIR passes/${TARGET}/include)

include_directories(${INC_DIR})

add_executable(${TARGET}
        ${SRC_DIR}/als.cc
        ${SRC_DIR}/smtsynth.cc
        ${SRC_DIR}/smt_utils.cc
        ${SRC_DIR}/yosys_utils.cc
        ${SRC_DIR}/graph.cc
        ${SRC_DIR}/ErSEvaluator.cc
        ${SRC_DIR}/AlsWorker.cc
        ${INC_DIR}/smtsynth.h
        ${INC_DIR}/smt_utils.h
        ${INC_DIR}/yosys_utils.h
        ${INC_DIR}/graph.h
        ${INC_DIR}/Optimizer.h
        ${INC_DIR}/ErSEvaluator.h
        ${INC_DIR}/AlsWorker.h)

target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wpedantic)

set(LIBS "-lboolector -lboost_system -lboost_filesystem -lboost_serialization -lsqlite3")
set(CMAKE_CXX_LINK_EXECUTABLE "yosys-config --build ${TARGET}.so <OBJECTS> ${LIBS}")

add_custom_target(install_plugin
        COMMAND mkdir -p ${YOSYS_DATDIR}/plugins
        COMMAND cp als.so ${YOSYS_DATDIR}/plugins)
