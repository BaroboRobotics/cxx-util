cmake_minimum_required(VERSION 3.2.1)
project(cxx-util CXX)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

option(CXXUTIL_BUILD_TESTS "Build cxx-util tests" OFF)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.54.0 REQUIRED COMPONENTS system filesystem log thread date_time regex iostreams)

set(sources src/iothread.cpp)
set_source_files_properties(${sources} PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
add_library(cxx-util STATIC ${sources})
target_link_libraries(cxx-util PUBLIC ${Boost_LIBRARIES})
target_include_directories(cxx-util PUBLIC include ${Boost_INCLUDE_DIRS})

if(CXXUTIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
