#include "EventLoopData.h"

#include <kj/debug.h>
#include <capnp/rpc.h>
#include <capnp/rpc-twoparty.h>

namespace sandstormPreload {
  EventLoopData::EventLoopData(RwDirectory::Client rootDir)
    : rootDir(rootDir)
  {}

  RwDirectory::Client EventLoopData::getRootDir() {
    return rootDir;
  }
}
