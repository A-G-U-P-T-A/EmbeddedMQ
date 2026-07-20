# emq (Go)

cgo bindings for EmbeddedMQ. Layout follows the same idea as
[mattn/go-sqlite3](https://github.com/mattn/go-sqlite3): the module ships C
sources and compiles them with cgo — no separate `libemq` install.

```text
go/
  go.mod
  emq.go              # public API
  zz_*.c              # AUTO-GENERATED cgo TUs (from scripts/sync_native.py)
  ../native/          # engine sources + public headers
```

Module path: `github.com/A-G-U-P-T-A/EmbeddedMQ/bindings/go`

## Install / build

Requires Go 1.21+, a C compiler, and `CGO_ENABLED=1`.

```bash
# from a clone of this monorepo
cd bindings/go
go build ./...
```

From another module (replace until a tagged release is consumed via the proxy):

```bash
go get github.com/A-G-U-P-T-A/EmbeddedMQ/bindings/go@v1.0.0-beta
```

## Usage

```go
package main

import (
	"fmt"
	emq "github.com/A-G-U-P-T-A/EmbeddedMQ/bindings/go"
)

func main() {
	rt, err := emq.NewRuntime()
	if err != nil {
		panic(err)
	}
	defer rt.Close()

	q, err := rt.CreateQueue("demo", &emq.QueueOpts{Capacity: 64, Producers: 1, Consumers: 1})
	if err != nil {
		panic(err)
	}
	defer q.Close()

	_ = q.Push([]byte("hello"))
	msg, err := q.Pop(0)
	if err != nil {
		panic(err)
	}
	defer msg.Release()
	fmt.Println(string(msg.Bytes()))
}
```

Always call `Message.Release()` after a successful pop (ownership matches `emq_message_release`).

## Optional: link a prebuilt libemq

```bash
EMQ_INCLUDE=../../core/include EMQ_LIB=../../build go build -tags emq_system .
```
