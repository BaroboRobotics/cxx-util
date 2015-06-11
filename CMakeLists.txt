cmake_minimum_required(VERSION 3.2.1)
project(cxx-util CXX)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

option(CXXUTIL_BUILD_TESTS "Build cxx-util tests" OFF)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.54.0 REQUIRED COMPONENTS system filesystem log thread)

add_library(cxx-util INTERFACE)
target_link_libraries(cxx-util
	INTERFACE ${Boost_LIBRARIES})
target_include_directories(cxx-util INTERFACE include)

#set_target_properties(cxx-util PROPERTIES
#  VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
#  SOVERSION ${VERSION_MAJOR}
#  MACOSX_RPATH ON
#)

if(CXXUTIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
