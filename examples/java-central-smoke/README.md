# Java Central smoke

Minimal Maven app that depends on the published Central artifact:

```xml
<dependency>
  <groupId>io.github.a-g-u-p-t-a</groupId>
  <artifactId>embeddedmq</artifactId>
  <version>1.0.0-beta.2</version>
</dependency>
```

Requires **JDK 22+** (FFM is final; the published JAR targets 22).

```bash
mvn -q package
mvn -q exec:java
# or
java --enable-native-access=ALL-UNNAMED -jar target/java-central-smoke-1.0.0-SNAPSHOT.jar
```
