use std::sync::mpsc;
use capnp_rpc::{
    twoparty,
    rpc_twoparty_capnp::Side,
    RpcSystem,
};
use futures::task;
use tokio::{
    runtime,
    net::UnixStream,
};
use tokio_util::compat::{
    Tokio02AsyncReadCompatExt,
    Tokio02AsyncWriteCompatExt,
};
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
   static BOOTSTRAP: bootstrap::Client =
        capnp_rpc::new_promise_client(Box::pin(get_bootstrap()));
}

/// Connect to the preload server and return its bootstrap interface.
///
/// Note that this is meant to be called once per process. It (intentionally)
/// leaks the connection, so DO NOT use this elsewhere.
async fn get_bootstrap() -> Result<capnp::capability::Client, capnp::Error> {
    let path = std::env::var("SANDSTORM_PRELOAD_SERVER").expect(
        "Environment variable SANDSTORM_PRELOAD_SERVER \
        not defined; can't use sandstorm LD_PRELOAD shim."
    );
    let stream = Box::leak(Box::new(UnixStream::connect(path).await?));
    // TODO: make sure socket is close-on-exec.
    let (rx, tx) = stream.split();
    let vat_net = Box::new(twoparty::VatNetwork::new(
        rx.compat(),
        tx.compat_write(),
        Side::Client,
        Default::default(),
    ));
    let mut rpc_system = RpcSystem::new(vat_net, None);
    let client: bootstrap::Client = rpc_system.bootstrap(Side::Server);
    tokio::task::spawn_local(Box::pin(async { rpc_system.await }));
    Ok(client.client)
}

pub fn inject<F>(func: impl FnOnce(bootstrap::Client) -> F + Send + 'static) -> F::Output where
    F: futures::Future,
    F::Output: Send + 'static,
{
    EVENT_LOOP_HANDLE.block_on(BOOTSTRAP.with(move |c| {
        func(c.clone())
    }))
}
