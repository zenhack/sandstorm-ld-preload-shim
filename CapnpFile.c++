#include "CapnpFile.h"

#include <errno.h>

namespace sandstormPreload {
  CapnpFile::CapnpFile(Node::Client& node) : node(node) {}

  ssize_t CapnpFile::read(void *, size_t) {
    errno = ENOSYS;
    return -1;
  }

  ssize_t CapnpFile::write(const void *, size_t) {
    errno = ENOSYS;
    return -1;
  }
};
