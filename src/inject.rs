use futures::task;
use tokio::runtime;
use std::sync::mpsc;

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
        let (sender, receiver) = mpsc::sync_channel::<runtime::Handle>(0);
        std::thread::spawn(move || {
            let mut my_runtime = runtime::Runtime::new().unwrap();
            let handle = my_runtime.handle();
            sender.send(handle.clone()).unwrap();
            my_runtime.block_on(Forever{});
        });
        receiver.recv().unwrap()
    };
}
