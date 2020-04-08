#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/limits.h>

#include <map>

#include <kj/async.h>
#include <kj/async-io.h>
#include <kj/async-unix.h>
#include <kj/debug.h>
#include <kj/filesystem.h>
#include <kj/mutex.h>

#include <capnp/rpc-twoparty.h>

#include "filesystem.capnp.h"

#include "real.h"
#include "EventLoopData.h"
#include "EventInjector.h"
#include "PseudoFile.h"
#include "CapnpFile.h"
#include "Vfs.h"

namespace sandstormPreload {
  // Scratch space for system/libc calls that need a buffer for a path:
  thread_local char pathBuf[PATH_MAX];

  static Vfs vfs;

  int allocFd();

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
