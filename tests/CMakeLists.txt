set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.54.0 REQUIRED COMPONENTS system filesystem log thread)

##############################################################################
# Tests

set_source_files_properties(callback.cpp PROPERTIES COMPILE_FLAGS -std=c++11)
add_executable(callback callback.cpp)
target_include_directories(callback
    PRIVATE $<TARGET_PROPERTY:cxx-util,INTERFACE_INCLUDE_DIRECTORIES>
    )
add_test(NAME callback COMMAND callback)

set_source_files_properties(variant.cpp PROPERTIES
    COMPILE_FLAGS "-std=c++11")
add_executable(variant variant.cpp)
target_include_directories(variant
    PRIVATE $<TARGET_PROPERTY:cxx-util,INTERFACE_INCLUDE_DIRECTORIES>
    ${Boost_INCLUDE_DIRS}
    )
add_test(NAME variant COMMAND variant)
