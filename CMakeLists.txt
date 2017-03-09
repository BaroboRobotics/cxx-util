cmake_minimum_required(VERSION 3.5)
project(cxx-util VERSION 0.2.0 LANGUAGES CXX)

set(AVR OFF)
if(CMAKE_SYSTEM_NAME MATCHES "AVR")
    set(AVR ON)
endif()

set(requiredCxxFeatures
    cxx_return_type_deduction
)

if(AVR)
    add_library(cxx-util INTERFACE)
    target_compile_features(cxx-util
        INTERFACE
            ${requiredCxxFeatures}
    )
else()
    set(Boost_USE_STATIC_LIBS ON)
    find_package(Boost 1.54.0 REQUIRED COMPONENTS system filesystem thread log date_time regex program_options)
    #find_package(websocketpp 0.8.0 REQUIRED)

    set(sources src/iothread.cpp src/log.cpp src/programpath.cpp src/version.cpp)
    add_library(cxx-util STATIC ${sources})
    set_target_properties(cxx-util
        PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR}
            MACOSX_RPATH ON
    )
    target_compile_features(cxx-util
        PUBLIC
            ${requiredCxxFeatures}
    )
    target_link_libraries(cxx-util PUBLIC Boost::filesystem Boost::log Boost::program_options)
    target_include_directories(cxx-util
        PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        #PUBLIC ${WEBSOCKETPP_INCLUDE_DIR}
    )
    target_compile_definitions(cxx-util
        PUBLIC
            #_WEBSOCKETPP_NO_CPP11_SYSTEM_ERROR_
            #_WEBSOCKETPP_CPP11_RANDOM_DEVICE_
            WINVER=0x0600
            _WIN32_WINNT=0x0600
            # By explicitly targeting Windows Vista and later at build time, we avoid some painful
            # linker issues with Boost.Log.
    )

    option(CXXUTIL_BUILD_TESTS "Build cxx-util tests" ON)
    if(CXXUTIL_BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
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
