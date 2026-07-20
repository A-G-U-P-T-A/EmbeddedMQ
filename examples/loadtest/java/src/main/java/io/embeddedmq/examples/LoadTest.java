package io.embeddedmq.examples;

import io.embeddedmq.Emq;

/**
 * Maven Central embeddedmq queue load test.
 */
public final class LoadTest {
    public static void main(String[] args) {
        int n = Integer.parseInt(System.getenv().getOrDefault("EMQ_LOAD_N", "100000"));
        int payloadLen = Integer.parseInt(System.getenv().getOrDefault("EMQ_LOAD_PAYLOAD", "64"));
        int capacity = Math.max(n + 16, 1024);
        byte[] payload = new byte[payloadLen];
        for (int i = 0; i < payloadLen; i++) {
            payload[i] = (byte) (i % 256);
        }

        System.out.printf("client=java n=%d payload=%d capacity=%d%n", n, payloadLen, capacity);

        try (Emq emq = new Emq(); Emq.Queue q = emq.openQueue("loadtest-java", capacity)) {
            long t0 = System.nanoTime();
            for (int i = 0; i < n; i++) {
                q.push(payload);
            }
            long t1 = System.nanoTime();

            for (int i = 0; i < n; i++) {
                try (Emq.Message msg = q.pop(1000)) {
                    msg.data();
                }
            }
            long t2 = System.nanoTime();

            double pushS = (t1 - t0) / 1e9;
            double popS = (t2 - t1) / 1e9;
            double totalS = (t2 - t0) / 1e9;
            System.out.printf(
                    "RESULT lang=java push_ops=%.0f/s pop_ops=%.0f/s roundtrip_ops=%.0f/s push_ms=%.1f pop_ms=%.1f total_ms=%.1f%n",
                    n / pushS,
                    n / popS,
                    n / totalS,
                    pushS * 1000,
                    popS * 1000,
                    totalS * 1000);
        }
    }

    private LoadTest() {}
}
