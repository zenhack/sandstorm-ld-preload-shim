#include "EventLoopData.h"

#include <kj/debug.h>
#include <capnp/rpc.h>
#include <capnp/rpc-twoparty.h>

namespace sandstormPreload {
  EventLoopData::EventLoopData(kj::AsyncIoContext& context, kj::StringPtr serverAddr) {
    rootDir = context.provider->getNetwork()
      .parseAddress(serverAddr)
      .then([](auto addr) -> auto {
          return addr->connect();
      })
      .then([](auto stream) -> auto {
          capnp::TwoPartyVatNetwork network(
            *stream,
            capnp::rpc::twoparty::Side::CLIENT
          );
          auto rpcSystem = capnp::makeRpcClient(network);
          capnp::MallocMessageBuilder message;
          auto vatId = message.initRoot<capnp::rpc::twoparty::VatId>();
          vatId.setSide(capnp::rpc::twoparty::Side::SERVER);
          return rpcSystem.bootstrap(vatId).castAs<RwDirectory>();
      })
      .fork();
  }

  kj::Promise<RwDirectory::Client> EventLoopData::getRootDir() {
    KJ_IF_MAYBE(promise, rootDir) {
      return promise->addBranch();
    } else {
      KJ_FAIL_ASSERT("rootDir is null");
    }
  }
}
