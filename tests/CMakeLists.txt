##############################################################################
# Tests

set_source_files_properties(callback.cpp PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
add_executable(callback callback.cpp)
target_link_libraries(callback PRIVATE cxx-util)
add_test(NAME callback COMMAND callback)

if(NOT MSVC)
    set_source_files_properties(variant.cpp PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
    add_executable(variant variant.cpp)
    target_link_libraries(variant PRIVATE cxx-util)
    add_test(NAME variant COMMAND variant)
endif()

set_source_files_properties(composedop.cpp PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON)
add_executable(composedop composedop.cpp)
target_link_libraries(composedop PRIVATE cxx-util)
add_test(NAME composedop COMMAND composedop)