WIP LD_PRELOAD library for working with the [sandstorm-filesystem][1]
protocol.

The idea is that to allow programs to access a capnp filesystem simply
by accessing `/sandstorm-magic`; the library will intercept calls to the
relevant libc functions and redirect filesystem operations in this path
to a server offering the filesystem protocol.

# Building

Make sure you have `capnproto` installed on the system, check out the
sandstorm source code somewhere and from the root of this repo run:

```
git submodule init
git submodule update
export SANDSTORM_PATH=/path/to/sandstorm/repo
make
```

This will generate `sandstorm-preload.so`

# Usage

```
export LD_PRELOAD=/path/to/sandstorm-preload.so`
export SANDSTORM_VFS_SERVER=<address of server>
cat /sandstorm-magic/foo # or some other command
```

[1]: https://github.com/zenhack/sandstorm-filesystem
