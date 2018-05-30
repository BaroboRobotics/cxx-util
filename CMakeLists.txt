cmake_minimum_required(VERSION 3.2.1)
project(cxx-util CXX)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

set(AVR OFF)
if(CMAKE_SYSTEM_NAME MATCHES "AVR")
    set(AVR ON)
endif()

option(CXXUTIL_BUILD_TESTS "Build cxx-util tests" OFF)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.54.0 REQUIRED COMPONENTS system filesystem log date_time regex iostreams)

if(AVR)
    add_library(cxx-util INTERFACE)
    target_link_libraries(cxx-util INTERFACE ${Boost_LIBRARIES})
    target_include_directories(cxx-util INTERFACE include ${Boost_INCLUDE_DIRS})
else()
    set(sources
        src/blob.cpp
        src/iothread.cpp
        )
    if(WIN32)
        list(APPEND sources src/windows/msi.cpp)
    endif()

    set_source_files_properties(${sources} PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
    add_library(cxx-util STATIC ${sources})
    set_target_properties(cxx-util PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(cxx-util PUBLIC ${Boost_LIBRARIES})
    target_include_directories(cxx-util PUBLIC include ${Boost_INCLUDE_DIRS})

    if(WIN32)
        target_link_libraries(cxx-util PUBLIC msi)
    endif()
endif()

if(CXXUTIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
