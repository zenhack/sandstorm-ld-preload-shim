#include "real.h"
#include <dlfcn.h>

namespace sandstormPreload {
  namespace real {
    close_ftype close = (close_ftype)dlsym(RTLD_NEXT, "close");
    read_ftype read = (read_ftype)dlsym(RTLD_NEXT, "read");
    write_ftype write = (write_ftype)dlsym(RTLD_NEXT, "write");
    open_ftype open = (open_ftype)dlsym(RTLD_NEXT, "open");
  };
};
