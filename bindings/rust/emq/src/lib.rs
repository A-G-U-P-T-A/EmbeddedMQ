//! Safe Rust bindings for EmbeddedMQ.

use emq_sys::{
    emq_claim, emq_message, emq_message_release, emq_pop, emq_pop_into, emq_pop_into_n, emq_push,
    emq_push_n, emq_queue_close, emq_queue_create, emq_queue_opts, emq_queue_opts_default,
    emq_queue_stats, emq_release_claim, emq_runtime_create, emq_runtime_destroy, emq_stats,
    emq_status_string, EMQ_ERR_EMPTY, EMQ_ERR_INVALID, EMQ_MSG_FLAG_BORROWED, EMQ_MSG_FLAG_CLAIMED,
    EMQ_OK,
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

    pub fn pop_into(&self, dst: &mut [u8], timeout: Option<Duration>) -> Result<usize> {
        self.inner.pop_into(dst, timeout)
    }

    pub fn push_n(&self, data: &[u8], count: usize) -> Result<usize> {
        self.inner.push_n(data, count)
    }

    pub fn pop_into_n(
        &self,
        dst: &mut [u8],
        msg_cap: usize,
        max_count: usize,
        timeout: Option<Duration>,
    ) -> Result<usize> {
        self.inner.pop_into_n(dst, msg_cap, max_count, timeout)
    }

    pub fn claim(&self, timeout: Option<Duration>) -> Result<Claim> {
        self.inner.claim(timeout)
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
        let mut msg = emq_message::default();
        let ms = timeout.map(|d| d.as_millis() as u32).unwrap_or(0);
        check(unsafe { emq_pop(self.raw, &mut msg, ms) })?;
        Ok(Message { inner: msg })
    }

    /// Hot path: copy into `dst` with no engine malloc. Returns byte length.
    pub fn pop_into(&self, dst: &mut [u8], timeout: Option<Duration>) -> Result<usize> {
        let mut n = 0usize;
        let ms = timeout.map(|d| d.as_millis() as u32).unwrap_or(0);
        let status = unsafe {
            emq_pop_into(
                self.raw,
                dst.as_mut_ptr() as *mut _,
                dst.len(),
                &mut n,
                ptr::null_mut(),
                ms,
            )
        };
        match status {
            EMQ_OK => Ok(n),
            EMQ_ERR_INVALID => Err(Error { code: EMQ_ERR_INVALID }),
            other => Err(Error { code: other }),
        }
    }

    /// Push the same payload `count` times (amortize FFI).
    pub fn push_n(&self, data: &[u8], count: usize) -> Result<usize> {
        let mut pushed = 0usize;
        check(unsafe {
            emq_push_n(
                self.raw,
                data.as_ptr() as *const _,
                data.len(),
                count,
                &mut pushed,
            )
        })?;
        Ok(pushed)
    }

    /// Batch pop into a stride buffer (`dst.len() >= msg_cap * max_count`).
    pub fn pop_into_n(
        &self,
        dst: &mut [u8],
        msg_cap: usize,
        max_count: usize,
        timeout: Option<Duration>,
    ) -> Result<usize> {
        if msg_cap == 0 || dst.len() < msg_cap.saturating_mul(max_count) {
            return Err(Error { code: EMQ_ERR_INVALID });
        }
        let mut count = 0usize;
        let ms = timeout.map(|d| d.as_millis() as u32).unwrap_or(0);
        let status = unsafe {
            emq_pop_into_n(
                self.raw,
                dst.as_mut_ptr() as *mut _,
                msg_cap,
                max_count,
                &mut count,
                ptr::null_mut(),
                ms,
            )
        };
        match status {
            EMQ_OK => Ok(count),
            EMQ_ERR_EMPTY if count == 0 => Ok(0),
            other => Err(Error { code: other }),
        }
    }

    /// Zero-copy claim into the ring; must call [`Claim::release`].
    pub fn claim(&self, timeout: Option<Duration>) -> Result<Claim> {
        let mut msg = emq_message::default();
        let ms = timeout.map(|d| d.as_millis() as u32).unwrap_or(0);
        check(unsafe { emq_claim(self.raw, &mut msg, ms) })?;
        Ok(Claim {
            queue: self.raw,
            inner: msg,
        })
    }

    pub fn try_pop(&self) -> Result<Option<Message>> {
        let mut msg = emq_message::default();
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

/// Zero-copy claim; payload is valid until [`Claim::release`] or drop.
pub struct Claim {
    queue: *mut emq_sys::emq_queue,
    inner: emq_message,
}

impl Claim {
    pub fn as_bytes(&self) -> &[u8] {
        if self.inner.data.is_null() || self.inner.size == 0 {
            return &[];
        }
        unsafe { std::slice::from_raw_parts(self.inner.data as *const u8, self.inner.size) }
    }

    pub fn release(mut self) -> Result<()> {
        check(unsafe { emq_release_claim(self.queue, &mut self.inner) })?;
        self.inner.data = ptr::null();
        Ok(())
    }
}

impl Drop for Claim {
    fn drop(&mut self) {
        if !self.inner.data.is_null() {
            unsafe {
                let _ = emq_release_claim(self.queue, &mut self.inner);
            }
        }
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
