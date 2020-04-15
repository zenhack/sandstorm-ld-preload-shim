#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "CapnpFile.h"
#include "Vfs.h"

#include "oflags.h"


namespace sandstormPreload {
  CapnpFile::CapnpFile(Node::Client& node, int oflags, StatInfo::Reader statInfo)
    : node(node),
    oflags(oflags),
    offset(0),
    executable(statInfo.getExecutable()),
    writable(statInfo.getWritable()),
    isDir(statInfo.which() == StatInfo::DIR) {}

  ssize_t CapnpFile::read(void *, size_t) {
    errno = ENOSYS;
    return -1;
  }

  ssize_t CapnpFile::write(const void *buf, size_t size) {
    if(OFLAG_ACCESS(oflags) == O_RDONLY || !writable) {
      errno = EPERM;
      return -1;
    }

    if(isDir) {
      // Emperically, this is what happens when you
      // open a directory and then try to write().
      errno = EBADF;
      return -1;
    }

    int err = 0;
    ssize_t result = (ssize_t)size;

    vfs.getInjector().runInLoop([&](auto) -> kj::Promise<void> {
      RwFile::Client file(node.castAs<RwFile>());

      auto fileWriteReq = file.writeRequest();
      fileWriteReq.setStartAt(offset);
      auto sink = fileWriteReq.send().getSink();

      auto expectSizeReq = sink.expectSizeRequest();
      expectSizeReq.setSize(size);
      auto expectSizeRes = expectSizeReq.send();

      auto streamWriteReq = sink.writeRequest();
      auto byteBuf = static_cast<const uint8_t *>(buf);
      streamWriteReq.setData(capnp::Data::Reader(byteBuf, size));
      auto streamWriteRes = streamWriteReq.send();

      auto doneRes = sink.doneRequest().send();

      return doneRes
        .ignoreResult()
        .catch_([&err, &result](kj::Exception&& e) -> kj::Promise<void> {
            KJ_LOG(ERROR, e);
          err = EIO;
          result = -1;
          return kj::READY_NOW;
        });
    });
    errno = err;
    return result;
  }
};
