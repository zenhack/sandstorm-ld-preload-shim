// vim: set ft=cpp :
#pragma once

#include <mutex>
#include <thread>

#include <kj/async.h>
#include <kj/async-io.h>
#include <kj/async-unix.h>
#include <kj/debug.h>

#include "real.h"
#include "EventLoopData.h"

namespace sandstormPreload {
  class EventInjector {
    // An EventInjector is used to inject events into an event loop running in
    // a separate thread. The thread is spawned from within the constructor.

  public:
    explicit EventInjector();
    // Spawn an event loop and run `f` in the event loop's thread.

    template<class Func>
    void runInLoop(Func& f) {
      // Run `f` in a separate thread, and wait for the promise it retuurns.
      // `f` should be a functor (typically a lambda) which takes an argument
      // of type `EventLoopData&` and returns a `kj::Promise<void>`

      std::mutex returnLock;
      auto fThenUnlock = [&](EventLoopData& data) -> kj::Promise<void> {
        return f(data).then([&returnLock]() -> kj::Promise<void> {
          returnLock.unlock();
          return kj::READY_NOW;
        });
      };

      auto pm = FnPromiseMaker<decltype(fThenUnlock)>(fThenUnlock);
      PromiseMaker *pmAddr = &pm;
      ssize_t written;

      returnLock.lock();
      auto fd = injectFd.lockExclusive();
      KJ_SYSCALL(written = real::write(fd->get(), &pmAddr, sizeof pmAddr));
      // FIXME: handle this correctly.
      KJ_ASSERT(written == sizeof pmAddr, "Short write during inject");

      // The event loop thread will unlock this when the promise completes:
      returnLock.lock();
    }
  private:

    class PromiseMaker {
      public:
        virtual kj::Promise<void> makePromise(EventLoopData&) = 0;
    };
    template<class Func>
    class FnPromiseMaker : public PromiseMaker {
      public:
        FnPromiseMaker(Func& fn): fn(fn) {}

        virtual kj::Promise<void> makePromise(EventLoopData& data) override {
          return fn(data);
        }
      private:
        Func& fn;
    };

    kj::Own<EventLoopData> initData;

    kj::Maybe<std::thread> loopThread;

    // To be used only outside the event loop thread:
    kj::MutexGuarded<kj::AutoCloseFd> injectFd;

    // To be used only inside the event loop thread:
    kj::AutoCloseFd handleFd;

    kj::Promise<void> acceptJobs(
        kj::AsyncIoContext& context,
        kj::UnixEventPort::FdObserver& observer);

  };
};
