# embeddedmq (Java)

JDK **21+** Panama FFM bindings (not JNI), packaged like
[xerial/sqlite-jdbc](https://github.com/xerial/sqlite-jdbc):

1. Maven artifact contains Java code **and** platform natives under
   `src/main/resources/native/<os>/<arch>/`
2. [`NativeLoader`](src/main/java/io/embeddedmq/NativeLoader.java) detects
   OS/arch, extracts the matching `emq.dll` / `libemq.so` / `libemq.dylib`,
   and loads it for FFM `SymbolLookup`

```text
java/
  pom.xml                         # io.embeddedmq:embeddedmq
  src/main/java/io/embeddedmq/
    Emq.java
    NativeLoader.java
  src/main/resources/native/
    linux/x86_64/libemq.so
    windows/x86_64/emq.dll
    macos/aarch64/libemq.dylib
```

Natives are filled by [`.github/workflows/release-bindings.yml`](../../.github/workflows/release-bindings.yml).

## Build

```bash
cd bindings/java
mvn -q package
```

Local override (skip JAR natives):

```bash
mvn -q test -Demq.lib.path=/path/to/dir/with/libemq
```

## Usage

```java
import io.embeddedmq.Emq;
import java.nio.charset.StandardCharsets;

try (Emq rt = new Emq()) {
    try (var q = rt.openQueue("demo", 64)) {
        q.push("hello".getBytes(StandardCharsets.UTF_8));
        try (var msg = q.pop(0)) {
            System.out.println(new String(msg.data(), StandardCharsets.UTF_8));
        }
    }
}
```

```xml
<!-- after Maven Central publish -->
<dependency>
  <groupId>io.github.a-g-u-p-t-a</groupId>
  <artifactId>embeddedmq</artifactId>
  <version>1.0.0-beta.1</version>
</dependency>
```

## Notes

- Struct layouts in `Emq.java` are still scaffold-grade; production should use
  jextract / explicit `MemoryLayout` from `emq_types.h`.
- FFM requires a **shared** library (not a static `.a`).
- Publish profile: `mvn -Pcentral deploy` (needs Sonatype namespace + GPG).
