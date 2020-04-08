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

#include <capnp/rpc-twoparty.h>

#include "filesystem.capnp.h"

#include "EventLoopData.h"

namespace sandstormPreload {
  class PseudoFile {
  public:
    virtual ssize_t read(void *buf, size_t count) = 0;
    virtual ssize_t write(const void *buf, size_t count) = 0;
  };

  class CapnpFile : public PseudoFile {
  public:
    CapnpFile(Node::Client& node) : node(node) {}
    virtual ssize_t read(void *buf, size_t count);
    virtual ssize_t write(const void *buf, size_t count);
  private:
    Node::Client node;
  };

  // Scratch space for system/libc calls that need a buffer for a path:
  thread_local char pathBuf[PATH_MAX];

  class EventInjector {
    // An EventInjector is used to inject events into an event loop running in
    // a separate thread. The thread is spawned from within the constructor.

  public:
    explicit EventInjector();
    // Spawn an event loop and run `f` in the event loop's thread.

    template<class Func>
    void runInLoop(Func& f);
    // Run `f` in a separate thread, and wait for the promise it retuurns.
    // `f` should be a functor (typically a lambda) which takes an argument
    // of type `EventLoopData&` and returns a `kj::Promise<void>`
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

  class Vfs {
  public:
    kj::Maybe<PseudoFile&> getFile(int fd);
    void addFile(int fd, kj::Own<PseudoFile>&& file);
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

        int err = 0;
        kj::Maybe<kj::Own<PseudoFile>> result;

        auto inLoop = [&](EventLoopData& data) -> kj::Promise<void> {
          return data.getRootDir()
            .then([&](auto dir) -> kj::Promise<void> {
                Node::Client node = dir;
                kj::Maybe<const kj::String&> basename;
                for(size_t i = 1; i < path.size(); i++) {
                  dir = node.castAs<RwDirectory>();
                  auto req = dir.walkRequest();
                  req.setName(path[i]);
                  basename = path[i];
                  node = req.send().getNode();
                }
                return node.statRequest().send().then([&](auto res) -> kj::Promise<void> {
                  auto info = res.getInfo();
                  if(!(flags & O_RDONLY) && !info.getWritable()) {
                    err = EPERM;
                    return kj::READY_NOW;
                  }
                  result = kj::heap<CapnpFile>(node);
                  return kj::READY_NOW;
                }, [&](kj::Exception&&) -> kj::Promise<void> {
                  // TODO: On errors, try to come up with more reasonable errno values.
                  if(!(flags & O_CREAT)) {
                    err = ENOENT;
                    return kj::READY_NOW;
                  }
                  // maybe the file doesn't exist; try creating it:
                  KJ_IF_MAYBE(name, basename) {
                    auto req = dir.createRequest();
                    req.setName(*name);
                    req.setExecutable(mode & 0100);
                    return req.send().then([&result](auto res) -> kj::Promise<void> {
                      Node::Client node = res.getFile();
                      result = kj::heap<CapnpFile>(node);
                      return kj::READY_NOW;
                    },[&err](kj::Exception&&) -> kj::Promise<void> {
                      err = EPERM;
                      return kj::READY_NOW;
                    });
                  }
                  // This was an attempt to open() the root. Odd that it failed.
                  err = EPERM;
                  return kj::READY_NOW;
                });

            }, [&](kj::Exception&&) -> auto {
              err = EIO;
              return kj::READY_NOW;
            });
        };
        vfs.getInjector().runInLoop(inLoop);

        errno = err;
        KJ_IF_MAYBE(file, result) {
          int fd = allocFd();
          vfs.addFile(fd, kj::mv(*file));
          return fd;
        } else {
          return -1;
        }
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
    return injector.get([](kj::SpaceFor<EventInjector>& space) -> auto {
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

  void Vfs::addFile(int fd, kj::Own<PseudoFile>&& file) {
    auto tbl = fdTable.lockExclusive();
    tbl->insert(std::pair<int, kj::Own<PseudoFile>>(fd, kj::mv(file)));
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
      this->initData = kj::heap<EventLoopData>(
          context,
          getenv("SANDSTORM_VFS_SERVER")
      );
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

  template<class Func>
  void EventInjector::runInLoop(Func& f) {
    std::mutex returnLock;
    auto fThenUnlock = [&](EventLoopData& data) -> kj::Promise<void> {
      return f(data).then([&returnLock]() -> kj::Promise<void> {
        returnLock.unlock();
        return kj::READY_NOW;
      });
    };

    auto pm = EventInjector::FnPromiseMaker<decltype(fThenUnlock)>(fThenUnlock);
    ssize_t written;

    returnLock.lock();
    auto fd = injectFd.lockExclusive();
    KJ_SYSCALL(written = real::write(fd->get(), &pm, sizeof pm));
    // FIXME: handle this correctly.
    KJ_ASSERT(written == sizeof pm, "Short write during inject");

    // The event loop thread will unlock this when the promise completes:
    returnLock.lock();
  }

  ssize_t CapnpFile::read(void *, size_t) {
    errno = ENOSYS;
    return -1;
  }

  ssize_t CapnpFile::write(const void *, size_t) {
    errno = ENOSYS;
    return -1;
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
