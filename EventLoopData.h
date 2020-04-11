// vim: set ft=cpp :
#pragma once

#include <kj/async.h>
#include <kj/async-io.h>
#include <kj/string.h>
#include "filesystem.capnp.h"

namespace sandstormPreload {
  class EventLoopData {
    public:
      EventLoopData(RwDirectory::Client rootDir);
      RwDirectory::Client getRootDir();
    private:
      RwDirectory::Client rootDir;
  };
};
