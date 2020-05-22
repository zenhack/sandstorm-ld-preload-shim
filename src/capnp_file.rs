use crate::{
    filesystem_capnp,
    result::Result,
    vfs,
};

pub struct FsNode(filesystem_capnp::node::Client);

impl FsNode {
    pub fn new(client: filesystem_capnp::node::Client) -> Self {
        FsNode(client)
    }
}

impl vfs::Fd for FsNode {

    fn read(&self, buf: &mut [u8]) -> Result<isize> {
        panic!("TODO");
    }

    fn write(&self, buf: &[u8]) -> Result<isize> {
        panic!("TODO");
    }
}
