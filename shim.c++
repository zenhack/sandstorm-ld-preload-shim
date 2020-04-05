#include <stdarg.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <map>
#include <kj/filesystem.h>
#include <kj/debug.h>
#include <kj/mutex.h>

namespace sandstormPreload {
  class PseudoFile {
  public:
    virtual ssize_t read(void *buf, size_t count) = 0;
    virtual ssize_t write(const void *buf, size_t count) = 0;
  };

  // Scratch space for system/libc calls that need a buffer for a path:
  thread_local char pathBuf[PATH_MAX];

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
        kj::Path path = nullptr;
        path = kj::mv(path).eval(pathBuf);
        if(path[0] != "sandstorm-magic") {
          return real::open(pathstr, flags, mode);
        }

        // TODO: actually do something.
        errno = EPERM;
        return -1;
      }

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
