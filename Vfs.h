// vim: set ft=cpp :
#pragma once

#include <map>

#include <kj/memory.h>
#include <kj/mutex.h>

#include "PseudoFile.h"
#include "EventInjector.h"

namespace sandstormPreload {
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
};
