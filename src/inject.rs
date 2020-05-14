use std::sync::mpsc;

struct Job {
}

impl Job {
    pub fn run(self) {
    }
}

lazy_static! {
    static ref JOB_SENDER: mpsc::SyncSender<Job> = {
        let (sender, receiver) = mpsc::sync_channel::<Job>(0);
        std::thread::spawn(move || {
            loop {
                match receiver.recv() {
                    Err(_) => return,
                    Ok(job) => {
                        job.run();
                    }
                }
            }
        });
        sender
    };
}
