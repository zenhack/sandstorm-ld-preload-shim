@0xfa27741246fb0c93;

using FileSystem = import "filesystem.capnp";

interface Bootstrap {
  rootfs @0 () -> (dir :FileSystem.RwDirectory);
}
