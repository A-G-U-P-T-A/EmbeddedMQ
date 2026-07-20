"""EmbeddedMQ Python bindings (CPython C extension)."""

from embeddedmq._emq import (
    EMQ_ERR_EMPTY,
    EMQ_ERR_FULL,
    EMQ_OK,
    Message,
    Queue,
    Runtime,
    status_string,
)

__all__ = [
    "EMQ_OK",
    "EMQ_ERR_EMPTY",
    "EMQ_ERR_FULL",
    "Runtime",
    "Queue",
    "Message",
    "status_string",
]

__version__ = "0.1.0"
