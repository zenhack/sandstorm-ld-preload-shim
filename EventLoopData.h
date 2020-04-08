// vim: set ft=cpp :
#pragma once

#include <kj/async.h>
#include <kj/async-io.h>
#include <kj/string.h>
#include "filesystem.capnp.h"

namespace sandstormPreload {
  class EventLoopData {
    public:
      EventLoopData(kj::AsyncIoContext& context, kj::StringPtr serverAddr);
      kj::Promise<RwDirectory::Client> getRootDir();

    private:
      kj::Maybe<kj::ForkedPromise<RwDirectory::Client>> rootDir;
  };
};
