use libc;
use std::{
    collections::HashMap,
    sync,
};

use lazy_static;

pub trait Fd {
    fn close(&self) -> libc::c_int;
}

pub type FdPtr = sync::Arc<sync::Mutex<dyn Fd + Send>>;

struct FdTable {
    fds: sync::Mutex<HashMap<libc::c_int, FdPtr>>,
}

impl FdTable {
    fn new() -> Self {
        FdTable{
            fds: sync::Mutex::new(HashMap::new()),
        }
    }

    fn get(&self, fd: libc::c_int) -> Option<FdPtr> {
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

pub fn remove(fd: libc::c_int) -> Option<FdPtr> {
    FD_TABLE.lock().unwrap().remove(fd)
}
