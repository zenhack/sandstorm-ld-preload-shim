// vim: set ft=cpp :
#pragma once

#include <sys/types.h>
#include <stddef.h>

namespace sandstormPreload {
  class PseudoFile {
  public:
    virtual ssize_t read(void *buf, size_t count) = 0;
    virtual ssize_t write(const void *buf, size_t count) = 0;
  };
};
