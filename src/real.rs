use lazy_static;

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

lazy_static! {
    static ref REAL: RealFns = unsafe { RealFns::get() };
}

pub unsafe fn close(fd: libc::c_int) -> libc::c_int {
    REAL.close(fd)
}
