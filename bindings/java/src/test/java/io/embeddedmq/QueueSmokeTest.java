package io.embeddedmq;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

/** Functional + minimum throughput guard for the Java binding. */
class QueueSmokeTest {

    @Test
    void pushPopCopyRoundTrip() {
        byte[] payload = "hello-emq".getBytes();
        byte[] dst = new byte[payload.length];
        try (Emq emq = new Emq(); Emq.Queue q = emq.openQueue("smoke", 32)) {
            q.push(payload);
            assertEquals(payload.length, q.popCopy(dst, 1000));
            assertArrayEquals(payload, dst);
        }
    }

    @Test
    void loadFloorPushNativePopCopy() {
        final int n = 50_000;
        final int payloadLen = 64;
        byte[] payload = new byte[payloadLen];
        byte[] dst = new byte[payloadLen];
        for (int i = 0; i < payloadLen; i++) {
            payload[i] = (byte) i;
        }

        try (Emq emq = new Emq();
             Emq.Queue q = emq.openQueue("floor", n + 16);
             var arena = java.lang.foreign.Arena.ofConfined()) {
            var nativePayload = arena.allocateFrom(
                    java.lang.foreign.ValueLayout.JAVA_BYTE, payload);

            // warmup
            for (int i = 0; i < 5_000; i++) {
                q.pushNative(nativePayload, payloadLen);
            }
            for (int i = 0; i < 5_000; i++) {
                q.popCopy(dst, 1000);
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < n; i++) {
                q.pushNative(nativePayload, payloadLen);
            }
            for (int i = 0; i < n; i++) {
                q.popCopy(dst, 1000);
            }
            long t1 = System.nanoTime();
            double ops = n / ((t1 - t0) / 1e9);
            // Floor: must beat the broken Central beta.3 ~0.45M by a wide margin.
            // CI hosts vary; 1.5M round-trip is a conservative gate after optimizations.
            assertTrue(ops > 1_500_000.0,
                    "roundtrip ops/s too low: " + ops + " (expected > 1.5M)");
        }
    }
}
