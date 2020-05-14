//! The LD_PRELOAD function wrappers themselves
use libc::*;
use crate::{
    real,
    vfs,
    result,
};

#[no_mangle]
pub unsafe extern "C" fn read(fd: c_int, buf: *mut c_void, count: size_t) -> ssize_t {
    if let Some(p) = vfs::get(fd) {
        let buf = buf as *mut u8;
        let slice = std::slice::from_raw_parts_mut(buf, count);
        result::extract(p.lock().unwrap().read(slice), -1)
    } else {
        real::read(fd, buf, count)
    }
}

#[no_mangle]
pub unsafe extern "C" fn write(fd: c_int, buf: *const c_void, count: size_t) -> ssize_t {
    real::write(fd, buf, count)
}

#[no_mangle]
pub unsafe extern "C" fn close(fd: c_int) -> c_int {
    if let Some(p) = vfs::remove(fd) {
        real::close(fd);
        result::extract(p.lock().unwrap().close().map(|_| 0), -1)
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
