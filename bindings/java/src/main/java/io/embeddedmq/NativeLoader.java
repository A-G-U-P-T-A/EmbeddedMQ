package io.embeddedmq;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;

/**
 * sqlite-jdbc-style loader: pick the OS/arch native from the JAR classpath
 * ({@code /native/<os>/<arch>/libemq.*}), extract to a temp dir, and return
 * the path for Panama {@code SymbolLookup.libraryLookup}.
 *
 * <p>Override with {@code -Demq.lib.path=/path/to/libemq.dll|.so|.dylib} or a
 * directory containing that library.
 */
public final class NativeLoader {
    private static final Object LOCK = new Object();
    private static Path extracted;

    private NativeLoader() {}

    public static Path resolve() {
        synchronized (LOCK) {
            if (extracted != null) {
                return extracted;
            }
            String override = System.getProperty("emq.lib.path");
            if (override != null && !override.isBlank()) {
                Path p = Path.of(override);
                if (Files.isDirectory(p)) {
                    p = findInDir(p);
                }
                if (!Files.isRegularFile(p)) {
                    throw new UnsatisfiedLinkError("emq.lib.path not found: " + override);
                }
                extracted = p.toAbsolutePath().normalize();
                return extracted;
            }
            extracted = extractBundled();
            return extracted;
        }
    }

    private static Path extractBundled() {
        String os = normalizeOs(System.getProperty("os.name", ""));
        String arch = normalizeArch(System.getProperty("os.arch", ""));
        String libName = System.mapLibraryName("emq");
        String resource = "/native/" + os + "/" + arch + "/" + libName;

        try (InputStream in = NativeLoader.class.getResourceAsStream(resource)) {
            if (in == null) {
                throw new UnsatisfiedLinkError(
                        "Bundled native not found: " + resource
                                + " (build with release-bindings CI or set -Demq.lib.path)");
            }
            Path dir = Files.createTempDirectory("embeddedmq-native-");
            dir.toFile().deleteOnExit();
            Path out = dir.resolve(libName);
            try (OutputStream osOut = Files.newOutputStream(out)) {
                in.transferTo(osOut);
            }
            out.toFile().deleteOnExit();
            // Best-effort: make executable on Unix.
            out.toFile().setExecutable(true, false);
            return out.toAbsolutePath().normalize();
        } catch (IOException e) {
            throw new UnsatisfiedLinkError("Failed to extract " + resource + ": " + e.getMessage());
        }
    }

    private static Path findInDir(Path dir) {
        String libName = System.mapLibraryName("emq");
        Path direct = dir.resolve(libName);
        if (Files.isRegularFile(direct)) {
            return direct;
        }
        // Windows sometimes uses emq.dll without lib prefix; also check Release/
        Path release = dir.resolve("Release").resolve(libName);
        if (Files.isRegularFile(release)) {
            return release;
        }
        Path alt = dir.resolve("libemq.dll");
        if (Files.isRegularFile(alt)) {
            return alt;
        }
        return direct;
    }

    static String normalizeOs(String osName) {
        String n = osName.toLowerCase(Locale.ROOT);
        if (n.contains("win")) {
            return "windows";
        }
        if (n.contains("mac") || n.contains("darwin") || n.contains("os x")) {
            return "macos";
        }
        if (n.contains("linux")) {
            return "linux";
        }
        throw new UnsatisfiedLinkError("Unsupported OS for EmbeddedMQ natives: " + osName);
    }

    static String normalizeArch(String arch) {
        String a = arch.toLowerCase(Locale.ROOT);
        if (a.equals("amd64") || a.equals("x86_64") || a.equals("x64")) {
            return "x86_64";
        }
        if (a.equals("aarch64") || a.equals("arm64")) {
            return "aarch64";
        }
        if (a.equals("x86") || a.equals("i386") || a.equals("i686")) {
            return "x86";
        }
        throw new UnsatisfiedLinkError("Unsupported CPU arch for EmbeddedMQ natives: " + arch);
    }
}
