##############################################################################
# Tests

#add_executable(callback callback.cpp)
#set_target_properties(callback PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
#target_link_libraries(callback PRIVATE cxx-util)
#add_test(NAME callback COMMAND callback)
#
#if(NOT MSVC)
#    add_executable(variant variant.cpp)
#    set_target_properties(variant PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
#    target_link_libraries(variant PRIVATE cxx-util)
#    add_test(NAME variant COMMAND variant)
#endif()
#
#add_executable(composed composed.cpp)
#set_target_properties(composed PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
#target_link_libraries(composed PRIVATE cxx-util)
#add_test(NAME composed COMMAND composed)
#
#add_executable(producerconsumer producerconsumer.cpp)
#set_target_properties(producerconsumer PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
#target_link_libraries(producerconsumer PRIVATE cxx-util)
#add_test(NAME producerconsumer COMMAND producerconsumer)
#
#add_executable(version version.cpp)
#set_target_properties(version PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
#target_link_libraries(version PRIVATE cxx-util)
#add_test(NAME version COMMAND version)

set(testSources callback.cpp producerconsumer.cpp version.cpp)
# TODO composed.cpp
if(NOT MSVC)
    list(APPEND testSources variant.cpp)
endif()
add_executable(util-test main.cpp ${testSources})
set_target_properties(util-test PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
target_link_libraries(util-test PRIVATE cxx-util)
add_test(NAME util-test COMMAND util-test)
