#![feature(c_variadic)]
use libc;
use std::{
    collections::HashMap,
    sync,
};

mod real;

#[macro_use]
extern crate lazy_static;

trait Fd {
    fn close(&self) -> libc::c_int;
}

type FdPtr = sync::Arc<sync::Mutex<dyn Fd + Send>>;

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


fn open3(pathname: *const libc::c_char, flags: libc::c_int, mode:  libc::mode_t) -> libc::c_int {
    -1
}

/// The LD_PRELOAD wrappers themselves:
pub mod wrappers {
    use libc::*;

    #[no_mangle]
    pub unsafe extern "C" fn close(fd: c_int) -> c_int {
        if let Some(p) = super::FD_TABLE.lock().unwrap().remove(fd) {
            super::real::close(fd);
            p.lock().unwrap().close()
        } else {
            super::real::close(fd)
        }
    }

    #[no_mangle]
    pub unsafe extern "C" fn open(pathname: *const c_char, flags: c_int, mut args: ...) -> c_int {
        let mode = match flags & (O_CREAT | O_TMPFILE) {
            0 => 0,
            _ => args.arg::<mode_t>(),
        };
        super::open3(pathname, flags, mode)
    }
}
