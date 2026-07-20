#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "emq/emq.h"

/* ------------------------------------------------------------------ */
/* Exceptions                                                          */
/* ------------------------------------------------------------------ */

static PyObject *EmqError;

static PyObject *raise_emq(emq_status status) {
  const char *msg = emq_status_string(status);
  if (!msg) msg = "unknown";
  PyErr_SetString(EmqError, msg);
  return NULL;
}

/* ------------------------------------------------------------------ */
/* Message                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
  PyObject_HEAD
  emq_message msg;
  int owns;
} MessageObject;

static void Message_dealloc(MessageObject *self) {
  if (self->owns) {
    emq_message_release(&self->msg);
  }
  Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *Message_data(MessageObject *self, PyObject *Py_UNUSED(ignored)) {
  if (!self->msg.data || self->msg.size == 0) {
    return PyBytes_FromStringAndSize("", 0);
  }
  return PyBytes_FromStringAndSize((const char *)self->msg.data,
                                   (Py_ssize_t)self->msg.size);
}

static PyObject *Message_id(MessageObject *self, PyObject *Py_UNUSED(ignored)) {
  return PyLong_FromUnsignedLongLong(self->msg.id);
}

static PyMethodDef Message_methods[] = {
    {"data", (PyCFunction)Message_data, METH_NOARGS, "Return message payload bytes."},
    {"id", (PyCFunction)Message_id, METH_NOARGS, "Return message id."},
    {NULL, NULL, 0, NULL}};

static PyTypeObject MessageType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "embeddedmq._emq.Message",
    .tp_basicsize = sizeof(MessageObject),
    .tp_dealloc = (destructor)Message_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = Message_methods,
};

static MessageObject *message_from_emq(emq_message *src) {
  MessageObject *m = PyObject_New(MessageObject, &MessageType);
  if (!m) return NULL;
  m->msg = *src;
  m->owns = 1;
  memset(src, 0, sizeof(*src));
  return m;
}

/* ------------------------------------------------------------------ */
/* Queue                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
  PyObject_HEAD
  emq_runtime *rt;
  emq_queue *q;
} QueueObject;

static void Queue_dealloc(QueueObject *self) {
  if (self->q) {
    emq_queue_close(self->q);
    self->q = NULL;
  }
  Py_TYPE(self)->tp_free((PyObject *)self);
}

static int parse_timeout_ms(PyObject *obj, unsigned int *out) {
  unsigned long v = PyLong_AsUnsignedLong(obj);
  if (v == (unsigned long)-1 && PyErr_Occurred()) return -1;
  if (v > 0xFFFFFFFFu) {
    PyErr_SetString(PyExc_OverflowError, "timeout_ms too large");
    return -1;
  }
  *out = (unsigned int)v;
  return 0;
}

static PyObject *Queue_push(QueueObject *self, PyObject *const *args,
                            Py_ssize_t nargs) {
  const void *data;
  Py_ssize_t len;
  Py_buffer buf;
  int have_buf = 0;
  emq_status st;

  if (nargs != 1) {
    PyErr_SetString(PyExc_TypeError, "push() takes exactly 1 argument");
    return NULL;
  }

  /* Hot path: exact bytes — skip buffer-protocol overhead. */
  if (PyBytes_Check(args[0])) {
    data = PyBytes_AS_STRING(args[0]);
    len = PyBytes_GET_SIZE(args[0]);
  } else if (PyByteArray_Check(args[0])) {
    data = PyByteArray_AS_STRING(args[0]);
    len = PyByteArray_GET_SIZE(args[0]);
  } else {
    if (PyObject_GetBuffer(args[0], &buf, PyBUF_SIMPLE) < 0) return NULL;
    data = buf.buf;
    len = buf.len;
    have_buf = 1;
  }

  st = emq_push(self->q, data, (size_t)len, NULL);
  if (have_buf) PyBuffer_Release(&buf);
  if (st != EMQ_OK) return raise_emq(st);
  Py_RETURN_NONE;
}

static PyObject *Queue_pop(QueueObject *self, PyObject *const *args,
                           Py_ssize_t nargs) {
  unsigned int timeout_ms = 0;
  emq_message msg;
  emq_status st;

  if (nargs > 1) {
    PyErr_SetString(PyExc_TypeError, "pop() takes at most 1 argument");
    return NULL;
  }
  if (nargs == 1 && parse_timeout_ms(args[0], &timeout_ms) < 0) return NULL;

  memset(&msg, 0, sizeof(msg));
  st = emq_pop(self->q, &msg, timeout_ms);
  if (st != EMQ_OK) return raise_emq(st);
  return (PyObject *)message_from_emq(&msg);
}

