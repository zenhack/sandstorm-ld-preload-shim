use libc;
use std::{
    collections::HashMap,
    sync,
};
use crate::result::Result;

use lazy_static;

pub trait Fd {
    fn read(&self, buf: &mut [u8]) -> Result<isize>;
    fn write(&self, buf: &[u8]) -> Result<isize>;
}

#[derive(Clone)]
pub struct FdPtr(sync::Arc<sync::Mutex<dyn Fd + Send>>);


impl FdPtr {
    // TODO: can we avoid the static lifetime? Maybe using Pin?
    pub fn new(fd: impl Fd + Send + 'static) -> Self {
        FdPtr(sync::Arc::new(sync::Mutex::new(fd)))
    }
}

impl Fd for FdPtr {
    fn read(&self, buf: &mut [u8]) -> Result<isize> {
        self.0.lock().unwrap().read(buf)
    }

    fn write(&self, buf: &[u8]) -> Result<isize> {
        self.0.lock().unwrap().write(buf)
    }
}

struct FdTable {
    fds: sync::Mutex<HashMap<libc::c_int, FdPtr>>,
}

impl FdTable {
    fn new() -> Self {
        FdTable{
            fds: sync::Mutex::new(HashMap::new()),
        }
    }

    pub fn get(&self, fd: libc::c_int) -> Option<FdPtr> {
        let mg = self.fds.lock().unwrap();
        mg.get(&fd).map(|v| v.clone())
    }

    pub fn remove(&self, fd: libc::c_int) -> Option<FdPtr> {
        self.fds.lock().unwrap().remove(&fd)
    }
}

lazy_static! {
    static ref FD_TABLE: sync::Mutex<FdTable> = sync::Mutex::new(FdTable::new());
}

pub fn get(fd: libc::c_int) -> Option<FdPtr> {
    FD_TABLE.lock().unwrap().get(fd)
}

pub fn remove(fd: libc::c_int) -> Option<FdPtr> {
    FD_TABLE.lock().unwrap().remove(fd)
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
