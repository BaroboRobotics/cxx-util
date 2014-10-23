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

set_source_files_properties(deadlinescheduler.cpp PROPERTIES
    COMPILE_FLAGS "-std=c++11 -pthread")
add_executable(deadlinescheduler deadlinescheduler.cpp)
target_include_directories(deadlinescheduler
    PRIVATE $<TARGET_PROPERTY:cxx-util,INTERFACE_INCLUDE_DIRECTORIES>
    ${Boost_INCLUDE_DIRS}
    )
target_link_libraries(deadlinescheduler ${Boost_LIBRARIES} -pthread rt)

add_test(NAME deadlinescheduler-100ms-run-time COMMAND deadlinescheduler 100)
add_test(NAME deadlinescheduler-300ms-run-time COMMAND deadlinescheduler 300)
add_test(NAME deadlinescheduler-500ms-run-time COMMAND deadlinescheduler 500)
add_test(NAME deadlinescheduler-1000ms-run-time COMMAND deadlinescheduler 1000)
add_test(NAME deadlinescheduler-1500ms-run-time COMMAND deadlinescheduler 1500)
add_test(NAME deadlinescheduler-3000ms-run-time COMMAND deadlinescheduler 3000)
add_test(NAME deadlinescheduler-6000ms-run-time COMMAND deadlinescheduler 6000)

set_source_files_properties(variant.cpp PROPERTIES
    COMPILE_FLAGS "-std=c++11")
add_executable(variant variant.cpp)
target_include_directories(variant
    PRIVATE $<TARGET_PROPERTY:cxx-util,INTERFACE_INCLUDE_DIRECTORIES>
    ${Boost_INCLUDE_DIRS}
    )
add_test(NAME variant COMMAND variant)
