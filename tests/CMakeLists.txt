project(cxx-util-tests LANGUAGES C CXX)

##############################################################################
# Tests

#add_executable(composed composed.cpp)
#set_target_properties(composed PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
#target_link_libraries(composed PRIVATE cxx-util)
#add_test(NAME composed COMMAND composed)

find_package(Nanopb REQUIRED)

nanopb_generate_cpp(rpcTestPbSources rpcTestPbHeaders rpc-test.proto)

set(rpcTestPbHeaderDirs)
foreach(header ${rpcTestPbHeaders})
    get_filename_component(dir ${header} DIRECTORY)
    list(APPEND rpcTestPbHeaderDirs ${dir})
endforeach()

add_executable(rpc-test rpc-test.cpp ${rpcTestPbSources})
set_target_properties(rpc-test PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
target_link_libraries(rpc-test PRIVATE cxx-util)
target_include_directories(rpc-test PRIVATE ${NANOPB_INCLUDE_DIRS} ${rpcTestPbHeaderDirs})
add_test(NAME rpc-test COMMAND rpc-test)

set(testSources
    op.cpp
    callback.cpp
    producerconsumer.cpp
    version.cpp
    asio-ws.cpp
)

# TODO composed.cpp
if(NOT MSVC)
    list(APPEND testSources variant.cpp)
endif()
add_executable(util-test main.cpp ${testSources})
set_target_properties(util-test PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
target_link_libraries(util-test PRIVATE cxx-util)
add_test(NAME util-test COMMAND util-test)
