// vim: set ft=cpp :
#pragma once

#include "PseudoFile.h"
#include "filesystem.capnp.h"

namespace sandstormPreload {
  class CapnpFile : public PseudoFile {
  public:
    CapnpFile(Node::Client& node);
    virtual ssize_t read(void *buf, size_t count);
    virtual ssize_t write(const void *buf, size_t count);
  private:
    Node::Client node;
  };
};
