set_source_files_properties(callback.cpp PROPERTIES COMPILE_FLAGS -std=c++11)
add_executable(callback callback.cpp)
target_include_directories(callback
    PRIVATE $<TARGET_PROPERTY:cxx-util,INCLUDE_DIRECTORIES>
    )

add_test(NAME callback COMMAND callback)