/* Hot path: return payload bytes directly (one alloc, no Message object). */
static PyObject *Queue_pop_bytes(QueueObject *self, PyObject *const *args,
                                 Py_ssize_t nargs) {
  unsigned int timeout_ms = 0;
  emq_message msg;
  emq_status st;
  PyObject *out;

  if (nargs > 1) {
    PyErr_SetString(PyExc_TypeError, "pop_bytes() takes at most 1 argument");
    return NULL;
  }
  if (nargs == 1 && parse_timeout_ms(args[0], &timeout_ms) < 0) return NULL;

  memset(&msg, 0, sizeof(msg));
  st = emq_pop(self->q, &msg, timeout_ms);
  if (st != EMQ_OK) return raise_emq(st);
  if (!msg.data || msg.size == 0) {
    out = PyBytes_FromStringAndSize("", 0);
  } else {
    out = PyBytes_FromStringAndSize((const char *)msg.data, (Py_ssize_t)msg.size);
  }
  emq_message_release(&msg);
  return out;
}

/*
 * Hot path: emq_pop_into into a writable buffer (no malloc in the engine).
 */
static PyObject *Queue_pop_copy(QueueObject *self, PyObject *const *args,
                                Py_ssize_t nargs) {
  unsigned int timeout_ms = 0;
  emq_status st;
  char *dst;
  Py_ssize_t dst_len;
  Py_buffer buf;
  int have_buf = 0;
  size_t n = 0;

  if (nargs < 1 || nargs > 2) {
    PyErr_SetString(PyExc_TypeError, "pop_copy(buf, timeout_ms=0)");
    return NULL;
  }
  if (nargs == 2 && parse_timeout_ms(args[1], &timeout_ms) < 0) return NULL;

  if (PyByteArray_Check(args[0])) {
    dst = PyByteArray_AS_STRING(args[0]);
    dst_len = PyByteArray_GET_SIZE(args[0]);
  } else {
    if (PyObject_GetBuffer(args[0], &buf, PyBUF_WRITABLE) < 0) return NULL;
    dst = (char *)buf.buf;
    dst_len = buf.len;
    have_buf = 1;
  }

  st = emq_pop_into(self->q, dst, (size_t)dst_len, &n, NULL, timeout_ms);
  if (have_buf) PyBuffer_Release(&buf);
  if (st == EMQ_ERR_INVALID) {
    PyErr_SetString(PyExc_ValueError, "buffer too small for message");
    return NULL;
  }
  if (st != EMQ_OK) return raise_emq(st);
  return PyLong_FromSsize_t((Py_ssize_t)n);
}

/* Amortize interpreter overhead via emq_push_n. */
static PyObject *Queue_push_repeat(QueueObject *self, PyObject *const *args,
                                   Py_ssize_t nargs) {
  const void *data;
  Py_ssize_t len;
  Py_buffer buf;
  int have_buf = 0;
  Py_ssize_t count;
  size_t pushed = 0;
  emq_status st;

  if (nargs != 2) {
    PyErr_SetString(PyExc_TypeError, "push_repeat(data, count)");
    return NULL;
  }
  count = PyLong_AsSsize_t(args[1]);
  if (count == -1 && PyErr_Occurred()) return NULL;
  if (count < 0) {
    PyErr_SetString(PyExc_ValueError, "count must be >= 0");
    return NULL;
  }

  if (PyBytes_Check(args[0])) {
    data = PyBytes_AS_STRING(args[0]);
    len = PyBytes_GET_SIZE(args[0]);
  } else if (PyByteArray_Check(args[0])) {
    data = PyByteArray_AS_STRING(args[0]);
    len = PyByteArray_GET_SIZE(args[0]);
  } else {
    if (PyObject_GetBuffer(args[0], &buf, PyBUF_SIMPLE) < 0) return NULL;
    data = buf.buf;
    len = buf.len;
    have_buf = 1;
  }

  if (count >= 16) {
    Py_BEGIN_ALLOW_THREADS
    st = emq_push_n(self->q, data, (size_t)len, (size_t)count, &pushed);
    Py_END_ALLOW_THREADS
  } else {
    st = emq_push_n(self->q, data, (size_t)len, (size_t)count, &pushed);
  }
  if (have_buf) PyBuffer_Release(&buf);
  if (st != EMQ_OK) return raise_emq(st);
  Py_RETURN_NONE;
}

