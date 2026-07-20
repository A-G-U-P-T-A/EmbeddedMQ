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

func report(mode string, n int, t0, t1, t2 time.Time) {
	pushS := t1.Sub(t0).Seconds()
	popS := t2.Sub(t1).Seconds()
	totalS := t2.Sub(t0).Seconds()
	fmt.Printf(
		"RESULT lang=go mode=%s push_ops=%.0f/s pop_ops=%.0f/s roundtrip_ops=%.0f/s push_ms=%.1f pop_ms=%.1f total_ms=%.1f\n",
		mode,
		float64(n)/pushS,
		float64(n)/popS,
		float64(n)/totalS,
		pushS*1000,
		popS*1000,
		totalS*1000,
	)
}

func median(samples []float64) float64 {
	sort.Float64s(samples)
	return samples[len(samples)/2]
}

func main() {
	n := envInt("EMQ_LOAD_N", 100000)
	payloadLen := envInt("EMQ_LOAD_PAYLOAD", 64)
	warmup := envInt("EMQ_LOAD_WARMUP", 20000)
	batch := envInt("EMQ_LOAD_BATCH", 32)
	trials := envInt("EMQ_LOAD_TRIALS", 5)
	if trials < 1 {
		trials = 1
	}
	capacity := uint32(n + 16)
	if capacity < 1024 {
		capacity = 1024
	}
	payload := make([]byte, payloadLen)
	for i := range payload {
		payload[i] = byte(i % 256)
	}
	dst := make([]byte, payloadLen)
	batchDst := make([]byte, payloadLen*batch)

	fmt.Printf("client=go n=%d payload=%d capacity=%d batch=%d trials=%d\n", n, payloadLen, capacity, batch, trials)

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

	for i := 0; i < warmup; i++ {
		_ = q.Push(payload)
	}
	for i := 0; i < warmup; i++ {
		_, _ = q.PopCopy(dst, 1000)
	}

	samples := make([]float64, trials)

	for t := 0; t < trials; t++ {
		t0 := time.Now()
		for i := 0; i < n; i++ {
			if err := q.Push(payload); err != nil {
				panic(err)
			}
		}
		t1 := time.Now()
		for i := 0; i < n; i++ {
			got, err := q.PopCopy(dst, 1000)
			if err != nil {
				panic(err)
			}
			if got != payloadLen {
				panic(fmt.Sprintf("bad len %d", got))
			}
		}
		t2 := time.Now()
		report("scalar_pop_into", n, t0, t1, t2)
		samples[t] = float64(n) / t2.Sub(t0).Seconds()
	}
	fmt.Printf("MEDIAN lang=go mode=scalar_pop_into roundtrip_ops=%.0f/s trials=%d\n", median(samples), trials)

	for t := 0; t < trials; t++ {
		t0 := time.Now()
		for left := n; left > 0; {
			chunk := batch
			if chunk > left {
				chunk = left
			}
			if err := q.PushRepeat(payload, chunk); err != nil {
				panic(err)
			}
			left -= chunk
		}
		t1 := time.Now()
		for left := n; left > 0; {
			chunk := batch
			if chunk > left {
				chunk = left
			}
			got, err := q.PopCopyN(batchDst, payloadLen, chunk)
			if err != nil {
				panic(err)
			}
			if got != chunk {
				panic(fmt.Sprintf("bad batch pop %d want %d", got, chunk))
			}
			left -= got
		}
		t2 := time.Now()
		report(fmt.Sprintf("batch_pop_into_n_b%d", batch), n, t0, t1, t2)
		samples[t] = float64(n) / t2.Sub(t0).Seconds()
	}
	fmt.Printf("MEDIAN lang=go mode=batch_pop_into_n roundtrip_ops=%.0f/s trials=%d\n", median(samples), trials)
}
