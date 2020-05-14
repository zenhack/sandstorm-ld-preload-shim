use std::sync::Arc;

pub struct FsNode(Arc<filesystem_capnp::Node::Client>);

impl vfs::Fd for FsNode {
    fn read(&self, buf: &mut [u8]) -> Result<isize> {
        panic!("TODO");
    }

    fn write(&self, buf: &[u8]) -> Result<isize> {
        panic!("TODO");
    }
}