/* Batch try-pop via emq_pop_into_n into a stride buffer. */
static PyObject *Queue_pop_copy_n(QueueObject *self, PyObject *const *args,
                                  Py_ssize_t nargs) {
  Py_ssize_t msg_cap;
  Py_ssize_t max_count;
  char *dst;
  Py_ssize_t dst_len;
  Py_buffer buf;
  int have_buf = 0;
  size_t count = 0;
  emq_status st;

  if (nargs != 3) {
    PyErr_SetString(PyExc_TypeError, "pop_copy_n(buf, msg_cap, max_count)");
    return NULL;
  }
  msg_cap = PyLong_AsSsize_t(args[1]);
  if (msg_cap == -1 && PyErr_Occurred()) return NULL;
  max_count = PyLong_AsSsize_t(args[2]);
  if (max_count == -1 && PyErr_Occurred()) return NULL;
  if (msg_cap <= 0 || max_count < 0) {
    PyErr_SetString(PyExc_ValueError, "msg_cap > 0 and max_count >= 0 required");
    return NULL;
  }

  if (PyByteArray_Check(args[0])) {
    dst = PyByteArray_AS_STRING(args[0]);
    dst_len = PyByteArray_GET_SIZE(args[0]);
  } else {
    if (PyObject_GetBuffer(args[0], &buf, PyBUF_WRITABLE) < 0) return NULL;
    dst = (char *)buf.buf;
    dst_len = buf.len;
    have_buf = 1;
  }
  if (dst_len < msg_cap * max_count) {
    if (have_buf) PyBuffer_Release(&buf);
    PyErr_Format(PyExc_ValueError, "buffer too small: need %zd have %zd",
                 msg_cap * max_count, dst_len);
    return NULL;
  }

  if (max_count >= 16) {
    Py_BEGIN_ALLOW_THREADS
    st = emq_pop_into_n(self->q, dst, (size_t)msg_cap, (size_t)max_count,
                        &count, NULL, 0);
    Py_END_ALLOW_THREADS
  } else {
    st = emq_pop_into_n(self->q, dst, (size_t)msg_cap, (size_t)max_count,
                        &count, NULL, 0);
  }
  if (have_buf) PyBuffer_Release(&buf);
  if (st != EMQ_OK && count == 0) return raise_emq(st);
  return PyLong_FromSsize_t((Py_ssize_t)count);
}

static PyObject *Queue_close(QueueObject *self, PyObject *Py_UNUSED(ignored)) {
  if (self->q) {
    emq_status st = emq_queue_close(self->q);
    self->q = NULL;
    if (st != EMQ_OK) return raise_emq(st);
  }
  Py_RETURN_NONE;
}

static PyMethodDef Queue_methods[] = {
    {"push", (PyCFunction)Queue_push, METH_FASTCALL,
     "Push bytes onto the queue."},
    {"push_repeat", (PyCFunction)Queue_push_repeat, METH_FASTCALL,
     "push_repeat(data, count): push same payload count times in one C call."},
    {"pop", (PyCFunction)Queue_pop, METH_FASTCALL,
     "Pop a Message (optional timeout_ms). Prefer pop_copy/pop_bytes hot paths."},
    {"pop_bytes", (PyCFunction)Queue_pop_bytes, METH_FASTCALL,
     "Pop payload as bytes (no Message object)."},
    {"pop_copy", (PyCFunction)Queue_pop_copy, METH_FASTCALL,
     "pop_copy(buf, timeout_ms=0) -> int; copy into writable buffer."},
    {"pop_copy_n", (PyCFunction)Queue_pop_copy_n, METH_FASTCALL,
     "pop_copy_n(buf, msg_cap, max_count) -> int; batch try-pop into stride buf."},
    {"close", (PyCFunction)Queue_close, METH_NOARGS, "Close the queue handle."},
    {NULL, NULL, 0, NULL}};

static PyTypeObject QueueType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "embeddedmq._emq.Queue",
    .tp_basicsize = sizeof(QueueObject),
    .tp_dealloc = (destructor)Queue_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = Queue_methods,
};

