package io.embeddedmq;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.Objects;

/**
 * Minimal Panama FFM scaffold for libemq.
 * <p>
 * This is <strong>not</strong> JNI — it uses {@link java.lang.foreign} (JDK 21+).
 * Build libemq first, then set {@code -Demq.lib.path=/path/to/build} or place
 * the native library on {@code java.library.path}.
 */
public final class Emq implements AutoCloseable {

    public static final int EMQ_OK = 0;
    public static final int EMQ_ERR_EMPTY = -5;
    public static final int EMQ_ERR_FULL = -4;

    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LOOKUP = SymbolLookup.libraryLookup(
            resolveLibrary(), Arena.global());

    private static final MethodHandle MH_RUNTIME_CREATE = downcall("emq_runtime_create",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS)); // emq_runtime**

    private static final MethodHandle MH_RUNTIME_DESTROY = downcall("emq_runtime_destroy",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    private static final MethodHandle MH_QUEUE_OPTS_DEFAULT = downcall("emq_queue_opts_default",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    private static final MethodHandle MH_QUEUE_CREATE = downcall("emq_queue_create",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS, // runtime
                    ValueLayout.ADDRESS, // name
                    ValueLayout.ADDRESS, // opts
                    ValueLayout.ADDRESS)); // queue**

    private static final MethodHandle MH_QUEUE_CLOSE = downcall("emq_queue_close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    private static final MethodHandle MH_PUSH = downcall("emq_push",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG,
                    ValueLayout.ADDRESS));

    private static final MethodHandle MH_POP = downcall("emq_pop",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT));

    private static final MethodHandle MH_MESSAGE_RELEASE = downcall("emq_message_release",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    private static final long OPTS_SIZE = 15 * 4L + ValueLayout.ADDRESS.byteSize(); // rough emq_queue_opts
    private static final long MSG_SIZE = 8 + 8 + 4 + 8 + 8 + ValueLayout.ADDRESS.byteSize()
            + ValueLayout.JAVA_LONG.byteSize() + 4;

    private final Arena arena = Arena.ofConfined();
    private final MemorySegment runtime;
    private boolean closed;

    public Emq() {
        MemorySegment rtOut = arena.allocate(ValueLayout.ADDRESS);
        int status = invokeInt(MH_RUNTIME_CREATE, rtOut);
        EmqException.check(status);
        runtime = rtOut.get(ValueLayout.ADDRESS, 0);
    }

    public Queue openQueue(String name, int capacity) {
        Objects.requireNonNull(name);
        MemorySegment cName = arena.allocateFrom(name, StandardCharsets.UTF_8);
        MemorySegment opts = arena.allocate(OPTS_SIZE);
        invokeVoid(MH_QUEUE_OPTS_DEFAULT, opts);
        opts.setAtIndex(ValueLayout.JAVA_INT, 4, capacity); // capacity field offset (approx)
        opts.setAtIndex(ValueLayout.JAVA_INT, 9, 1); // producers
        opts.setAtIndex(ValueLayout.JAVA_INT, 10, 1); // consumers

        MemorySegment qOut = arena.allocate(ValueLayout.ADDRESS);
        int status = invokeInt(MH_QUEUE_CREATE, runtime, cName, opts, qOut);
        EmqException.check(status);
        MemorySegment queue = qOut.get(ValueLayout.ADDRESS, 0);
        return new Queue(queue);
    }

    @Override
    public void close() {
        if (!closed) {
            invokeVoid(MH_RUNTIME_DESTROY, runtime);
            closed = true;
            arena.close();
        }
    }

    public final class Queue implements AutoCloseable {
        private final MemorySegment handle;
        private boolean queueClosed;

        Queue(MemorySegment handle) {
            this.handle = handle;
        }

        public void push(byte[] data) {
            MemorySegment payload = data.length == 0
                    ? MemorySegment.NULL
                    : arena.allocateFrom(ValueLayout.JAVA_BYTE, data);
            int status = invokeInt(MH_PUSH, handle, payload, (long) data.length, MemorySegment.NULL);
            EmqException.check(status);
        }

        public Message pop(int timeoutMs) {
            MemorySegment msg = arena.allocate(MSG_SIZE);
            int status = invokeInt(MH_POP, handle, msg, timeoutMs);
            EmqException.check(status);
            return new Message(msg);
        }

        @Override
        public void close() {
            if (!queueClosed) {
                EmqException.check(invokeInt(MH_QUEUE_CLOSE, handle));
                queueClosed = true;
            }
        }
    }

    public final class Message implements AutoCloseable {
        private final MemorySegment segment;
        private boolean released;

        Message(MemorySegment segment) {
            this.segment = segment;
        }

        public byte[] data() {
            MemorySegment ptr = segment.get(ValueLayout.ADDRESS, 40); // data offset (platform-dependent scaffold)
            long size = segment.get(ValueLayout.JAVA_LONG, 48);
            if (ptr == MemorySegment.NULL || size == 0) {
                return new byte[0];
            }
            return ptr.reinterpret(size).toArray(ValueLayout.JAVA_BYTE);
        }

        @Override
        public void close() {
            if (!released) {
                invokeVoid(MH_MESSAGE_RELEASE, segment);
                released = true;
            }
        }
    }

    private static Path resolveLibrary() {
        String prop = System.getProperty("emq.lib.path");
        if (prop != null) {
            return Path.of(prop);
        }
        return Path.of(System.getProperty("user.dir"), "..", "..", "build");
    }

    private static MethodHandle downcall(String name, FunctionDescriptor desc) {
        MemorySegment sym = LOOKUP.find(name)
                .orElseThrow(() -> new UnsatisfiedLinkError("missing symbol: " + name));
        return LINKER.downcallHandle(sym, desc);
    }

    private static int invokeInt(MethodHandle mh, Object... args) {
        try {
            return (int) mh.invokeWithArguments(args);
        } catch (Throwable t) {
            throw new RuntimeException(t);
        }
    }

    private static void invokeVoid(MethodHandle mh, Object... args) {
        try {
            mh.invokeWithArguments(args);
        } catch (Throwable t) {
            throw new RuntimeException(t);
        }
    }

    public static final class EmqException extends RuntimeException {
        public final int code;

        EmqException(int code) {
            super("emq_status=" + code);
            this.code = code;
        }

        static void check(int code) {
            if (code != EMQ_OK) {
                throw new EmqException(code);
            }
        }
    }
}
