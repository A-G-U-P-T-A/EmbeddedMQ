package main

import (
	"fmt"
	"os"
	"strconv"
	"time"

	emq "github.com/A-G-U-P-T-A/EmbeddedMQ/bindings/go"
)

func envInt(key string, def int) int {
	v := os.Getenv(key)
	if v == "" {
		return def
	}
	n, err := strconv.Atoi(v)
	if err != nil {
		return def
	}
	return n
}

func main() {
	n := envInt("EMQ_LOAD_N", 100000)
	payloadLen := envInt("EMQ_LOAD_PAYLOAD", 64)
	capacity := uint32(n + 16)
	if capacity < 1024 {
		capacity = 1024
	}
	payload := make([]byte, payloadLen)
	for i := range payload {
		payload[i] = byte(i % 256)
	}

	fmt.Printf("client=go n=%d payload=%d capacity=%d\n", n, payloadLen, capacity)

	rt, err := emq.NewRuntime()
	if err != nil {
		panic(err)
	}
	defer rt.Close()

	q, err := rt.CreateQueue("loadtest-go", &emq.QueueOpts{
		Capacity:  capacity,
		Producers: 1,
		Consumers: 1,
	})
	if err != nil {
		panic(err)
	}
	defer q.Close()

	t0 := time.Now()
	for i := 0; i < n; i++ {
		if err := q.Push(payload); err != nil {
			panic(err)
		}
	}
	t1 := time.Now()

	for i := 0; i < n; i++ {
		msg, err := q.Pop(1000)
		if err != nil {
			panic(err)
		}
		_ = msg.Data()
		msg.Release()
	}
	t2 := time.Now()

	pushS := t1.Sub(t0).Seconds()
	popS := t2.Sub(t1).Seconds()
	totalS := t2.Sub(t0).Seconds()
	fmt.Printf(
		"RESULT lang=go push_ops=%.0f/s pop_ops=%.0f/s roundtrip_ops=%.0f/s push_ms=%.1f pop_ms=%.1f total_ms=%.1f\n",
		float64(n)/pushS,
		float64(n)/popS,
		float64(n)/totalS,
		pushS*1000,
		popS*1000,
		totalS*1000,
	)
}
