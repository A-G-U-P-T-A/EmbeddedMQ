// Package emq wraps libemq via cgo.
//
// By default the EmbeddedMQ C engine is compiled from bindings/native
// (SQLite-style). Run `python scripts/sync_native.py` after core changes.
//
// Optional: link a prebuilt lib with
//
//	EMQ_INCLUDE=... EMQ_LIB=... go build -tags emq_system .
package emq

/*
#cgo !emq_system CFLAGS: -I${SRCDIR}/../native/include -I${SRCDIR}/../native/src -std=c11 -O2
#cgo !emq_system,windows CFLAGS: -DEMQ_PLATFORM_WINDOWS -D_CRT_SECURE_NO_WARNINGS
#cgo !emq_system,linux CFLAGS: -D_GNU_SOURCE -DEMQ_PLATFORM_POSIX
#cgo !emq_system,darwin CFLAGS: -D_DARWIN_C_SOURCE -DEMQ_PLATFORM_POSIX
#cgo emq_system CFLAGS: -DEMQ_SYSTEM_LIB=1 -I${EMQ_INCLUDE} -O2
#cgo emq_system LDFLAGS: -L${EMQ_LIB} -lemq
#cgo windows LDFLAGS: -lSynchronization
#cgo !windows LDFLAGS: -lpthread

#include <stdlib.h>
#include "emq/emq.h"
*/
import "C"

import (
	"errors"
	"fmt"
	"runtime"
	"unsafe"
)

// Status codes from emq_status.
const (
	OK             = int(C.EMQ_OK)
	ErrInvalid     = int(C.EMQ_ERR_INVALID)
	ErrNoMem       = int(C.EMQ_ERR_NOMEM)
	ErrNotFound    = int(C.EMQ_ERR_NOT_FOUND)
	ErrFull        = int(C.EMQ_ERR_FULL)
	ErrEmpty       = int(C.EMQ_ERR_EMPTY)
	ErrIO          = int(C.EMQ_ERR_IO)
	ErrTimeout     = int(C.EMQ_ERR_TIMEOUT)
	ErrExists      = int(C.EMQ_ERR_EXISTS)
	ErrClosed      = int(C.EMQ_ERR_CLOSED)
	ErrBusy        = int(C.EMQ_ERR_BUSY)
	ErrUnsupported = int(C.EMQ_ERR_UNSUPPORTED)
)

// Error represents a non-OK emq_status.
type Error struct {
	Code int
}

func (e Error) Error() string {
	s := C.emq_status_string(C.emq_status(e.Code))
	if s == nil {
		return fmt.Sprintf("emq error %d", e.Code)
	}
	return C.GoString(s)
}

func check(status C.emq_status) error {
	if status == C.EMQ_OK {
		return nil
	}
	return Error{Code: int(status)}
}

// Runtime owns an emq_runtime handle.
type Runtime struct {
	rt *C.emq_runtime
}

// NewRuntime creates a new EmbeddedMQ runtime.
func NewRuntime() (*Runtime, error) {
	var rt *C.emq_runtime
	if err := check(C.emq_runtime_create(&rt)); err != nil {
		return nil, err
	}
	r := &Runtime{rt: rt}
	runtime.SetFinalizer(r, (*Runtime).Close)
	return r, nil
}

// Close destroys the runtime. Queues must be closed first.
func (r *Runtime) Close() error {
	if r.rt == nil {
		return nil
	}
	C.emq_runtime_destroy(r.rt)
	r.rt = nil
	runtime.SetFinalizer(r, nil)
	return nil
}

// QueueOpts configures queue creation.
type QueueOpts struct {
	Capacity  uint32
	Producers uint32
	Consumers uint32
}

// CreateQueue opens a new queue on this runtime.
func (r *Runtime) CreateQueue(name string, opts *QueueOpts) (*Queue, error) {
	if r.rt == nil {
		return nil, errors.New("runtime closed")
	}
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))

	var cOpts C.emq_queue_opts
	C.emq_queue_opts_default(&cOpts)
	if opts != nil {
		cOpts.capacity = C.uint32_t(opts.Capacity)
		cOpts.producers = C.uint32_t(opts.Producers)
		cOpts.consumers = C.uint32_t(opts.Consumers)
	}

	var q *C.emq_queue
	if err := check(C.emq_queue_create(r.rt, cname, &cOpts, &q)); err != nil {
		return nil, err
	}
	return &Queue{rt: r, q: q}, nil
}

