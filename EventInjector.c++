#include <mutex>
#include <thread>

#include <unistd.h>
#include <fcntl.h>

#include <kj/debug.h>
#include <kj/mutex.h>
#include <kj/async-unix.h>

#include "EventLoopData.h"
#include "EventInjector.h"

namespace sandstormPreload {
  static const char *server_addr_var = "SANDSTORM_VFS_SERVER";

  EventInjector::EventInjector() {
    int pipefds[2];
    KJ_SYSCALL(pipe2(pipefds, O_CLOEXEC));
    *injectFd.lockExclusive() = kj::AutoCloseFd(pipefds[1]);
    handleFd = kj::AutoCloseFd(pipefds[0]);

    loopThread = std::thread([this]() {
      auto context = kj::setupAsyncIo();
      char *vfs_addr = getenv(server_addr_var);
      KJ_ASSERT(
          vfs_addr != nullptr,
          kj::str("environment variable ", server_addr_var, " undefined.")
      );
      this->initData = kj::heap<EventLoopData>(context, vfs_addr);
      kj::UnixEventPort::FdObserver observer(
          context.unixEventPort,
          handleFd.get(),
          kj::UnixEventPort::FdObserver::Flags::OBSERVE_READ
      );
      this->acceptJobs(context, observer).wait(context.waitScope);
    });
  }

  kj::Promise<void> EventInjector::acceptJobs(
      kj::AsyncIoContext& context,
      kj::UnixEventPort::FdObserver& observer) {
    return observer.whenBecomesReadable().then([this, &context, &observer]() {
      ssize_t countRead;
      PromiseMaker *pm;
      KJ_SYSCALL(countRead = real::read(handleFd.get(), &pm, sizeof pm));
      // FIXME: handle this correctly:
      KJ_ASSERT(countRead == sizeof pm, "Short read on handleFd");
      pm->makePromise(*this->initData).detach([](kj::Exception&& e) {
          KJ_LOG(ERROR, kj::str(
                "Exception thrown from EventInjector::runInLoop(): ",
                e));
          std::terminate();
      });
      return acceptJobs(context, observer);
    });
  }
};
