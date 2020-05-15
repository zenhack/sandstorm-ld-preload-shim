#![feature(c_variadic)]
#[macro_use]
extern crate lazy_static;

pub mod util_capnp {
  include!(concat!(env!("OUT_DIR"), "/util_capnp.rs"));
}
pub mod filesystem_capnp {
  include!(concat!(env!("OUT_DIR"), "/filesystem_capnp.rs"));
}
pub mod preload_server_capnp {
  include!(concat!(env!("OUT_DIR"), "/preload_server_capnp.rs"));
}

mod inject;
mod real;
mod result;
mod vfs;
pub mod wrappers;
