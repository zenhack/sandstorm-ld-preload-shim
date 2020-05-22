//! The LD_PRELOAD function wrappers themselves
use libc::*;
use crate::{
    real,
    inject,
    vfs,
    vfs::Fd,
    result,
    filesystem_capnp,
    capnp_file::FsNode,
};
use std::ffi::CStr;
use std::path;
use std::path::{
    Path,
    PathBuf,
};

#[no_mangle]
pub unsafe extern "C" fn read(fd: c_int, buf: *mut c_void, count: size_t) -> ssize_t {
    if inject::in_rpc_thread() {
        return real::read(fd, buf, count)
    }

    // Raw pointers are not Send, so we need to do this to outsmart the type checker
    // and get the pointer into the event loop:
    let bufaddr = buf as usize;

    let res = vfs::with_fds(move |fds| {
        let r = fds.get(fd).map(|p| {
            let buf = bufaddr as *mut u8;
            let slice = std::slice::from_raw_parts_mut(buf, count);
            result::extract(p.read(slice), -1)
        });
        async move { r }
    });
    match res {
        Some(v) => v,
        None => real::read(fd, buf, count),
    }
}

#[no_mangle]
pub unsafe extern "C" fn write(fd: c_int, buf: *const c_void, count: size_t) -> ssize_t {
    if inject::in_rpc_thread() {
        return real::write(fd, buf, count)
    }

    // See comment in `read()`.
    let bufaddr = buf as usize;

    let res = vfs::with_fds(move |fds| {
        let r = fds.get(fd).map(|p| {
            let buf = bufaddr as *const u8;
            let slice = std::slice::from_raw_parts(buf, count);
            result::extract(p.write(slice), -1)
        });
        async move { r }
    });
    match res {
        Some(v) => v,
        None => real::write(fd, buf, count),
    }
}

#[no_mangle]
pub unsafe extern "C" fn close(fd: c_int) -> c_int {
    if inject::in_rpc_thread() {
        return real::close(fd)
    }

    vfs::with_fds(move |fds| {
        fds.remove(fd);
        async {}
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
    if inject::in_rpc_thread() {
        return real::open(pathname, flags, mode)
    }

    if let Ok(s) = CStr::from_ptr(pathname).to_str() {
        if let Some(abs_path) = make_absolute(Path::new(s)) {
            if let Ok(virt_path) = abs_path.strip_prefix("/sandstorm-magic") {
                return virt_open(virt_path.to_path_buf(), flags, mode);
            }
        }
    }
    real::open(pathname, flags, mode)
}

fn and_errno<T>((ret, err): (T, c_int)) -> T {
    unsafe {
        let errno_loc = libc::__errno_location();
        *errno_loc = err;
    }
    ret
}

fn virt_open(path: PathBuf, _flags: c_int, _mode: mode_t) -> c_int {
    and_errno(vfs::with_fds_and_bootstrap(move |bs, fds| {
        let rootfs = bs.rootfs_request().send().pipeline.get_dir();
        let mut node = filesystem_capnp::node::Client { client: rootfs.client };
        let mut final_reply = None;
        for component in path.components() {

            // TODO: maybe fail more loudly if these don't look right:
            let os_str = match component {
                path::Component::Normal(os_str) => os_str,
                _ => continue,
            };
            let segment = match os_str.to_str() {
                Some(s) => s,
                None => continue,
            };

            let dir = filesystem_capnp::directory::Client { client: node.client };
            let mut req = dir.walk_request();
            req.get().set_name(segment);
            let reply = req.send();
            node = filesystem_capnp::node::Client {
                client: reply.pipeline.get_node().client
            };
            final_reply = Some(reply);
        }
        async move {
            match final_reply {
                None => {
                    fds.add(vfs::FdPtr::new(FsNode::new(node)))
                },
                Some(reply) => {
                    match reply.promise.await {
                        // TODO: be smarter about what error to return.
                        Err(_) => (-1, libc::ENOENT),
                        Ok(res) => {
                            // FIXME: handle errors from get() and get_node():
                            let fd = vfs::FdPtr::new(FsNode::new(
                                res.get().unwrap().get_node().unwrap()
                            ));
                            fds.add(fd)
                        }
                    }
                }
            }
        }
    }))
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
