# Java bindings (Panama FFM)

JDK **21+** scaffold using the [Foreign Function & Memory API](https://openjdk.org/jeps/454) — **not JNI**.

## Prerequisites

1. Build `libemq` (or `emq.dll` / `libemq.so` if you enable shared builds).
2. JDK 21 or later with native access enabled for tests.

```bash
cmake -S core -B build -DEMQ_BUILD_TESTS=OFF
cmake --build build
```

## Build

```bash
cd bindings/java
mvn -q compile
```

Point Maven at the native library directory:

```bash
mvn -q test -Demq.lib.path=/path/to/build
```

Surefire passes `-Djava.library.path` and `--enable-native-access=ALL-UNNAMED`.

## Usage sketch

```java
try (Emq rt = new Emq()) {
    try (var q = rt.openQueue("demo", 64)) {
        q.push("hello".getBytes(StandardCharsets.UTF_8));
        try (var msg = q.pop(0)) {
            System.out.println(new String(msg.data()));
        }
    }
}
```

`Message.close()` calls `emq_message_release`.

## Layout

```
java/
  pom.xml
  src/main/java/io/embeddedmq/Emq.java
```

## Notes

- Struct layouts in `Emq.java` use approximate offsets suitable for a scaffold; production bindings should generate layouts from `emq_types.h` (jextract or manual `MemoryLayout` definitions).
- Static linking is not supported by FFM; use a shared `libemq` or load the platform library from your build tree.
- No JNI `.so` / `.dll` bridge layer is involved.
