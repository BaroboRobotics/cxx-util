#ifndef RPC_TEST_CLIENT_HPP
#define RPC_TEST_CLIENT_HPP

#include <rpc-test.pb.hpp>
#include <pb_asio.hpp>

#include <util/log.hpp>

struct TestClient {
    //util::RpcRequestBank<rpc_test_RpcRequest, rpc_test_RpcReply> requestBank;

    util::log::Logger lg;

    // ===================================================================================
    // Event messages

    void operator()(const rpc_test_RpcReply& rpcReply);
    void operator()(const rpc_test_Quux& quux);
};

void TestClient::operator()(const rpc_test_RpcReply& rpcReply) {
    if (rpcReply.has_requestId) {
        #if 0
        if (!nanopb::visit(requestBank[rpcReply.requestId], rpcReply.arg)) {
            BOOST_LOG(lg) << "Client received an RPC reply with unexpected type";
        }
        #endif
    }
    else {
        BOOST_LOG(lg) << "Server received an RPC reply with no request ID";
    }
}

void TestClient::operator()(const rpc_test_Quux& quux) {
    BOOST_LOG(lg) << "Client received a Quux event";
}

#endif
