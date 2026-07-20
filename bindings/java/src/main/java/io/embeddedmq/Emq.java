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
 *
 * <p>Hot path notes:
 * <ul>
 *   <li>Uses {@code invokeExact} (not {@code invokeWithArguments}).</li>
 *   <li>Marks short native calls as {@link Linker.Option#critical}.</li>
 *   <li>Reuses per-queue scratch segments for push/pop.</li>
 *   <li>{@link Queue#push(byte[])} / {@link Queue#popCopy(byte[], int)} avoid
 *       per-message Java object allocation.</li>
 * </ul>
 */
public final class Emq implements AutoCloseable {

    public static final int EMQ_OK = 0;
    public static final int EMQ_ERR_FULL = -4;
    public static final int EMQ_ERR_EMPTY = -5;
    public static final int EMQ_ERR_TIMEOUT = -7;

    static final StructLayout QUEUE_OPTS = MemoryLayout.structLayout(
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

    static final StructLayout MESSAGE = MemoryLayout.structLayout(
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

    // critical(true): short native calls — huge win vs default Panama transitions.
    private static final Linker.Option CRITICAL = Linker.Option.critical(true);

    private static final MethodHandle MH_RUNTIME_CREATE = downcall(
            "emq_runtime_create",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    private static final MethodHandle MH_RUNTIME_DESTROY = downcall(
            "emq_runtime_destroy",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    private static final MethodHandle MH_QUEUE_OPTS_DEFAULT = downcall(
            "emq_queue_opts_default",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    private static final MethodHandle MH_QUEUE_CREATE = downcall(
            "emq_queue_create",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS));

    private static final MethodHandle MH_QUEUE_CLOSE = downcallCritical(
            "emq_queue_close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    private static final MethodHandle MH_PUSH = downcallCritical(
            "emq_push",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_LONG, ValueLayout.ADDRESS));

    private static final MethodHandle MH_POP = downcallCritical(
            "emq_pop",
            FunctionDescriptor.of(ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));

    private static final MethodHandle MH_MESSAGE_RELEASE = downcallCritical(
            "emq_message_release",
            FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));

    private final Arena arena = Arena.ofConfined();
    private final MemorySegment runtime;
    private boolean closed;

    public Emq() {
        MemorySegment rtOut = arena.allocate(ValueLayout.ADDRESS);
        EmqException.check(invokeCreate(rtOut));
        runtime = rtOut.get(ValueLayout.ADDRESS, 0);
    }

    public Queue openQueue(String name, int capacity) {
        Objects.requireNonNull(name);
        MemorySegment cName = arena.allocateFrom(name, StandardCharsets.UTF_8);
        MemorySegment opts = arena.allocate(QUEUE_OPTS);
        invokeOptsDefault(opts);
        VH_CAPACITY.set(opts, 0L, capacity);

        MemorySegment qOut = arena.allocate(ValueLayout.ADDRESS);
        EmqException.check(invokeQueueCreate(runtime, cName, opts, qOut));
        return new Queue(qOut.get(ValueLayout.ADDRESS, 0));
    }

    @Override
    public void close() {
        if (!closed) {
            invokeDestroy(runtime);
            closed = true;
            arena.close();
        }
    }

    public final class Queue implements AutoCloseable {
        private final MemorySegment handle;
        private final MemorySegment msgScratch = arena.allocate(MESSAGE);
        private MemorySegment pushScratch = MemorySegment.NULL;
        private long pushScratchCap;
        private boolean queueClosed;

        Queue(MemorySegment handle) {
            this.handle = handle;
        }

        /** Convenience push — copies into a reused native scratch buffer. */
        public void push(byte[] data) {
            Objects.requireNonNull(data);
            if (data.length == 0) {
                EmqException.check(invokePush(handle, MemorySegment.NULL, 0L, MemorySegment.NULL));
                return;
            }
            ensurePushScratch(data.length);
            MemorySegment.copy(data, 0, pushScratch, ValueLayout.JAVA_BYTE, 0, data.length);
            EmqException.check(invokePush(handle, pushScratch, data.length, MemorySegment.NULL));
        }

        /**
         * Zero-copy push from an already-native segment (caller keeps it live
         * until {@code emq_push} returns — payload is copied into the queue).
         */
        public void pushNative(MemorySegment data, long size) {
            EmqException.check(invokePush(handle,
                    data == null ? MemorySegment.NULL : data,
                    size,
                    MemorySegment.NULL));
        }

        /**
         * High-throughput pop: copy payload into {@code dst}, release native
         * message, return byte length. Avoids Message allocation.
         */
        public int popCopy(byte[] dst, int timeoutMs) {
            Objects.requireNonNull(dst);
            msgScratch.fill((byte) 0);
            EmqException.check(invokePop(handle, msgScratch, timeoutMs));
            try {
                MemorySegment ptr = (MemorySegment) VH_MSG_DATA.get(msgScratch, 0L);
                long size = (long) VH_MSG_SIZE.get(msgScratch, 0L);
                if (ptr.address() == 0 || size <= 0) {
                    return 0;
                }
                if (size > dst.length) {
                    throw new IllegalArgumentException(
                            "dst too small: need " + size + " have " + dst.length);
                }
                MemorySegment.copy(ptr.reinterpret(size), ValueLayout.JAVA_BYTE, 0, dst, 0, (int) size);
                return (int) size;
            } finally {
                invokeMessageRelease(msgScratch);
            }
        }

        /** Compatibility API (allocates a Message wrapper). Prefer {@link #popCopy}. */
        public Message pop(int timeoutMs) {
            MemorySegment owned = arena.allocate(MESSAGE);
            EmqException.check(invokePop(handle, owned, timeoutMs));
            return new Message(owned);
        }

        private void ensurePushScratch(int need) {
            if (pushScratchCap >= need) {
                return;
            }
            long cap = Math.max(need, 256);
            pushScratch = arena.allocate(cap);
            pushScratchCap = cap;
        }

        @Override
        public void close() {
            if (!queueClosed) {
                EmqException.check(invokeQueueClose(handle));
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
            if (ptr.address() == 0 || size <= 0) {
                return new byte[0];
            }
            return ptr.reinterpret(size).toArray(ValueLayout.JAVA_BYTE);
        }

        public MemorySegment dataSegment() {
            MemorySegment ptr = (MemorySegment) VH_MSG_DATA.get(segment, 0L);
            long size = (long) VH_MSG_SIZE.get(segment, 0L);
            if (ptr.address() == 0 || size <= 0) {
                return MemorySegment.NULL;
            }
            return ptr.reinterpret(size);
        }

        public long size() {
            return (long) VH_MSG_SIZE.get(segment, 0L);
        }

        @Override
        public void close() {
            if (!released) {
                invokeMessageRelease(segment);
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

    private static MethodHandle downcallCritical(String name, FunctionDescriptor desc) {
        MemorySegment sym = LOOKUP.find(name)
                .orElseThrow(() -> new UnsatisfiedLinkError("missing symbol: " + name));
        return LINKER.downcallHandle(sym, desc, CRITICAL);
    }

    private static int invokeCreate(MemorySegment out) {
        try {
            return (int) MH_RUNTIME_CREATE.invokeExact(out);
        } catch (Throwable t) {
            throw wrap(t);
        }
    }

    private static void invokeDestroy(MemorySegment rt) {
        try {
            MH_RUNTIME_DESTROY.invokeExact(rt);
        } catch (Throwable t) {
            throw wrap(t);
        }
    }

    private static void invokeOptsDefault(MemorySegment opts) {
        try {
            MH_QUEUE_OPTS_DEFAULT.invokeExact(opts);
        } catch (Throwable t) {
            throw wrap(t);
        }
    }

    private static int invokeQueueCreate(
            MemorySegment rt, MemorySegment name, MemorySegment opts, MemorySegment out) {
        try {
            return (int) MH_QUEUE_CREATE.invokeExact(rt, name, opts, out);
        } catch (Throwable t) {
            throw wrap(t);
        }
    }

    private static int invokeQueueClose(MemorySegment q) {
        try {
            return (int) MH_QUEUE_CLOSE.invokeExact(q);
        } catch (Throwable t) {
            throw wrap(t);
        }
    }

    private static int invokePush(
            MemorySegment q, MemorySegment data, long size, MemorySegment meta) {
        try {
            return (int) MH_PUSH.invokeExact(q, data, size, meta);
        } catch (Throwable t) {
            throw wrap(t);
        }
    }

    private static int invokePop(MemorySegment q, MemorySegment msg, int timeoutMs) {
        try {
            return (int) MH_POP.invokeExact(q, msg, timeoutMs);
        } catch (Throwable t) {
            throw wrap(t);
        }
    }

    private static void invokeMessageRelease(MemorySegment msg) {
        try {
            MH_MESSAGE_RELEASE.invokeExact(msg);
        } catch (Throwable t) {
            throw wrap(t);
        }
    }

    private static RuntimeException wrap(Throwable t) {
        if (t instanceof RuntimeException re) {
            return re;
        }
        if (t instanceof Error e) {
            throw e;
        }
        return new RuntimeException(t);
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
