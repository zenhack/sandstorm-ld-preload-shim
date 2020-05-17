//! The LD_PRELOAD function wrappers themselves
use libc::*;
use crate::{
    real,
    vfs,
    vfs::Fd,
    result,
};
use std::ffi::CStr;
use std::path::{
    Path,
    PathBuf,
};

#[no_mangle]
pub unsafe extern "C" fn read(fd: c_int, buf: *mut c_void, count: size_t) -> ssize_t {
    // Raw pointers are not Send, so we need to do this to outsmart the type checker
    // and get the pointer into the event loop:
    let bufaddr = buf as usize;

    let res = vfs::with_fds(move |_bs, fds| {
        fds.get(fd).map(|p| {
            let buf = bufaddr as *mut u8;
            let slice = std::slice::from_raw_parts_mut(buf, count);
            result::extract(p.read(slice), -1)
        })
    });
    match res {
        Some(v) => v,
        None => real::read(fd, buf, count),
    }
}

#[no_mangle]
pub unsafe extern "C" fn write(fd: c_int, buf: *const c_void, count: size_t) -> ssize_t {
    // See comment in `read()`.
    let bufaddr = buf as usize;

    let res = vfs::with_fds(move |_bs, fds| {
        fds.get(fd).map(|p| {
            let buf = bufaddr as *const u8;
            let slice = std::slice::from_raw_parts(buf, count);
            result::extract(p.write(slice), -1)
        })
    });
    match res {
        Some(v) => v,
        None => real::write(fd, buf, count),
    }
}

#[no_mangle]
pub unsafe extern "C" fn close(fd: c_int) -> c_int {
    vfs::with_fds(move |_bs, fds| {
        fds.remove(fd);
    });
    real::close(fd)
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
    if let Ok(s) = CStr::from_ptr(pathname).to_str() {
        if let Some(abs_path) = make_absolute(Path::new(s)) {
            if let Ok(virt_path) = abs_path.strip_prefix("/sandstorm-magic") {
                return virt_open(virt_path, flags, mode);
            }
        }
    }
    real::open(pathname, flags, mode)
}

fn virt_open(_path: &Path, _flags: c_int, _mode: mode_t) -> c_int {
    panic!("TODO");
}

fn make_absolute(path: &Path) -> Option<PathBuf> {
    if path.is_absolute() {
        return Some(path.to_path_buf())
    };
    match std::env::current_dir() {
        Ok(cwd_path) => Some(cwd_path.join(path)),
        Err(_) => {
            // No way of working out what the absolute path
            // we were given is; we'll treat this as not
            // something we should handle:
            None
        }
    }
}
