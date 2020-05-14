//! The LD_PRELOAD function wrappers themselves
use libc::*;
use crate::{
    real,
    vfs,
};

#[no_mangle]
pub unsafe extern "C" fn close(fd: c_int) -> c_int {
    if let Some(p) = vfs::remove(fd) {
        real::close(fd);
        p.lock().unwrap().close()
    } else {
        real::close(fd)
    }
}

#[no_mangle]
pub unsafe extern "C" fn open(pathname: *const c_char, flags: c_int, mut args: ...) -> c_int {
    let mode = match flags & (O_CREAT | O_TMPFILE) {
        0 => 0,
        _ => args.arg::<mode_t>(),
    };
    open3(pathname, flags, mode)
}

unsafe fn open3(pathname: *const c_char, flags: c_int, mode: mode_t) -> c_int {
    -1
}
