package io.embeddedmq;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.StructLayout;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.VarHandle;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.Objects;

/**
 * Panama FFM bindings for libemq (JDK 22+).
 * <p>
 * Natives are loaded like sqlite-jdbc: the JAR ships
 * {@code /native/<os>/<arch>/libemq.*}; {@link NativeLoader} extracts and loads
 * the matching library. Override with {@code -Demq.lib.path=...} for local builds.
 */
public final class Emq implements AutoCloseable {

    public static final int EMQ_OK = 0;
    public static final int EMQ_ERR_FULL = -4;
    public static final int EMQ_ERR_EMPTY = -5;
    public static final int EMQ_ERR_TIMEOUT = -7;

    /** Matches {@code emq_queue_opts} on LP64 / Windows x64. */
    private static final StructLayout QUEUE_OPTS = MemoryLayout.structLayout(
            ValueLayout.JAVA_INT.withName("storage"),
            ValueLayout.JAVA_INT.withName("policy"),
            ValueLayout.JAVA_INT.withName("delivery"),
            ValueLayout.JAVA_INT.withName("fsync"),
            ValueLayout.JAVA_INT.withName("capacity"),
            ValueLayout.JAVA_INT.withName("visibility_ms"),
            ValueLayout.JAVA_INT.withName("inline_threshold"),
            ValueLayout.JAVA_INT.withName("ring_size"),
            ValueLayout.ADDRESS.withName("path"),
            ValueLayout.JAVA_INT.withName("producers"),
            ValueLayout.JAVA_INT.withName("consumers"),
            ValueLayout.JAVA_INT.withName("backpressure"),
            ValueLayout.JAVA_INT.withName("high_watermark"),
            ValueLayout.JAVA_INT.withName("low_watermark"));

    /** Matches {@code emq_message} on LP64 / Windows x64. */
    private static final StructLayout MESSAGE = MemoryLayout.structLayout(
            ValueLayout.JAVA_LONG.withName("id"),
            ValueLayout.JAVA_LONG.withName("offset"),
            ValueLayout.JAVA_INT.withName("priority"),
            MemoryLayout.paddingLayout(4),
            ValueLayout.JAVA_LONG.withName("deliver_at_ns"),
            ValueLayout.JAVA_LONG.withName("ttl_ns"),
            ValueLayout.ADDRESS.withName("data"),
            ValueLayout.JAVA_LONG.withName("size"),
            ValueLayout.JAVA_INT.withName("flags"),
            MemoryLayout.paddingLayout(4));

    private static final VarHandle VH_CAPACITY =
            QUEUE_OPTS.varHandle(MemoryLayout.PathElement.groupElement("capacity"));
    private static final VarHandle VH_MSG_DATA =
            MESSAGE.varHandle(MemoryLayout.PathElement.groupElement("data"));
    private static final VarHandle VH_MSG_SIZE =
            MESSAGE.varHandle(MemoryLayout.PathElement.groupElement("size"));

    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LOOKUP = SymbolLookup.libraryLookup(
            resolveLibrary(), Arena.global());

    private static final MethodHandle MH_RUNTIME_CREATE = downcall("emq_runtime_create",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    private static final MethodHandle MH_RUNTIME_DESTROY = downcall("emq_runtime_destroy",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    private static final MethodHandle MH_QUEUE_OPTS_DEFAULT = downcall("emq_queue_opts_default",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    private static final MethodHandle MH_QUEUE_CREATE = downcall("emq_queue_create",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));

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

    private final Arena arena = Arena.ofConfined();
    private final MemorySegment runtime;
    private boolean closed;

    public Emq() {
        MemorySegment rtOut = arena.allocate(ValueLayout.ADDRESS);
        EmqException.check(invokeInt(MH_RUNTIME_CREATE, rtOut));
        runtime = rtOut.get(ValueLayout.ADDRESS, 0);
    }

    public Queue openQueue(String name, int capacity) {
        Objects.requireNonNull(name);
        MemorySegment cName = arena.allocateFrom(name, StandardCharsets.UTF_8);
        MemorySegment opts = arena.allocate(QUEUE_OPTS);
        invokeVoid(MH_QUEUE_OPTS_DEFAULT, opts);
        VH_CAPACITY.set(opts, 0L, capacity);

        MemorySegment qOut = arena.allocate(ValueLayout.ADDRESS);
        EmqException.check(invokeInt(MH_QUEUE_CREATE, runtime, cName, opts, qOut));
        return new Queue(qOut.get(ValueLayout.ADDRESS, 0));
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
            Objects.requireNonNull(data);
            MemorySegment payload = data.length == 0
                    ? MemorySegment.NULL
                    : arena.allocateFrom(ValueLayout.JAVA_BYTE, data);
            EmqException.check(invokeInt(MH_PUSH, handle, payload, (long) data.length, MemorySegment.NULL));
        }

        public Message pop(int timeoutMs) {
            MemorySegment msg = arena.allocate(MESSAGE);
            EmqException.check(invokeInt(MH_POP, handle, msg, timeoutMs));
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
            MemorySegment ptr = (MemorySegment) VH_MSG_DATA.get(segment, 0L);
            long size = (long) VH_MSG_SIZE.get(segment, 0L);
            if (ptr.equals(MemorySegment.NULL) || size <= 0) {
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
        return NativeLoader.resolve();
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
