#include <stdarg.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/limits.h>

#include <map>
#include <mutex>
#include <thread>

#include <kj/async.h>
#include <kj/async-io.h>
#include <kj/async-unix.h>
#include <kj/debug.h>
#include <kj/filesystem.h>
#include <kj/mutex.h>

namespace sandstormPreload {
  class PseudoFile {
  public:
    virtual ssize_t read(void *buf, size_t count) = 0;
    virtual ssize_t write(const void *buf, size_t count) = 0;
  };

  // Scratch space for system/libc calls that need a buffer for a path:
  thread_local char pathBuf[PATH_MAX];

  class EventInjector {
    // An EventInjector is used to inject events into an event loop running in
    // a separate thread. The thread is spawned from within the constructor.

  public:
    EventInjector();

    template<class Func>
    void runInLoop(Func& f);
    // Run `f`, which should be a functor (typically a lambda) which takes no
    // arguments and returns a `kj::Promise<void>`, in a separate thread.
    // Then, wait for the promise it returns.
  private:
    class PromiseMaker {
      public:
        virtual kj::Promise<void> makePromise() = 0;
    };
    template<class Func>
    class FnPromiseMaker : public PromiseMaker {
      public:
        FnPromiseMaker(Func& fn): fn(fn) {}

        virtual kj::Promise<void> makePromise() override {
          return fn();
        }
      private:
        Func& fn;
    };

    kj::Maybe<std::thread> loopThread;

    // To be used only outside the event loop thread:
    kj::MutexGuarded<kj::AutoCloseFd> injectFd;

    // To be used only inside the event loop thread:
    kj::AutoCloseFd handleFd;

    kj::Promise<void> acceptJobs(
        kj::AsyncIoContext& context,
        kj::UnixEventPort::FdObserver& observer);
  };

  class Vfs {
  public:
    Vfs();
    kj::Maybe<PseudoFile&> getFile(int fd);
    int closeFd(int fd);
    EventInjector& getInjector();
  private:
    kj::MutexGuarded<std::map<int, kj::Own<PseudoFile>>> fdTable;
    kj::Lazy<EventInjector> injector;
  };

  static Vfs vfs;

  int allocFd();

  namespace real {
    // The "real" versions of the libc functions that we intercept.

    typedef int (*close_ftype)(int);
    close_ftype close = (close_ftype)dlsym(RTLD_NEXT, "close");

    typedef ssize_t (*read_ftype)(int, void *, size_t);
    read_ftype read = (read_ftype)dlsym(RTLD_NEXT, "read");

    typedef ssize_t (*write_ftype)(int, const void *, size_t);
    write_ftype write = (write_ftype)dlsym(RTLD_NEXT, "write");

    typedef int (*open_ftype)(const char *, int, ...);
    open_ftype open = (open_ftype)dlsym(RTLD_NEXT, "open");

  }; // namespace real

  namespace wrappers {
    // The LD_PRELOAD wrappers themselves.

    extern "C" {
      int open(const char *pathstr, int flags, ...) {
        // from open(2):
        //
        // > The mode argument specifies the file mode bits be applied when
        // > a new file is created. This argument must be supplied when O_CREAT
        // > or O_TMPFILE is specified in flags; if neither O_CREAT nor O_TMPFILE
        // > is specified, then mode is ignored.
        //
        // We use those flags to work out whether we were called with a third
        // argument or not.
        mode_t mode = 0;
        if(flags & (O_CREAT | O_TMPFILE)) {
          va_list args;
          va_start(args, flags);
          mode = va_arg(args, mode_t);
          va_end(args);
        }
        realpath(pathstr, pathBuf);
        auto path = kj::Path(nullptr).eval(pathBuf);
        if(path[0] != "sandstorm-magic") {
          return real::open(pathstr, flags, mode);
        }

        // TODO: actually do something.
        errno = EPERM;
        return -1;
      }

      int close(int fd) noexcept {
        return vfs.closeFd(fd);
      }

      ssize_t read(int fd, void *buf, size_t count) noexcept {
        KJ_IF_MAYBE(file, vfs.getFile(fd)) {
          return file->read(buf, count);
        } else {
          return real::read(fd, buf, count);
        }
      }

      ssize_t write(int fd, const void *buf, size_t count) noexcept {
        KJ_IF_MAYBE(file, vfs.getFile(fd)) {
          return file->write(buf, count);
        } else {
          return real::write(fd, buf, count);
        }
      }
    };
  }; // namespace wrappers

  EventInjector& Vfs::getInjector() {
    return injector.get([](kj::SpaceFor<EventInjector>& space) -> kj::Own<EventInjector> {
      return space.construct();
    });
  }

  kj::Maybe<PseudoFile&> Vfs::getFile(int fd) {
    auto tbl = fdTable.lockExclusive();
    auto it = tbl->find(fd);
    if(it == tbl->end()) {
      return nullptr;
    }
    return *it->second;
  }

  int Vfs::closeFd(int fd) {
    {
      auto tbl = fdTable.lockExclusive();
      tbl->erase(fd);
    }
    return real::close(fd);
  }

  EventInjector::EventInjector() {
    int pipefds[2];
    KJ_SYSCALL(pipe2(pipefds, O_CLOEXEC));
    *injectFd.lockExclusive() = kj::AutoCloseFd(pipefds[1]);
    handleFd = kj::AutoCloseFd(pipefds[0]);

    loopThread = std::thread([this]() {
      auto context = kj::setupAsyncIo();
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
      pm->makePromise().detach([](kj::Exception&& e) {
          KJ_LOG(ERROR, kj::str(
                "Exception thrown from EventInjector::runInLoop(): ",
                e));
          std::terminate();
      });
      return acceptJobs(context, observer);
    });
  }

  template<class Func>
  void EventInjector::runInLoop(Func &f) {
    std::mutex returnLock;
    auto fThenUnlock = [&]() {
      return f().then([&]() {
        returnLock.unlock();
        return kj::READY_NOW;
      });
    };

    auto pm = FnPromiseMaker<decltype(fThenUnlock)>(fThenUnlock);
    ssize_t written;

    returnLock.lock();
    auto fd = injectFd.lockExclusive();
    KJ_SYSCALL(written = real::write(fd.get(), &pm, sizeof pm));
    // FIXME: handle this correctly.
    KJ_ASSERT(written == sizeof pm, "Short write during inject");

    // The event loop thread will unlock this when the promise completes:
    returnLock.lock();
  }

  int allocFd() {
    // Allocate a file descriptor. Even for spoofed files, we still allocate
    // a real file descriptor, so we can be sure to avoid namespace
    // collisions with fds handed out by the OS.

    // Create a pipe, close one end of it, use the other as our fd:
    int pipefds[2];
    KJ_SYSCALL(pipe2(pipefds, O_CLOEXEC));
    real::close(pipefds[1]);
    return pipefds[0];
  }
}; // namespace sandstormPreload
