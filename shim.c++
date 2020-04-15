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

#include "oflags.h"

namespace sandstormPreload {
  // Scratch space for system/libc calls that need a buffer for a path:
  thread_local char pathBuf[PATH_MAX];

  int openPseudo(kj::PathPtr path, int flags, mode_t mode);

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

        // If the path is under /sandstorm-magic, handle it ourselves:
        auto path = kj::Path(nullptr).eval(pathstr);
        if(path[0] == "sandstorm-magic") {
          return openPseudo(path, flags, mode);
        }

        // ...or if it's a symlink to something under /sandstorm-magic:
        memset(pathBuf, 0, PATH_MAX);
        if(readlink(pathstr, pathBuf, PATH_MAX) >= 0) {
          path = kj::Path(kj::mv(path)).eval(pathBuf);
          if(path[0] == "sandstorm-magic") {
            return openPseudo(path, flags, mode);
          }
        }

        // Otherwise, pass it through to the syscall:
        return real::open(pathstr, flags, mode);
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

  int openPseudo(kj::PathPtr path, int flags, mode_t mode) {
    int err = 0;
    kj::Maybe<kj::Own<PseudoFile>> result;
    Node::Client node(nullptr);

    vfs.getInjector().runInLoop([&](EventLoopData& data) -> kj::Promise<void> {
      // TODO: we should be more thoughtful about what errno values we return
      // when things fail. For example, we should always look at the exception type
      // to inform the decision: disconnected errors should retrun EIO, while
      // unimplemented means a permission error or trying to walk() on a non-directory
      // or something.

      // Walk down the directory tree from the root until we hit our target.
      // We start at index 1 to drop the /sandstorm-magic prefix.
      auto dir = data.getRootDir();
      node = dir;
      kj::Maybe<const kj::String&> basename;
      for(size_t i = 1; i < path.size(); i++) {
        dir = node.castAs<RwDirectory>();
        auto req = dir.walkRequest();
        req.setName(path[i]);
        basename = path[i];
        node = req.send().getNode();
      }

      // Check if the node is there:
      return node.statRequest().send().then([&](auto res) -> kj::Promise<void> {
        // It is! check the permissions, returning an error if needbe,
        // otherwise just return the file.
        auto info = res.getInfo();
        if(OFLAG_ACCESS(flags) != O_RDONLY && !info.getWritable()) {
          err = EPERM;
          return kj::READY_NOW;
        }
        result = kj::heap<CapnpFile>(node, flags, info);
        return kj::READY_NOW;

      }, [&](kj::Exception&&) -> kj::Promise<void> {
        // Couldn't stat the file. If we weren't asked to create it,
        // then this is fatal; return an error:
        if(!(flags & O_CREAT)) {
          err = ENOENT;
          return kj::READY_NOW;
        }

        // Maybe the file doesn't exist; try creating it:
        KJ_IF_MAYBE(name, basename) {
          // TODO: check if we should be making a directory instead.
          auto req = dir.createRequest();
          req.setName(*name);
          bool writable = mode & 0200;
          bool executable = mode & 0100;
          req.setExecutable(executable);
          return req.send().then([&result, writable, executable, flags](auto res) -> kj::Promise<void> {
            capnp::MallocMessageBuilder msg;
            auto info = msg.initRoot<StatInfo>();
            info.setExecutable(executable);
            info.setWritable(writable);
            info.initFile();

            Node::Client node = res.getFile();
            result = kj::heap<CapnpFile>(node, flags, info);
            return kj::READY_NOW;
          },[&err](kj::Exception&&) -> kj::Promise<void> {
            // Creating failed. We assume a permission error for now. TODO:
            // look at the exception and be a little smarter if we can.
            err = EPERM;
            return kj::READY_NOW;
          });
        } else {
          // If basename is null, then was an attempt to open() the root.
          // Odd that it failed.
          err = EPERM;
          return kj::READY_NOW;
        }
      });
    });

    errno = err;
    KJ_IF_MAYBE(file, result) {
      int fd = allocFd();
      vfs.addFile(fd, kj::mv(*file));
      return fd;
    } else {
      return -1;
    }
  }
}; // namespace sandstormPreload
