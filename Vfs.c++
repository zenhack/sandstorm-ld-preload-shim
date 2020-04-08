
#include <fcntl.h>
#include <unistd.h>

#include "Vfs.h"
#include "real.h"

namespace sandstormPreload {
  EventInjector& Vfs::getInjector() {
    return injector.get([](kj::SpaceFor<EventInjector>& space) -> auto {
      return space.construct();
    });
  }

  kj::Maybe<PseudoFile&> Vfs::getFile(int fd) {
    auto tbl = fdTable.lockExclusive();
    auto it = tbl->find(fd);
    if(it == tbl->end()) {
      return nullptr;
    }
    return *it->second;
  }

  void Vfs::addFile(int fd, kj::Own<PseudoFile>&& file) {
    auto tbl = fdTable.lockExclusive();
    tbl->insert(std::pair<int, kj::Own<PseudoFile>>(fd, kj::mv(file)));
  }

  int Vfs::closeFd(int fd) {
    {
      auto tbl = fdTable.lockExclusive();
      tbl->erase(fd);
    }
    return real::close(fd);
  }

  Vfs vfs;

  int allocFd() {
    // Create a pipe, close one end of it, use the other as our fd:
    int pipefds[2];
    KJ_SYSCALL(pipe2(pipefds, O_CLOEXEC));
    real::close(pipefds[1]);
    return pipefds[0];
  }
};
