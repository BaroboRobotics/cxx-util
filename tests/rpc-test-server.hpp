#ifndef RPC_TEST_SERVER_HPP
#define RPC_TEST_SERVER_HPP

#include <rpc-test.pb.hpp>
#include <pb_asio.hpp>

#include <util/log.hpp>

struct TestServer {
    float propertyValue = 1.0;

    util::log::Logger lg;

    // ===================================================================================
    // RPC request implementations

    rpc_test_GetProperty_Out operator()(const rpc_test_GetProperty_In& in);
    rpc_test_SetProperty_Out operator()(const rpc_test_SetProperty_In& in);

    // ===================================================================================
    // Event messages

    void operator()(const rpc_test_RpcRequest& rpcRequest);
    void operator()(const rpc_test_Quux&);
};

inline rpc_test_GetProperty_Out TestServer::operator()(const rpc_test_GetProperty_In& in) {
    BOOST_LOG(lg) << "Server received a GetProperty RPC request";
    return {true, propertyValue};
}

inline rpc_test_SetProperty_Out TestServer::operator()(const rpc_test_SetProperty_In& in) {
    if (in.has_value) {
        propertyValue = in.value;
    }
    else {
        BOOST_LOG(lg) << "Server received an invalid SetProperty RPC request";
    }
    return {};
}

inline void TestServer::operator()(const rpc_test_RpcRequest& rpcRequest) {
    auto serverToClient = rpc_test_ServerToClient{};
    serverToClient.arg.rpcReply.has_requestId = rpcRequest.has_requestId;
    serverToClient.arg.rpcReply.requestId = rpcRequest.requestId;

    auto visitor = [&serverToClient, this](const auto& req) {
        nanopb::assign(serverToClient.arg.rpcReply.arg, (*this)(req));
        nanopb::assign(serverToClient.arg, serverToClient.arg.rpcReply);
    };
    if (nanopb::visit(visitor, rpcRequest.arg)) {
        //static_assert(false, "TODO: send serverToClient back to client");
        BOOST_LOG(lg) << "Server received and replied to an RPC request";
    }
    else {
        BOOST_LOG(lg) << "Server received an unrecognized RPC request";
    }
}

inline void TestServer::operator()(const rpc_test_Quux&) {
    BOOST_LOG(lg) << "Server received a Quux event";
}

#endif
