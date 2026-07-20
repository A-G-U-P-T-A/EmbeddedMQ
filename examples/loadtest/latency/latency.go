package main

import (
	"fmt"
	"os"
	"sort"
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

func pct(sorted []int64, p float64) int64 {
	if len(sorted) == 0 {
		return 0
	}
	idx := int(p*float64(len(sorted)-1) + 0.5)
	if idx >= len(sorted) {
		idx = len(sorted) - 1
	}
	return sorted[idx]
}

func main() {
	n := envInt("EMQ_LOAD_N", 1000000)
	payloadLen := envInt("EMQ_LOAD_PAYLOAD", 64)
	warmup := envInt("EMQ_LOAD_WARMUP", 50000)
	payload := make([]byte, payloadLen)
	for i := range payload {
		payload[i] = 0xAB
	}
	dst := make([]byte, payloadLen)
	lat := make([]int64, n)

	rt, err := emq.NewRuntime()
	if err != nil {
		panic(err)
	}
	defer rt.Close()
	q, err := rt.CreateQueue("lat-go", &emq.QueueOpts{
		Capacity: 4096, Producers: 1, Consumers: 1,
	})
	if err != nil {
		panic(err)
	}
	defer q.Close()

	for i := 0; i < warmup; i++ {
		_ = q.Push(payload)
		_, _ = q.PopCopy(dst, 0)
	}

	for i := 0; i < n; i++ {
		t0 := time.Now()
		if err := q.Push(payload); err != nil {
			panic(err)
		}
		if _, err := q.PopCopy(dst, 0); err != nil {
			panic(err)
		}
		lat[i] = time.Since(t0).Nanoseconds()
	}

	sort.Slice(lat, func(i, j int) bool { return lat[i] < lat[j] })
	fmt.Printf("LATENCY lang=go payload=%d n=%d p50_ns=%d p99_ns=%d p999_ns=%d p9999_ns=%d\n",
		payloadLen, n, pct(lat, 0.50), pct(lat, 0.99), pct(lat, 0.999), pct(lat, 0.9999))
}
