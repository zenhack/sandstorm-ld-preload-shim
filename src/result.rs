pub type Result<T> = std::result::Result<T, errno::Errno>;


pub fn extract<T>(res: Result<T>, def: T) -> T {
    match res {
        Ok(v) => v,
        Err(e) => {
            errno::set_errno(e);
            def
        }
    }
}
