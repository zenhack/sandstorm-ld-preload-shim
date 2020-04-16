// vim: set ft=cpp :
#pragma once

#include "PseudoFile.h"
#include "filesystem.capnp.h"

namespace sandstormPreload {
  class CapnpFile final : public PseudoFile {
  public:
    CapnpFile(Node::Client& node, int oflags, StatInfo::Reader statInfo);
    virtual ssize_t read(void *buf, size_t count);
    virtual ssize_t write(const void *buf, size_t count);
  private:
    Node::Client node;
    int oflags;
    uint64_t offset;
    bool executable, writable, isDir;
  };
};
