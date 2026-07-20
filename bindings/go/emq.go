// Package emq wraps libemq via cgo.
//
// Set EMQ_INCLUDE to the directory containing emq/emq.h and EMQ_LIB to the
// directory containing libemq before building:
//
//	EMQ_INCLUDE=../../core/include EMQ_LIB=../../build go build .
package emq

/*
#cgo CFLAGS: -I${EMQ_INCLUDE}
#cgo LDFLAGS: -L${EMQ_LIB} -lemq
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
	OK           = int(C.EMQ_OK)
	ErrInvalid   = int(C.EMQ_ERR_INVALID)
	ErrNoMem     = int(C.EMQ_ERR_NOMEM)
	ErrNotFound  = int(C.EMQ_ERR_NOT_FOUND)
	ErrFull      = int(C.EMQ_ERR_FULL)
	ErrEmpty     = int(C.EMQ_ERR_EMPTY)
	ErrIO        = int(C.EMQ_ERR_IO)
	ErrTimeout   = int(C.EMQ_ERR_TIMEOUT)
	ErrExists    = int(C.EMQ_ERR_EXISTS)
	ErrClosed    = int(C.EMQ_ERR_CLOSED)
	ErrBusy      = int(C.EMQ_ERR_BUSY)
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

// Push enqueues a byte slice.
func (q *Queue) Push(data []byte) error {
	if q.q == nil {
		return errors.New("queue closed")
	}
	var ptr unsafe.Pointer
	if len(data) > 0 {
		ptr = unsafe.Pointer(&data[0])
	}
	return check(C.emq_push(q.q, ptr, C.size_t(len(data)), nil))
}

// Message holds an owned emq_message; call Release when done.
type Message struct {
	msg C.emq_message
}

// Data returns a copy of the payload.
func (m *Message) Data() []byte {
	if m.msg.data == nil || m.msg.size == 0 {
		return nil
	}
	out := make([]byte, int(m.msg.size))
	copy(out, C.GoBytes(m.msg.data, C.int(m.msg.size)))
	return out
}

// ID returns the message identifier.
func (m *Message) ID() uint64 {
	return uint64(m.msg.id)
}

// Release returns payload memory to the library.
func (m *Message) Release() {
	if m.msg.data != nil {
		C.emq_message_release(&m.msg)
	}
}

// Pop waits up to timeoutMs for a message (0 = non-blocking).
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

// Close closes the queue handle.
func (q *Queue) Close() error {
	if q.q == nil {
		return nil
	}
	err := check(C.emq_queue_close(q.q))
	q.q = nil
	return err
}
