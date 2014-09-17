cmake_minimum_required(VERSION 2.8.12)
project(cxx-util CXX)

option(CXXUTIL_BUILD_TESTS "Build cxx-util tests" OFF)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.54.0 REQUIRED COMPONENTS system filesystem log thread)

set(SOURCES
    dummySourceFile.cpp
    )

# TODO: use an INTERFACE library instead in CMake 3.0
add_library(cxx-util STATIC ${SOURCES})
target_link_libraries(cxx-util
	INTERFACE ${Boost_LIBRARIES})
target_include_directories(cxx-util INTERFACE include)

if(CXXUTIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
