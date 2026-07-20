package io.embeddedmq;

/**
 * In-binding load / bottleneck test (run with surefire or {@code exec:java}).
 *
 * <pre>
 *   mvn -q -DskipTests package
 *   mvn -q exec:java -Dexec.classpathScope=test \
 *     -Dexec.mainClass=io.embeddedmq.QueueLoadTest \
 *     -Dexec.jvmArgs=--enable-native-access=ALL-UNNAMED
 * </pre>
 */
public final class QueueLoadTest {
    public static void main(String[] args) {
        int n = Integer.parseInt(System.getProperty("emq.load.n",
                System.getenv().getOrDefault("EMQ_LOAD_N", "100000")));
        int payloadLen = Integer.parseInt(System.getProperty("emq.load.payload",
                System.getenv().getOrDefault("EMQ_LOAD_PAYLOAD", "64")));
        int warmup = Integer.parseInt(System.getProperty("emq.load.warmup", "20000"));
        int capacity = Math.max(n + 16, 1024);

        byte[] payload = new byte[payloadLen];
        for (int i = 0; i < payloadLen; i++) {
            payload[i] = (byte) (i % 256);
        }
        byte[] dst = new byte[payloadLen];

        System.out.printf(
                "java-load n=%d warmup=%d payload=%d capacity=%d%n",
                n, warmup, payloadLen, capacity);

        try (Emq emq = new Emq(); Emq.Queue q = emq.openQueue("java-load", capacity)) {
            // Warmup (JIT + FFM critical stubs).
            runPushPop(q, payload, dst, warmup, false);

            // Bottleneck A: legacy Message + data() copy (old API shape).
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
            print("legacy_Message+data()", n, t0, t1, t2);

            // Bottleneck B: push + popCopy (no Message alloc).
            t0 = System.nanoTime();
            for (int i = 0; i < n; i++) {
                q.push(payload);
            }
            t1 = System.nanoTime();
            for (int i = 0; i < n; i++) {
                int got = q.popCopy(dst, 1000);
                if (got != payloadLen) {
                    throw new IllegalStateException("bad len " + got);
                }
            }
            t2 = System.nanoTime();
            print("push+popCopy", n, t0, t1, t2);

            // Bottleneck C: pushNative from a stable segment + popCopy.
            try (var arena = java.lang.foreign.Arena.ofConfined()) {
                var nativePayload = arena.allocateFrom(
                        java.lang.foreign.ValueLayout.JAVA_BYTE, payload);
                t0 = System.nanoTime();
                for (int i = 0; i < n; i++) {
                    q.pushNative(nativePayload, payloadLen);
                }
                t1 = System.nanoTime();
                for (int i = 0; i < n; i++) {
                    q.popCopy(dst, 1000);
                }
                t2 = System.nanoTime();
                print("pushNative+popCopy", n, t0, t1, t2);
            }
        }
    }

    private static void runPushPop(Emq.Queue q, byte[] payload, byte[] dst, int n, boolean check) {
        for (int i = 0; i < n; i++) {
            q.push(payload);
        }
        for (int i = 0; i < n; i++) {
            int got = q.popCopy(dst, 1000);
            if (check && got != payload.length) {
                throw new IllegalStateException("bad len");
            }
        }
    }

    private static void print(String label, int n, long t0, long t1, long t2) {
        double pushS = (t1 - t0) / 1e9;
        double popS = (t2 - t1) / 1e9;
        double totalS = (t2 - t0) / 1e9;
        System.out.printf(
                "RESULT mode=%s push_ops=%.0f/s pop_ops=%.0f/s roundtrip_ops=%.0f/s push_ms=%.1f pop_ms=%.1f total_ms=%.1f%n",
                label,
                n / pushS,
                n / popS,
                n / totalS,
                pushS * 1000,
                popS * 1000,
                totalS * 1000);
    }

    private QueueLoadTest() {}
}
