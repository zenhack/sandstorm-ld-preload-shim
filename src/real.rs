use lazy_static;
use libc::{c_int, c_void, mode_t, size_t, ssize_t, c_char};

fn cstr(buf: &[u8]) -> *const c_char {
    &buf[0] as *const u8 as *const c_char
}

unsafe fn dlnext(name: &[u8]) -> *mut c_void {
    libc::dlsym(libc::RTLD_NEXT, cstr(name))
}

type CloseType = unsafe extern fn(c_int) -> c_int;
type ReadType = unsafe extern fn(c_int, *mut c_void, size_t) -> ssize_t;
type WriteType = unsafe extern fn(c_int, *const c_void, size_t) -> ssize_t;
type OpenType = unsafe extern fn(*const c_char, c_int, mode_t) -> c_int;

#[derive(Copy, Clone)]
struct RealFns {
    real_open: OpenType,
    real_close: CloseType,
    real_read: ReadType,
    real_write: WriteType,
}

impl RealFns {
    unsafe fn get() -> Self {
        use std::mem::transmute;
        RealFns {
            real_open: transmute::<_, OpenType>(dlnext(b"open\0")),
            real_close: transmute::<_, CloseType>(dlnext(b"close\0")),
            real_read: transmute::<_, ReadType>(dlnext(b"read\0")),
            real_write: transmute::<_, WriteType>(dlnext(b"write\0")),
        }
    }

    unsafe fn open(&self, path: *const c_char, flags: c_int, mode: mode_t) -> c_int {
        let f = self.real_open;
        f(path, flags, mode)
    }

    unsafe fn close(&self, fd: c_int) -> c_int {
        let f = self.real_close;
        f(fd)
    }

    unsafe fn read(&self, fd: c_int, buf: *mut c_void, count: size_t) -> ssize_t {
        let f = self.real_read;
        f(fd, buf, count)
    }

    unsafe fn write(&self, fd: c_int, buf: *const c_void, count: size_t) -> ssize_t {
        let f = self.real_write;
        f(fd, buf, count)
    }
}

lazy_static! {
    static ref REAL: RealFns = unsafe { RealFns::get() };
}

pub unsafe fn open(path: *const c_char, flags: c_int, mode: mode_t) -> c_int {
    REAL.open(path, flags, mode)
}

pub unsafe fn close(fd: c_int) -> c_int {
    REAL.close(fd)
}

pub unsafe fn read(fd: c_int, buf: *mut c_void, count: size_t) -> ssize_t {
    REAL.read(fd, buf, count)
}

pub unsafe fn write(fd: c_int, buf: *const c_void, count: size_t) -> ssize_t {
    REAL.write(fd, buf, count)
}
