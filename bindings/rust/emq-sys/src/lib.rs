//! Raw FFI to `libemq`. Prefer the safe `emq` crate for application code.

#![allow(non_camel_case_types, non_snake_case, dead_code)]

use std::os::raw::{c_char, c_int, c_void};

pub type emq_status = c_int;

pub const EMQ_OK: emq_status = 0;
pub const EMQ_ERR_INVALID: emq_status = -1;
pub const EMQ_ERR_NOMEM: emq_status = -2;
pub const EMQ_ERR_NOT_FOUND: emq_status = -3;
pub const EMQ_ERR_FULL: emq_status = -4;
pub const EMQ_ERR_EMPTY: emq_status = -5;
pub const EMQ_ERR_IO: emq_status = -6;
pub const EMQ_ERR_TIMEOUT: emq_status = -7;
pub const EMQ_ERR_EXISTS: emq_status = -8;
pub const EMQ_ERR_CLOSED: emq_status = -9;
pub const EMQ_ERR_BUSY: emq_status = -10;
pub const EMQ_ERR_UNSUPPORTED: emq_status = -11;

pub const EMQ_MSG_FLAG_BORROWED: u32 = 0x8000_0000;
pub const EMQ_MSG_FLAG_CLAIMED: u32 = 0x4000_0000;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct emq_message {
    pub id: u64,
    pub offset: u64,
    pub priority: u32,
    pub deliver_at_ns: u64,
    pub ttl_ns: u64,
    pub data: *const c_void,
    pub size: usize,
    pub flags: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct emq_queue_opts {
    pub storage: u32,
    pub policy: u32,
    pub delivery: u32,
    pub fsync: u32,
    pub capacity: u32,
    pub visibility_ms: u32,
    pub inline_threshold: u32,
    pub ring_size: u32,
    pub path: *const c_char,
    pub producers: u32,
    pub consumers: u32,
    pub backpressure: u32,
    pub high_watermark: u32,
    pub low_watermark: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct emq_stats {
    pub enqueued: u64,
    pub dequeued: u64,
    pub acked: u64,
    pub nacked: u64,
    pub expired: u64,
    pub depth: u64,
    pub bytes: u64,
    pub redelivered: u64,
}

pub enum emq_runtime {}
pub enum emq_queue {}

extern "C" {
    pub fn emq_runtime_create(out: *mut *mut emq_runtime) -> emq_status;
    pub fn emq_runtime_destroy(rt: *mut emq_runtime);
    pub fn emq_queue_opts_default(opts: *mut emq_queue_opts);
    pub fn emq_queue_create(
        rt: *mut emq_runtime,
        name: *const c_char,
        opts: *const emq_queue_opts,
        out: *mut *mut emq_queue,
    ) -> emq_status;
    pub fn emq_queue_close(q: *mut emq_queue) -> emq_status;
    pub fn emq_push(
        q: *mut emq_queue,
        data: *const c_void,
        size: usize,
        meta: *const emq_message,
    ) -> emq_status;
    pub fn emq_pop(
        q: *mut emq_queue,
        out: *mut emq_message,
        timeout_ms: u32,
    ) -> emq_status;
    pub fn emq_try_pop(q: *mut emq_queue, out: *mut emq_message) -> emq_status;
    pub fn emq_claim(
        q: *mut emq_queue,
        out: *mut emq_message,
        timeout_ms: u32,
    ) -> emq_status;
    pub fn emq_release_claim(q: *mut emq_queue, message: *mut emq_message) -> emq_status;
    pub fn emq_queue_stats(q: *mut emq_queue, out: *mut emq_stats) -> emq_status;
    pub fn emq_message_release(message: *mut emq_message);
    pub fn emq_status_string(status: emq_status) -> *const c_char;
}
