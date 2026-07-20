# embeddedmq (Java)

JDK **22+** Panama FFM bindings (not JNI), packaged like
[xerial/sqlite-jdbc](https://github.com/xerial/sqlite-jdbc):

1. Maven artifact contains Java code **and** platform natives under
   `src/main/resources/native/<os>/<arch>/`
2. [`NativeLoader`](src/main/java/io/embeddedmq/NativeLoader.java) detects
   OS/arch, extracts the matching `emq.dll` / `libemq.so` / `libemq.dylib`,
   and loads it for FFM `SymbolLookup`

```text
java/
  pom.xml
  src/main/java/io/embeddedmq/
    Emq.java
    NativeLoader.java
  src/main/resources/native/
    linux/x86_64/libemq.so
    windows/x86_64/emq.dll
    macos/aarch64/libemq.dylib
  src/test/java/io/embeddedmq/
    QueueSmokeTest.java
    QueueLoadTest.java      # bottleneck / ops-per-sec harness
```

## Hot-path API (preferred)

```java
try (Emq rt = new Emq();
     var q = rt.openQueue("demo", 64);
     var arena = java.lang.foreign.Arena.ofConfined()) {
    byte[] payload = "hello".getBytes();
    var nativePayload = arena.allocateFrom(
            java.lang.foreign.ValueLayout.JAVA_BYTE, payload);
    byte[] dst = new byte[payload.length];

    q.pushNative(nativePayload, payload.length); // no per-call alloc
    int n = q.popCopy(dst, 1000);                // no Message object
}
```

`push(byte[])` / `pop()` still work but allocate more; use them for convenience,
not tight loops.

## Build + test (local JAR)

Natives must be staged first (CI does this; locally pull from a prior release JAR
or copy `out/native/...` from `release-bindings`).

```bash
cd bindings/java
# ensure native/<os>/<arch>/libemq.* exists under src/main/resources
mvn -q test          # includes throughput floor (>1.5M round-trip ops/s)
bash run-loadtest.sh # full 100k bottleneck breakdown
```

JVM flag required: `--enable-native-access=ALL-UNNAMED`.

## Why Central `1.0.0-beta.3` looked ~30× slow

The published client used `MethodHandle.invokeWithArguments`, allocated a new
arena segment per push/pop, and always copied via `Message.data()`. That was
**client overhead**, not the C engine. Local `1.0.0-beta.4-SNAPSHOT` fixes:

- `invokeExact` + `Linker.Option.critical`
- reused push/pop scratch buffers
- `pushNative` / `popCopy` fast path

Do **not** publish another Central version until `run-loadtest.sh` numbers look right.

## Dependency

```xml
<dependency>
  <groupId>io.github.a-g-u-p-t-a</groupId>
  <artifactId>embeddedmq</artifactId>
  <version>1.0.0-beta.4-SNAPSHOT</version> <!-- local install until published -->
</dependency>
```