/* ------------------------------------------------------------------ */
/* Runtime                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
  PyObject_HEAD
  emq_runtime *rt;
} RuntimeObject;

static void Runtime_dealloc(RuntimeObject *self) {
  if (self->rt) {
    emq_runtime_destroy(self->rt);
    self->rt = NULL;
  }
  Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *Runtime_create_queue(RuntimeObject *self, PyObject *args) {
  const char *name;
  unsigned int capacity = 0;
  if (!PyArg_ParseTuple(args, "s|I", &name, &capacity)) return NULL;

  emq_queue_opts opts;
  emq_queue_opts_default(&opts);
  opts.capacity = capacity;
  opts.producers = 1;
  opts.consumers = 1;

  emq_queue *q = NULL;
  emq_status st = emq_queue_create(self->rt, name, &opts, &q);
  if (st != EMQ_OK) return raise_emq(st);

  QueueObject *qo = PyObject_New(QueueObject, &QueueType);
  if (!qo) return NULL;
  qo->rt = self->rt;
  qo->q = q;
  return (PyObject *)qo;
}

static PyMethodDef Runtime_methods[] = {
    {"create_queue", (PyCFunction)Runtime_create_queue, METH_VARARGS,
     "Create a queue by name (optional capacity)."},
    {NULL, NULL, 0, NULL}};

static PyTypeObject RuntimeType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "embeddedmq._emq.Runtime",
    .tp_basicsize = sizeof(RuntimeObject),
    .tp_dealloc = (destructor)Runtime_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = Runtime_methods,
};

static PyObject *Runtime_new(PyTypeObject *type, PyObject *args,
                             PyObject *kwds) {
  if (PyTuple_Size(args) != 0 || (kwds && PyDict_Size(kwds))) {
    PyErr_SetString(PyExc_TypeError, "Runtime() takes no arguments");
    return NULL;
  }
  emq_runtime *rt = NULL;
  emq_status st = emq_runtime_create(&rt);
  if (st != EMQ_OK) return raise_emq(st);
  RuntimeObject *self = (RuntimeObject *)type->tp_alloc(type, 0);
  if (!self) {
    emq_runtime_destroy(rt);
    return NULL;
  }
  self->rt = rt;
  return (PyObject *)self;
}

static PyTypeObject RuntimeTypeWithNew = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "embeddedmq._emq.Runtime",
    .tp_basicsize = sizeof(RuntimeObject),
    .tp_dealloc = (destructor)Runtime_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_methods = Runtime_methods,
    .tp_new = Runtime_new,
};

/* ------------------------------------------------------------------ */
/* Module                                                              */
/* ------------------------------------------------------------------ */

static PyObject *status_string_fn(PyObject *Py_UNUSED(self), PyObject *args) {
  int code = 0;
  if (!PyArg_ParseTuple(args, "i", &code)) return NULL;
  const char *s = emq_status_string((emq_status)code);
  return PyUnicode_FromString(s ? s : "unknown");
}

static PyMethodDef module_methods[] = {
    {"status_string", status_string_fn, METH_VARARGS, "Map status code to string."},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef emqmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "embeddedmq._emq",
    .m_doc = "Low-level EmbeddedMQ C extension.",
    .m_size = -1,
    .m_methods = module_methods,
};

PyMODINIT_FUNC PyInit__emq(void) {
  PyObject *m;
  if (PyType_Ready(&MessageType) < 0) return NULL;
  if (PyType_Ready(&QueueType) < 0) return NULL;
  if (PyType_Ready(&RuntimeTypeWithNew) < 0) return NULL;

  m = PyModule_Create(&emqmodule);
  if (!m) return NULL;

  EmqError = PyErr_NewException("embeddedmq.EmqError", NULL, NULL);
  Py_INCREF(EmqError);
  PyModule_AddObject(m, "EmqError", EmqError);

  PyModule_AddIntConstant(m, "EMQ_OK", EMQ_OK);
  PyModule_AddIntConstant(m, "EMQ_ERR_EMPTY", EMQ_ERR_EMPTY);
  PyModule_AddIntConstant(m, "EMQ_ERR_FULL", EMQ_ERR_FULL);

  Py_INCREF(&RuntimeTypeWithNew);
  PyModule_AddObject(m, "Runtime", (PyObject *)&RuntimeTypeWithNew);
  Py_INCREF(&QueueType);
  PyModule_AddObject(m, "Queue", (PyObject *)&QueueType);
  Py_INCREF(&MessageType);
  PyModule_AddObject(m, "Message", (PyObject *)&MessageType);

  return m;
}
