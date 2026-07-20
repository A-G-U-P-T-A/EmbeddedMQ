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
go get github.com/A-G-U-P-T-A/EmbeddedMQ/bindings/go@v1.0.0-beta.3
```

Licensed under Apache-2.0 (see repo root `LICENSE`).

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

	payload := []byte("hello")
	_ = q.Push(payload)

	// Hot path: emq_pop_into (no engine malloc), one cgo call
	dst := make([]byte, len(payload))
	n, err := q.PopCopy(dst, 0)
	if err != nil {
		panic(err)
	}
	fmt.Println(string(dst[:n]))

	// Throughput: emq_push_n + emq_pop_into_n
	// _ = q.PushRepeat(payload, 32)
	// got, _ := q.PopCopyN(batchDst, len(payload), 32)
}
```

Prefer `PopCopy` / `PushRepeat`+`PopCopyN`. `Pop` + `Message.Data()` allocates —
convenience only. Always `Message.Release()` after `Pop`.

## Optional: link a prebuilt libemq

```bash
EMQ_INCLUDE=../../core/include EMQ_LIB=../../build go build -tags emq_system .
```
