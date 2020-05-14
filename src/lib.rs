#![feature(c_variadic)]
use libc;
use std::{
    collections::HashMap,
    sync,
};

#[macro_use]
extern crate lazy_static;

fn cstr(buf: &[u8]) -> *const libc::c_char {
    &buf[0] as *const u8 as *const libc::c_char
}

unsafe fn dlnext(name: &[u8]) -> *mut libc::c_void {
    libc::dlsym(libc::RTLD_NEXT, cstr(name))
}

type CloseType = unsafe extern fn(libc::c_int) -> libc::c_int;

#[derive(Copy, Clone)]
struct RealFns {
    real_close: CloseType,
}

impl RealFns {
    unsafe fn get() -> Self {
        use std::mem::transmute;
        RealFns {
            real_close: transmute::<_, CloseType>(dlnext(b"close\0")),
        }
    }

    unsafe fn close(&self, fd: libc::c_int) -> libc::c_int {
        let f = self.real_close;
        f(fd)
    }
}

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

    fn remove(&self, fd: libc::c_int) -> Option<FdPtr> {
        self.fds.lock().unwrap().remove(&fd)
    }
}

lazy_static! {
    static ref REAL: RealFns = unsafe { RealFns::get() };
    static ref FD_TABLE: sync::Mutex<FdTable> = sync::Mutex::new(FdTable::new());
}


fn open3(pathname: *const libc::c_char, flags: libc::c_int, mode:  libc::mode_t) -> libc::c_int {
    -1
}

/// The LD_PRELOAD wrappers themselves:
pub mod wrappers {
    use super::REAL;
    use libc::*;

    #[no_mangle]
    pub unsafe extern "C" fn close(fd: c_int) -> c_int {
        if let Some(p) = super::FD_TABLE.remove(fd) {
            p.close()
        }
        REAL.close(fd)
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
