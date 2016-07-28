##############################################################################
# Tests

set_source_files_properties(callback.cpp PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
add_executable(callback callback.cpp)
set_target_properties(callback PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
target_link_libraries(callback PRIVATE cxx-util)
add_test(NAME callback COMMAND callback)

if(NOT MSVC)
    add_executable(variant variant.cpp)
    set_target_properties(variant PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(variant PRIVATE cxx-util)
    add_test(NAME variant COMMAND variant)
endif()

set_source_files_properties(composed.cpp PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
add_executable(composed composed.cpp)
set_target_properties(composed PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
target_link_libraries(composed PRIVATE cxx-util)
add_test(NAME composed COMMAND composed)

set_source_files_properties(producerconsumer.cpp PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
add_executable(producerconsumer producerconsumer.cpp)
set_target_properties(producerconsumer PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
target_link_libraries(producerconsumer PRIVATE cxx-util)
add_test(NAME producerconsumer COMMAND producerconsumer)
