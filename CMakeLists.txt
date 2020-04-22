project(Yosys-ALS LANGUAGES CXX)
cmake_minimum_required(VERSION 3.13.4)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/libs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Boost REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Boolector REQUIRED)

set (YOSYS_ROOT		/yosys)
set (YOSYS_CONFIG	${YOSYS_ROOT}/yosys-config)

execute_process(COMMAND ${YOSYS_CONFIG} --cxx OUTPUT_VARIABLE YOSYS_CXX)
string(STRIP ${YOSYS_CXX} YOSYS_CXX)

execute_process(COMMAND ${YOSYS_CONFIG} --cxxflags OUTPUT_VARIABLE YOSYS_CXXFLAGS)
string(STRIP ${YOSYS_CXXFLAGS} YOSYS_CXXFLAGS)

execute_process(COMMAND ${YOSYS_CONFIG} --ldflags OUTPUT_VARIABLE YOSYS_LINK_FLAGS)
string(STRIP ${YOSYS_LINK_FLAGS} YOSYS_LINK_FLAGS)

execute_process(COMMAND ${YOSYS_CONFIG} --ldlibs OUTPUT_VARIABLE YOSYS_LINK_LIBS)
string(STRIP ${YOSYS_LINK_LIBS} YOSYS_LINK_LIBS)

set(CMAKE_CXX_COMPILER ${YOSYS_CXX})
set(CMAKE_CXX_FLAGS ${YOSYS_CXXFLAGS})

set (TARGET_NAME 	als)
set (TARGET_SRC		${CMAKE_CURRENT_SOURCE_DIR}/passes/als)

add_library (
	${TARGET_NAME} SHARED
	${TARGET_SRC}/als.cc
	${TARGET_SRC}/graph.cc
	${TARGET_SRC}/Optimizer.cc
	${TARGET_SRC}/smt_context.cc
	${TARGET_SRC}/aig_model.cc
	${TARGET_SRC}/smt_utils.cc
	${TARGET_SRC}/yosys_utils.cc
)

target_include_directories (
	${TARGET_NAME}
	PRIVATE ${Boost_INCLUDE_DIRS}
	PRIVATE ${YOSYS_ROOT}
)	

target_link_directories (
	${TARGET_NAME}
	PRIVATE ${Boost_LIBRARY_DIRS}
)

target_link_libraries (
	${TARGET_NAME}
	${Boost_LIBRARIES}
	Eigen3::Eigen
	Boolector::boolector
)

target_link_options(
	${TARGET_NAME}
	PRIVATE "${YOSYS_LINK_FLAGS} ${YOSYS_LINK_LIBS}"
)



