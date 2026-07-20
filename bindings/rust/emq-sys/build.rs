use std::env;
use std::path::{Path, PathBuf};

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    // emq-sys -> rust -> bindings
    let bindings_root = manifest_dir.parent().unwrap().parent().unwrap();
    let native = bindings_root.join("native");

    println!("cargo:rerun-if-env-changed=EMQ_LIB_DIR");
    println!("cargo:rerun-if-env-changed=EMQ_INCLUDE_DIR");
    println!("cargo:rerun-if-env-changed=EMQ_SYSTEM_LIB");
    println!("cargo:rerun-if-changed={}", native.join("sources.txt").display());

    // Escape hatch: link a prebuilt system/static libemq.
    if env::var("EMQ_SYSTEM_LIB").ok().as_deref() == Some("1")
        || env::var("EMQ_LIB_DIR").is_ok()
    {
        link_system(&bindings_root);
        return;
    }

    if !native.join("sources.txt").is_file() {
        panic!(
            "missing {}; run: python scripts/sync_native.py",
            native.join("sources.txt").display()
        );
    }

    compile_bundled(&native);

    if cfg!(target_os = "windows") {
        println!("cargo:rustc-link-lib=Synchronization");
    } else {
        println!("cargo:rustc-link-lib=pthread");
    }
}

fn link_system(bindings_root: &Path) {
    let include_dir = env::var("EMQ_INCLUDE_DIR").unwrap_or_else(|_| {
        bindings_root
            .join("native")
            .join("include")
            .to_string_lossy()
            .into_owned()
    });
    let lib_dir = env::var("EMQ_LIB_DIR").unwrap_or_else(|_| {
        bindings_root
            .join("..")
            .join("build")
            .to_string_lossy()
            .into_owned()
    });
    println!("cargo:rustc-link-search=native={lib_dir}");
    println!("cargo:rustc-link-lib=static=emq");
    if cfg!(target_os = "windows") {
        println!("cargo:rustc-link-lib=Synchronization");
    } else {
        println!("cargo:rustc-link-lib=pthread");
    }
    let header = PathBuf::from(&include_dir).join("emq").join("emq.h");
    println!("cargo:rerun-if-changed={}", header.display());
}

fn compile_bundled(native: &Path) {
    let sources_txt = native.join("sources.txt");
    let list = std::fs::read_to_string(&sources_txt).expect("read sources.txt");
    let mut build = cc::Build::new();
    build
        .std("c11")
        .include(native.join("include"))
        .include(native.join("src"))
        .warnings(false);

    if cfg!(target_os = "windows") {
        build.define("EMQ_PLATFORM_WINDOWS", None);
        build.define("_CRT_SECURE_NO_WARNINGS", None);
    } else if cfg!(target_os = "macos") {
        build.define("EMQ_PLATFORM_POSIX", None);
        build.define("_DARWIN_C_SOURCE", None);
    } else {
        build.define("EMQ_PLATFORM_POSIX", None);
        build.define("_GNU_SOURCE", None);
    }

    for line in list.lines() {
        let rel = line.trim();
        if rel.is_empty() {
            continue;
        }
        let path = native.join(rel);
        println!("cargo:rerun-if-changed={}", path.display());
        build.file(&path);
    }

    build.compile("emq");
}
