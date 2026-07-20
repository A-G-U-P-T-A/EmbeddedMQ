package io.embeddedmq.examples;

import io.embeddedmq.Emq;

import java.lang.foreign.Arena;
import java.lang.foreign.ValueLayout;
import java.util.Arrays;

/**
 * Local JAR load test: scalar {@code popCopy} (emq_pop_into) and batch modes,
 * reporting median over {@code EMQ_LOAD_TRIALS}.
 */
public final class LoadTest {
    public static void main(String[] args) {
        int n = Integer.parseInt(System.getenv().getOrDefault("EMQ_LOAD_N", "100000"));
        int payloadLen = Integer.parseInt(System.getenv().getOrDefault("EMQ_LOAD_PAYLOAD", "64"));
        int batch = Integer.parseInt(System.getenv().getOrDefault("EMQ_LOAD_BATCH", "32"));
        int trials = Math.max(1, Integer.parseInt(System.getenv().getOrDefault("EMQ_LOAD_TRIALS", "5")));
        int capacity = Math.max(n + 16, 1024);
        byte[] payload = new byte[payloadLen];
        for (int i = 0; i < payloadLen; i++) {
            payload[i] = (byte) (i % 256);
        }
        byte[] dst = new byte[payloadLen];
        byte[] batchDst = new byte[payloadLen * batch];

        System.out.printf(
                "client=java n=%d payload=%d capacity=%d batch=%d trials=%d%n",
                n, payloadLen, capacity, batch, trials);

        try (Emq emq = new Emq();
             Emq.Queue q = emq.openQueue("loadtest-java", capacity);
             Arena arena = Arena.ofConfined()) {
            var nativePayload = arena.allocateFrom(ValueLayout.JAVA_BYTE, payload);

            for (int i = 0; i < 20_000; i++) {
                q.pushNative(nativePayload, payloadLen);
            }
            for (int i = 0; i < 20_000; i++) {
                q.popCopy(dst, 1000);
            }

            double[] samples = new double[trials];
            for (int t = 0; t < trials; t++) {
                long t0 = System.nanoTime();
                for (int i = 0; i < n; i++) {
                    q.pushNative(nativePayload, payloadLen);
                }
                long t1 = System.nanoTime();
                for (int i = 0; i < n; i++) {
                    int got = q.popCopy(dst, 1000);
                    if (got != payloadLen) {
                        throw new IllegalStateException("bad len " + got);
                    }
                }
                long t2 = System.nanoTime();
                samples[t] = report("scalar_pop_into", n, t0, t1, t2);
            }
            System.out.printf(
                    "MEDIAN lang=java mode=scalar_pop_into roundtrip_ops=%.0f/s trials=%d%n",
                    median(samples), trials);

            for (int t = 0; t < trials; t++) {
                long t0 = System.nanoTime();
                for (int left = n; left > 0; ) {
                    int chunk = Math.min(batch, left);
                    q.pushRepeat(payload, chunk);
                    left -= chunk;
                }
                long t1 = System.nanoTime();
                for (int left = n; left > 0; ) {
                    int chunk = Math.min(batch, left);
                    int got = q.popCopyN(batchDst, payloadLen, chunk);
                    if (got != chunk) {
                        throw new IllegalStateException("bad batch " + got);
                    }
                    left -= got;
                }
                long t2 = System.nanoTime();
                samples[t] = report("batch_pop_into_n_b" + batch, n, t0, t1, t2);
            }
            System.out.printf(
                    "MEDIAN lang=java mode=batch_pop_into_n roundtrip_ops=%.0f/s trials=%d%n",
                    median(samples), trials);
        }
    }

    private static double report(String mode, int n, long t0, long t1, long t2) {
        double pushS = (t1 - t0) / 1e9;
        double popS = (t2 - t1) / 1e9;
        double totalS = (t2 - t0) / 1e9;
        System.out.printf(
                "RESULT lang=java mode=%s push_ops=%.0f/s pop_ops=%.0f/s roundtrip_ops=%.0f/s push_ms=%.1f pop_ms=%.1f total_ms=%.1f%n",
                mode,
                n / pushS,
                n / popS,
                n / totalS,
                pushS * 1000,
                popS * 1000,
                totalS * 1000);
        return n / totalS;
    }

    private static double median(double[] samples) {
        double[] copy = Arrays.copyOf(samples, samples.length);
        Arrays.sort(copy);
        return copy[copy.length / 2];
    }

    private LoadTest() {}
}
