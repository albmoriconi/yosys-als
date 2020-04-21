project(Yosys-ALS LANGUAGES CXX)
cmake_minimum_required(VERSION 3.13.4)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_COMPILER /usr/bin/clang++)
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -ggdb3 -MD -fPIC -std=c++11 -Os")
add_definitions(-D_YOSYS_ -DYOSYS_ENABLE_READLINE -DYOSYS_ENABLE_PLUGINS -DYOSYS_ENABLE_GLOB -DYOSYS_ENABLE_ZLIB -DYOSYS_ENABLE_TCL -DYOSYS_ENABLE_ABC -DYOSYS_ENABLE_COVER)

set (TARGET_NAME 	als)
set (TARGET_SRC		${CMAKE_CURRENT_SOURCE_DIR}/passes/als)
set (YOSYS_SRC		/yosys)
set (BOOLECTOR_SRC 	/boolector/src)
set (BOOLECTOR_LIB	/boolector/build/lib)

add_library (
	${TARGET_NAME} SHARED
	${TARGET_SRC}/als.cc
	${TARGET_SRC}/graph.cc
	${TARGET_SRC}/Optimizer.cc
	${TARGET_SRC}/smtsynth.cc
	${TARGET_SRC}/smt_utils.cc
	${TARGET_SRC}/yosys_utils.cc
)

target_include_directories (
	${TARGET_NAME}
	PRIVATE /usr/local/include
	PRIVATE /usr/include/tcl8.6
	PRIVATE /usr/include/eigen3
	PRIVATE ${YOSYS_SRC}
	PRIVATE ${BOOLECTOR_SRC}
)	

target_link_directories (
	${TARGET_NAME}
	PRIVATE ${BOOLECTOR_LIB}
)

target_link_libraries (
	${TARGET_NAME}
	stdc++ 
	m 
	rt 
	readline 
	ffi
	dl
	z
	tcl8.6
	tclstub8.6
	boolector
)

target_link_options(
	${TARGET_NAME}
	PRIVATE -rdynamic
)



