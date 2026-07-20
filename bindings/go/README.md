# Go bindings

cgo module at `github.com/A-G-U-P-T-A/EmbeddedMQ/bindings/go`.

## Prerequisites

Build static `libemq`:

```bash
cmake -S core -B build -DEMQ_BUILD_TESTS=OFF
cmake --build build
```

## Build

cgo reads include and library paths from environment variables:

```bash
export EMQ_INCLUDE=$PWD/../../core/include
export EMQ_LIB=$PWD/../../build
go build .
```

On Windows (PowerShell):

```powershell
$env:EMQ_INCLUDE = "$PWD\..\..\core\include"
$env:EMQ_LIB = "$PWD\..\..\build"
go build .
```

## Usage

```go
rt, err := emq.NewRuntime()
q, err := rt.CreateQueue("demo", &emq.QueueOpts{Capacity: 64, Producers: 1, Consumers: 1})
err = q.Push([]byte("hello"))
msg, err := q.Pop(0)
defer msg.Release()
_ = q.Close()
_ = rt.Close()
```

Always call `Message.Release()` (or rely on explicit pop ownership) to free payloads via `emq_message_release`.

## Notes

- Links `libemq` statically from `EMQ_LIB`.
- SPSC contract: use `Producers: 1, Consumers: 1` for single-threaded push/pop pairs.
