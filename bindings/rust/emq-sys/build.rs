use std::env;
use std::path::PathBuf;

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let bindings_root = manifest_dir.parent().unwrap().parent().unwrap();

    let include_dir = env::var("EMQ_INCLUDE_DIR").unwrap_or_else(|_| {
        bindings_root
            .join("..")
            .join("core")
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

    println!("cargo:rerun-if-env-changed=EMQ_INCLUDE_DIR");
    println!("cargo:rerun-if-env-changed=EMQ_LIB_DIR");
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
