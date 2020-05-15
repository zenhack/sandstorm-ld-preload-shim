use futures::task;
use tokio::runtime;
use std::sync::mpsc;
use crate::preload_server_capnp::bootstrap;

// An implementation of Future which never resolves. I(zenhack) am sure this must exist
// in some library somewhere, but am having trouble finding it.
struct Forever{}
impl futures::Future for Forever {
    type Output = ();

    fn poll(self: std::pin::Pin<&mut Self>, _cx: &mut task::Context) -> task::Poll<Self::Output> {
        task::Poll::Pending
    }
}

lazy_static! {
    static ref EVENT_LOOP_HANDLE: runtime::Handle = {
        let (tx, rx) = mpsc::sync_channel::<runtime::Handle>(0);
        std::thread::spawn(move || {
            let mut my_runtime = runtime::Runtime::new().unwrap();
            tx.send(my_runtime.handle().clone()).unwrap();
            my_runtime.block_on(Forever{});
        });
        rx.recv().unwrap()
    };
}

thread_local! {
    static BOOTSTRAP: bootstrap::Client = {
        let _path = std::env::var("SANDSTORM_VFS_SERVER")
            .expect(
                "Environment variable SANDSTORM_PRELOAD_SERVER \
                not defined; can't use sandstorm LD_PRELOAD shim."
            );
        panic!("TODO: connect to server and get bootstrap interface.");
    }
}

pub fn inject<F>(func: impl FnOnce(bootstrap::Client) -> F) -> F::Output where
    F: futures::Future + Send + 'static
{
    EVENT_LOOP_HANDLE.block_on(BOOTSTRAP.with(|c| {
        func(c.clone())
    }))
}
