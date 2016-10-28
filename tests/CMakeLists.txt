##############################################################################
# Tests

#add_executable(composed composed.cpp)
#set_target_properties(composed PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
#target_link_libraries(composed PRIVATE cxx-util)
#add_test(NAME composed COMMAND composed)

set(testSources callback.cpp producerconsumer.cpp version.cpp asio-ws.cpp)
# TODO composed.cpp
if(NOT MSVC)
    list(APPEND testSources variant.cpp)
endif()
add_executable(util-test main.cpp ${testSources})
set_target_properties(util-test PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
target_link_libraries(util-test PRIVATE cxx-util)
add_test(NAME util-test COMMAND util-test)
