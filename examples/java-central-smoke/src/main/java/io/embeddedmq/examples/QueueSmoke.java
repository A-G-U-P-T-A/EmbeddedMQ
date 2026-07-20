package io.embeddedmq.examples;

import io.embeddedmq.Emq;

import java.nio.charset.StandardCharsets;

/**
 * Resolves {@code io.github.a-g-u-p-t-a:embeddedmq} from Maven Central,
 * loads the bundled native, and runs a basic queue push/pop round-trip.
 */
public final class QueueSmoke {
    public static void main(String[] args) {
        System.out.println("EmbeddedMQ Central smoke");
        System.out.println("java.version=" + System.getProperty("java.version"));
        System.out.println("os=" + System.getProperty("os.name") + " / "
                + System.getProperty("os.arch"));

        try (Emq emq = new Emq();
             Emq.Queue orders = emq.openQueue("orders", 64)) {

            String[] payloads = {
                    "hello-from-maven-central",
                    "queue-roundtrip-1",
                    "queue-roundtrip-2"
            };

            for (String text : payloads) {
                orders.push(text.getBytes(StandardCharsets.UTF_8));
                System.out.println("pushed: " + text);
            }

            for (int i = 0; i < payloads.length; i++) {
                try (Emq.Message msg = orders.pop(1000)) {
                    String got = new String(msg.data(), StandardCharsets.UTF_8);
                    System.out.println("popped: " + got);
                    if (!got.equals(payloads[i])) {
                        throw new IllegalStateException(
                                "payload mismatch: expected=" + payloads[i] + " got=" + got);
                    }
                }
            }

            try {
                orders.pop(50);
                throw new IllegalStateException("expected empty/timeout on fourth pop");
            } catch (Emq.EmqException empty) {
                System.out.println("empty/timeout as expected: emq_status=" + empty.code);
            }

            System.out.println("OK — queue push/pop via Maven Central JAR");
        }
    }

    private QueueSmoke() {}
}
