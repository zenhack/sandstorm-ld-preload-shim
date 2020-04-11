#include <mutex>
#include <thread>

#include <unistd.h>
#include <fcntl.h>

#include <kj/debug.h>
#include <kj/mutex.h>
#include <kj/async-unix.h>

#include <capnp/rpc-twoparty.h>

#include "EventLoopData.h"
#include "EventInjector.h"

namespace sandstormPreload {
  static const char *server_addr_var = "SANDSTORM_VFS_SERVER";

  static kj::Promise<void> acceptJobs(
      EventLoopData& data,
      kj::AutoCloseFd& handleFd,
      kj::UnixEventPort::FdObserver& observer) {
    return observer.whenBecomesReadable().then([&]() {
      ssize_t countRead;
      EventInjector::PromiseMaker *pm;
      KJ_SYSCALL(countRead = real::read(handleFd.get(), &pm, sizeof pm));
      // FIXME: handle this correctly:
      KJ_ASSERT(countRead == sizeof pm, "Short read on handleFd");
      pm->makePromise(data).detach([](kj::Exception&& e) {
          KJ_LOG(ERROR, kj::str(
                "Exception thrown from EventInjector::runInLoop(): ",
                e));
          std::terminate();
      });
      return acceptJobs(data, handleFd, observer);
    });
  }

  EventInjector::EventInjector() {
    int pipefds[2];
    KJ_SYSCALL(pipe2(pipefds, O_CLOEXEC));
    *injectFd.lockExclusive() = kj::AutoCloseFd(pipefds[1]);

    std::thread([pipefds]() {
      auto handleFd = kj::AutoCloseFd(pipefds[0]);

      auto context = kj::setupAsyncIo();
      char *vfs_addr = getenv(server_addr_var);
      KJ_ASSERT(
          vfs_addr != nullptr,
          kj::str("environment variable ", server_addr_var, " undefined.")
      );
      kj::UnixEventPort::FdObserver observer(
          context.unixEventPort,
          handleFd.get(),
          kj::UnixEventPort::FdObserver::Flags::OBSERVE_READ
      );
      auto addr = context.provider->getNetwork().parseAddress(vfs_addr).wait(context.waitScope);
      auto stream = addr->connect().wait(context.waitScope);

      capnp::TwoPartyVatNetwork network(
          *stream,
          capnp::rpc::twoparty::Side::CLIENT
      );
      auto rpcSystem = capnp::makeRpcClient(network);
      capnp::MallocMessageBuilder message;
      auto vatId = message.initRoot<capnp::rpc::twoparty::VatId>();
      vatId.setSide(capnp::rpc::twoparty::Side::SERVER);
      auto rootDir = rpcSystem.bootstrap(vatId).castAs<RwDirectory>();
      auto data = EventLoopData(rootDir);

      acceptJobs(data, handleFd, observer).wait(context.waitScope);
    }).detach();
  }
};
