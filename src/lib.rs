#![feature(c_variadic)]
#[macro_use]
extern crate lazy_static;

pub mod util_capnp {
  include!(concat!(env!("OUT_DIR"), "/util_capnp.rs"));
}
pub mod filesystem_capnp {
  include!(concat!(env!("OUT_DIR"), "/filesystem_capnp.rs"));
}

mod real;
mod result;
mod vfs;
pub mod wrappers;
