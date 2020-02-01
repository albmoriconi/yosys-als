cmake_minimum_required(VERSION 3.15)
project(dbcreate)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(dbcreate
        smtsynth.cc
        smtsynth.h
        smt_utils.cc
        smt_utils.h
        main.cpp)

find_package(Boolector)
target_link_libraries(dbcreate Boolector::boolector)
