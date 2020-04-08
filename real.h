// vim: set ft=cpp :
#pragma once

#include <stddef.h>
#include <sys/types.h>

namespace sandstormPreload {
  namespace real {
    // The "real" versions of the libc functions that we intercept.

    typedef int (*close_ftype)(int);
    typedef ssize_t (*read_ftype)(int, void *, size_t);
    typedef ssize_t (*write_ftype)(int, const void *, size_t);
    typedef int (*open_ftype)(const char *, int, ...);

    extern close_ftype close;
    extern read_ftype read;
    extern write_ftype write;
    extern open_ftype open;

  }; // namespace real
};