// Queue is a handle to an emq_queue.
type Queue struct {
	rt *Runtime
	q  *C.emq_queue
}

// Push enqueues a byte slice (zero-copy into C for the duration of the call;
// the engine copies into the queue).
func (q *Queue) Push(data []byte) error {
	if q.q == nil {
		return errors.New("queue closed")
	}
	var ptr unsafe.Pointer
	if len(data) > 0 {
		ptr = unsafe.Pointer(&data[0])
	}
	st := C.emq_push(q.q, ptr, C.size_t(len(data)), nil)
	runtime.KeepAlive(data)
	return check(st)
}

// Message holds an owned emq_message; call Release when done.
type Message struct {
	msg C.emq_message
}

// Data returns a single owned copy of the payload.
func (m *Message) Data() []byte {
	if m.msg.data == nil || m.msg.size == 0 {
		return nil
	}
	return C.GoBytes(m.msg.data, C.int(m.msg.size))
}

// Bytes is a zero-copy view of the payload. Valid only until Release.
func (m *Message) Bytes() []byte {
	if m.msg.data == nil || m.msg.size == 0 {
		return nil
	}
	return unsafe.Slice((*byte)(m.msg.data), int(m.msg.size))
}

// ID returns the message identifier.
func (m *Message) ID() uint64 {
	return uint64(m.msg.id)
}

// Release returns payload memory to the library.
func (m *Message) Release() {
	if m.msg.data != nil {
		C.emq_message_release(&m.msg)
		m.msg.data = nil
		m.msg.size = 0
	}
}

// Pop waits up to timeoutMs for a message (0 = non-blocking).
// Prefer PopCopy on hot paths to avoid heap-allocating Message.
func (q *Queue) Pop(timeoutMs uint32) (*Message, error) {
	if q.q == nil {
		return nil, errors.New("queue closed")
	}
	var msg C.emq_message
	if err := check(C.emq_pop(q.q, &msg, C.uint32_t(timeoutMs))); err != nil {
		return nil, err
	}
	return &Message{msg: msg}, nil
}

// PopCopy pops into dst via emq_pop_into (no malloc), returns byte length.
func (q *Queue) PopCopy(dst []byte, timeoutMs uint32) (int, error) {
	if q.q == nil {
		return 0, errors.New("queue closed")
	}
	var n C.size_t
	var ptr unsafe.Pointer
	if len(dst) > 0 {
		ptr = unsafe.Pointer(&dst[0])
	}
	st := C.emq_pop_into(q.q, ptr, C.size_t(len(dst)), &n, nil, C.uint32_t(timeoutMs))
	runtime.KeepAlive(dst)
	if st == C.EMQ_ERR_INVALID && len(dst) > 0 {
		return 0, fmt.Errorf("dst too small for message")
	}
	if err := check(st); err != nil {
		return 0, err
	}
	return int(n), nil
}

// PushRepeat pushes the same payload n times via emq_push_n (one cgo call).
func (q *Queue) PushRepeat(data []byte, n int) error {
	if q.q == nil {
		return errors.New("queue closed")
	}
	if n <= 0 {
		return nil
	}
	var ptr unsafe.Pointer
	if len(data) > 0 {
		ptr = unsafe.Pointer(&data[0])
	}
	var pushed C.size_t
	st := C.emq_push_n(q.q, ptr, C.size_t(len(data)), C.size_t(n), &pushed)
	runtime.KeepAlive(data)
	return check(st)
}

// PopCopyN pops up to max messages into dst via emq_pop_into_n.
// dst layout is max contiguous slots of msgCap bytes (len(dst) >= max*msgCap).
func (q *Queue) PopCopyN(dst []byte, msgCap int, max int) (int, error) {
	if q.q == nil {
		return 0, errors.New("queue closed")
	}
	if max <= 0 {
		return 0, nil
	}
	if msgCap <= 0 || len(dst) < msgCap*max {
		return 0, fmt.Errorf("dst too small: need %d have %d", msgCap*max, len(dst))
	}
	var n C.size_t
	st := C.emq_pop_into_n(q.q, unsafe.Pointer(&dst[0]), C.size_t(msgCap),
		C.size_t(max), &n, nil, 0)
	runtime.KeepAlive(dst)
	if err := check(st); err != nil {
		return int(n), err
	}
	return int(n), nil
}

// Close closes the queue handle.
func (q *Queue) Close() error {
	if q.q == nil {
		return nil
	}
	err := check(C.emq_queue_close(q.q))
	q.q = nil
	return err
}
