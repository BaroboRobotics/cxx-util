cmake_minimum_required(VERSION 2.8)
project(cxx-util CXX)

option(CXXUTIL_BUILD_TESTS "Build cxx-util tests" OFF)

set(SOURCES
    include/util/callback.hpp
    )

# TODO: use an INTERFACE library instead in CMake 3.0
add_library(cxx-util OBJECT ${SOURCES})
target_include_directories(cxx-util PUBLIC include)

if(CXXUTIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
