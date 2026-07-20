//! Safe Rust bindings for EmbeddedMQ.

use emq_sys::{
    emq_message, emq_message_release, emq_pop, emq_push, emq_queue_close, emq_queue_create,
    emq_queue_opts, emq_queue_opts_default, emq_queue_stats, emq_runtime_create,
    emq_runtime_destroy, emq_stats, emq_status_string, EMQ_ERR_EMPTY, EMQ_ERR_INVALID, EMQ_OK,
    EMQ_MSG_FLAG_BORROWED, EMQ_MSG_FLAG_CLAIMED,
};
use std::ffi::{CStr, CString};
use std::cell::Cell;
use std::marker::PhantomData;
use std::ptr;
use std::time::Duration;

/// Library error mapped from `emq_status`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Error {
    pub code: i32,
}

impl Error {
    pub fn message(self) -> &'static str {
        unsafe {
            let ptr = emq_status_string(self.code);
            if ptr.is_null() {
                "unknown"
            } else {
                CStr::from_ptr(ptr).to_str().unwrap_or("invalid utf-8")
            }
        }
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} ({})", self.message(), self.code)
    }
}

impl std::error::Error for Error {}

pub type Result<T> = std::result::Result<T, Error>;

fn check(status: i32) -> Result<()> {
    if status == EMQ_OK {
        Ok(())
    } else {
        Err(Error { code: status })
    }
}

/// Owns an `emq_runtime` handle.
pub struct Runtime {
    raw: *mut emq_sys::emq_runtime,
}

impl Runtime {
    pub fn new() -> Result<Self> {
        let mut raw = ptr::null_mut();
        check(unsafe { emq_runtime_create(&mut raw) })?;
        Ok(Self { raw })
    }

    pub fn create_queue(&self, name: &str, opts: Option<QueueOpts>) -> Result<Queue<'_>> {
        let cname = CString::new(name).map_err(|_| Error { code: EMQ_ERR_INVALID })?;
        let mut c_opts = QueueOpts::default().into_c();
        if let Some(o) = opts {
            c_opts = o.into_c();
        } else {
            unsafe { emq_queue_opts_default(&mut c_opts) };
        }
        let mut raw = ptr::null_mut();
        check(unsafe {
            emq_queue_create(self.raw, cname.as_ptr(), &c_opts, &mut raw)
        })?;
        Ok(Queue {
            raw,
            _rt: PhantomData,
        })
    }
}

impl Drop for Runtime {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { emq_runtime_destroy(self.raw) };
        }
    }
}

/// Queue options mirroring `emq_queue_opts` (defaults applied by C when omitted).
#[derive(Clone, Debug)]
pub struct QueueOpts {
    pub capacity: u32,
    pub producers: u32,
    pub consumers: u32,
}

impl Default for QueueOpts {
    fn default() -> Self {
        Self {
            capacity: 0,
            producers: 1,
            consumers: 1,
        }
    }
}

impl QueueOpts {
    fn into_c(self) -> emq_queue_opts {
        let mut opts = unsafe {
            let mut o = std::mem::MaybeUninit::<emq_queue_opts>::uninit();
            emq_queue_opts_default(o.as_mut_ptr());
            o.assume_init()
        };
        opts.capacity = self.capacity;
        opts.producers = self.producers;
        opts.consumers = self.consumers;
        opts
    }
}

/// Single-producer single-consumer queue handle.
///
/// Marked `!Sync` because the C contract allows only one pusher and one popper thread.
pub struct SpscQueue<'rt> {
    inner: Queue<'rt>,
    /// SPSC queues are not `Sync` (one pusher, one popper).
    _not_sync: PhantomData<Cell<()>>,
}

impl<'rt> SpscQueue<'rt> {
    pub fn new(rt: &'rt Runtime, name: &str, capacity: u32) -> Result<Self> {
        let inner = rt.create_queue(
            name,
            Some(QueueOpts {
                capacity,
                producers: 1,
                consumers: 1,
            }),
        )?;
        Ok(Self {
            inner,
            _not_sync: PhantomData,
        })
    }

    pub fn push(&self, data: &[u8]) -> Result<()> {
        self.inner.push(data)
    }

    pub fn pop(&self, timeout: Option<Duration>) -> Result<Message> {
        self.inner.pop(timeout)
    }

    pub fn try_pop(&self) -> Result<Option<Message>> {
        self.inner.try_pop()
    }

    pub fn stats(&self) -> Result<Stats> {
        self.inner.stats()
    }
}

/// Queue bound to a runtime lifetime.
pub struct Queue<'rt> {
    raw: *mut emq_sys::emq_queue,
    _rt: PhantomData<&'rt Runtime>,
}

impl<'rt> Queue<'rt> {
    pub fn push(&self, data: &[u8]) -> Result<()> {
        check(unsafe {
            emq_push(
                self.raw,
                data.as_ptr() as *const _,
                data.len(),
                ptr::null(),
            )
        })
    }

    pub fn pop(&self, timeout: Option<Duration>) -> Result<Message> {
        let mut msg = emq_message {
            data: ptr::null(),
            ..Default::default()
        };
        let ms = timeout.map(|d| d.as_millis() as u32).unwrap_or(0);
        check(unsafe { emq_pop(self.raw, &mut msg, ms) })?;
        Ok(Message { inner: msg })
    }

    pub fn try_pop(&self) -> Result<Option<Message>> {
        let mut msg = emq_message {
            data: ptr::null(),
            ..Default::default()
        };
        let status = unsafe { emq_sys::emq_try_pop(self.raw, &mut msg) };
        match status {
            EMQ_OK => Ok(Some(Message { inner: msg })),
            EMQ_ERR_EMPTY => Ok(None),
            other => Err(Error { code: other }),
        }
    }

    pub fn stats(&self) -> Result<Stats> {
        let mut raw = emq_stats::default();
        check(unsafe { emq_queue_stats(self.raw, &mut raw) })?;
        Ok(Stats { raw })
    }

    pub fn close(self) -> Result<()> {
        check(unsafe { emq_queue_close(self.raw) })
    }
}

/// Owned message; frees payload on drop via `emq_message_release`.
pub struct Message {
    inner: emq_message,
}

impl Message {
    pub fn id(&self) -> u64 {
        self.inner.id
    }

    pub fn as_bytes(&self) -> &[u8] {
        if self.inner.data.is_null() || self.inner.size == 0 {
            return &[];
        }
        unsafe { std::slice::from_raw_parts(self.inner.data as *const u8, self.inner.size) }
    }

    pub fn is_borrowed(&self) -> bool {
        self.inner.flags & EMQ_MSG_FLAG_BORROWED != 0
    }

    pub fn is_claimed(&self) -> bool {
        self.inner.flags & EMQ_MSG_FLAG_CLAIMED != 0
    }
}

impl Drop for Message {
    fn drop(&mut self) {
        unsafe { emq_message_release(&mut self.inner) };
    }
}

/// Snapshot of `emq_stats`.
#[derive(Clone, Default)]
pub struct Stats {
    raw: emq_stats,
}

impl Stats {
    pub fn enqueued(&self) -> u64 {
        self.raw.enqueued
    }
    pub fn dequeued(&self) -> u64 {
        self.raw.dequeued
    }
    pub fn depth(&self) -> u64 {
        self.raw.depth
    }
}
