use libc;
use std::{
    cell::RefCell,
    collections::HashMap,
    rc::Rc,
};
use crate::{
    result::Result,
    inject,
    preload_server_capnp::bootstrap,
};

pub trait Fd {
    fn read(&self, buf: &mut [u8]) -> Result<isize>;
    fn write(&self, buf: &[u8]) -> Result<isize>;
}

#[derive(Clone)]
pub struct FdPtr(Rc<dyn Fd>);


impl FdPtr {
    // TODO: can we avoid the static lifetime? Maybe using Pin?
    pub fn new(fd: impl Fd + 'static) -> Self {
        FdPtr(Rc::new(fd))
    }
}

impl Fd for FdPtr {
    fn read(&self, buf: &mut [u8]) -> Result<isize> {
        self.0.read(buf)
    }

    fn write(&self, buf: &[u8]) -> Result<isize> {
        self.0.write(buf)
    }
}

pub struct FdTable {
    fds: RefCell<HashMap<libc::c_int, FdPtr>>,
}

impl FdTable {
    fn new() -> Self {
        FdTable{
            fds: RefCell::new(HashMap::new()),
        }
    }

    pub fn get(&self, fd: libc::c_int) -> Option<FdPtr> {
        self.fds.borrow().get(&fd).map(|v| v.clone())
    }

    pub fn remove(&self, fd: libc::c_int) -> Option<FdPtr> {
        self.fds.borrow_mut().remove(&fd)
    }

    pub fn add(&self, fdp: FdPtr) -> (libc::c_int, libc::c_int) {
        let fd = new_fd();
        if fd < 0 {
            return (fd, unsafe { *libc::__errno_location() })
        }
        self.fds.borrow_mut().insert(fd, fdp);
        (fd, 0)
    }
}

thread_local! {
    static FD_TABLE: &'static FdTable = Box::leak(Box::new(FdTable::new()));
}

pub fn with_fds_and_bootstrap<F>(func: impl Send + 'static + FnOnce(&bootstrap::Client, &'static FdTable) -> F) -> F::Output where
    F: futures::Future,
    F::Output: Send + 'static,
{
    inject::with_bootstrap(|client| {
        FD_TABLE.with(|tbl| {
            func(&client, tbl)
        })
    })
}

pub fn with_fds<F>(func: impl Send + 'static + FnOnce(&'static FdTable) -> F) -> F::Output where
    F: futures::Future,
    F::Output: Send + 'static,
{
    inject::inject(|| FD_TABLE.with(|tbl| func(tbl)))
}

/// Allocate a fresh file descriptor.
pub fn new_fd() -> libc::c_int {
    unsafe {
        let mut pipe_fds: [i32; 2] = [0, 0];
        let ret = libc::pipe2(&mut pipe_fds[0] as *mut libc::c_int,
                              libc::O_CLOEXEC);
        if ret < 0 {
            return ret
        }
        libc::close(pipe_fds[1]);
        return pipe_fds[0];
    }
}
