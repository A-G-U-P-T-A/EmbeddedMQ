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

static PyObject *Queue_push(QueueObject *self, PyObject *args) {
  Py_buffer buf;
  if (!PyArg_ParseTuple(args, "y*", &buf)) return NULL;
  emq_status st =
      emq_push(self->q, buf.buf, (size_t)buf.len, NULL);
  PyBuffer_Release(&buf);
  if (st != EMQ_OK) return raise_emq(st);
  Py_RETURN_NONE;
}

static PyObject *Queue_pop(QueueObject *self, PyObject *args) {
  unsigned int timeout_ms = 0;
  if (!PyArg_ParseTuple(args, "|I", &timeout_ms)) return NULL;
  emq_message msg;
  memset(&msg, 0, sizeof(msg));
  emq_status st = emq_pop(self->q, &msg, timeout_ms);
  if (st != EMQ_OK) return raise_emq(st);
  return (PyObject *)message_from_emq(&msg);
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
    {"push", (PyCFunction)Queue_push, METH_VARARGS, "Push bytes onto the queue."},
    {"pop", (PyCFunction)Queue_pop, METH_VARARGS, "Pop a message (optional timeout_ms)."},
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
