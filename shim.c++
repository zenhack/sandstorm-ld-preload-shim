#include <dlfcn.h>
#include <unistd.h>
#include <map>
#include <kj/debug.h>
#include <kj/mutex.h>

namespace sandstormPreload {
  class PseudoFile {
  public:
    virtual ssize_t read(void *buf, size_t count) = 0;
    virtual ssize_t write(const void *buf, size_t count) = 0;
  };

  class Globals {
  public:
    kj::Maybe<PseudoFile&> getFile(int fd);
    int closeFd(int fd);
  private:
    kj::MutexGuarded<std::map<int, kj::Own<PseudoFile>>> fdTable;
  };

  static Globals globals;

  int allocFd();

  namespace real {
    // Wrappers around the "real" versions of the libc functions that we
    // intercept.

    typedef int (*close_ftype)(int);
    typedef ssize_t (*read_ftype)(int, void *, size_t);
    typedef ssize_t (*write_ftype)(int, const void *, size_t);

    int close(int fd) noexcept {
      close_ftype fn = (close_ftype)dlsym(RTLD_NEXT, "close");
      return fn(fd);
    }

    ssize_t read(int fd, void *buf, size_t count) noexcept {
      read_ftype fn = (read_ftype)dlsym(RTLD_NEXT, "read");
      return fn(fd, buf, count);
    }

    ssize_t write(int fd, const void *buf, size_t count) noexcept {
      write_ftype fn = (write_ftype)dlsym(RTLD_NEXT, "write");
      return fn(fd, buf, count);
    }
  } // namespace real

  namespace wrappers {
    // The LD_PRELOAD wrappers themselves.

    extern "C" {
      int close(int fd) noexcept {
        return globals.closeFd(fd);
      }

      ssize_t read(int fd, void *buf, size_t count) noexcept {
        KJ_IF_MAYBE(file, globals.getFile(fd)) {
          return file->read(buf, count);
        } else {
          return real::read(fd, buf, count);
        }
      }

      ssize_t write(int fd, const void *buf, size_t count) noexcept {
        KJ_IF_MAYBE(file, globals.getFile(fd)) {
          return file->write(buf, count);
        } else {
          return real::write(fd, buf, count);
        }
      }
    };
  }; // namespace wrappers

  kj::Maybe<PseudoFile&> Globals::getFile(int fd) {
    auto tbl = fdTable.lockExclusive();
    auto it = tbl->find(fd);
    if(it == tbl->end()) {
      return nullptr;
    }
    return *it->second;
  }

  int Globals::closeFd(int fd) {
    {
      auto tbl = fdTable.lockExclusive();
      tbl->erase(fd);
    }
    return real::close(fd);
  }

  int allocFd() {
    // Allocate a file descriptor. Even for spoofed files, we still allocate
    // a real file descriptor, so we can be sure to avoid namespace
    // collisions with fds handed out by the OS.

    // Create a pipe, close one end of it, use the other as our fd:
    int pipefds[2];
    KJ_SYSCALL(pipe(pipefds));
    real::close(pipefds[1]);
    return pipefds[0];
  }
}; // namespace sandstormPreload
