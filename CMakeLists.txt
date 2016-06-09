cmake_minimum_required(VERSION 3.5)
project(cxx-util VERSION 0.2.0 LANGUAGES CXX)

set(AVR OFF)
if(CMAKE_SYSTEM_NAME MATCHES "AVR")
    set(AVR ON)
endif()

option(CXXUTIL_BUILD_TESTS "Build cxx-util tests" OFF)

if(AVR)
    add_library(cxx-util INTERFACE)
else()
    set(Boost_USE_STATIC_LIBS ON)
    find_package(Boost 1.54.0 REQUIRED COMPONENTS system filesystem thread log date_time regex)
    find_package(websocketpp 0.7.0 REQUIRED)
    message(STATUS "websocketpp at: ${WEBSOCKETPP_INCLUDE_DIR}")
    set(sources
        src/iothread.cpp
    )

    add_library(cxx-util STATIC ${sources})
    set_target_properties(cxx-util PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(cxx-util PUBLIC Boost::filesystem Boost::log)
    target_include_directories(cxx-util PRIVATE include PUBLIC
        ${WEBSOCKETPP_INCLUDE_DIR}
    )
endif()

if(CXXUTIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS cxx-util EXPORT barobo
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/ DESTINATION include)

# Boilerplate follows

export(EXPORT barobo
    FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-targets.cmake"
    NAMESPACE barobo::
)

install(EXPORT barobo
    FILE ${PROJECT_NAME}-targets.cmake
    NAMESPACE barobo::
    DESTINATION cmake
)

configure_file(${PROJECT_NAME}-config.cmake.in ${PROJECT_NAME}-config.cmake @ONLY)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-configVersion.cmake"
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
    "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-configVersion.cmake"
    DESTINATION cmake
)
