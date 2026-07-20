from pathlib import Path
import os
import sys

from setuptools import Extension, setup

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]

# Vendored by scripts/sync_native.py (must live under bindings/python/).
NATIVE = ROOT / "native"
sources_txt = NATIVE / "sources.txt"
use_system = os.environ.get("EMQ_SYSTEM_LIB") == "1" or os.environ.get("EMQ_LIB_DIR")


def rel(p: Path) -> str:
    """setuptools requires paths relative to setup.py, /-separated, no abs."""
    return Path(os.path.relpath(p, ROOT)).as_posix()


ext_sources = [rel(ROOT / "src" / "embeddedmq" / "_emq.c")]
include_dirs = []
library_dirs = []
libraries = []
extra_compile = []

if use_system:
    emq_root = Path(os.environ.get("EMQ_ROOT", REPO))
    include_dirs.append(str(emq_root / "core" / "include"))
    library_dirs.append(str(Path(os.environ.get("EMQ_LIB_DIR", emq_root / "build"))))
    libraries.append("emq")
else:
    if not sources_txt.is_file():
        raise SystemExit(
            "missing bindings/python/native/sources.txt — run: python scripts/sync_native.py"
        )
    include_dirs.append(str(NATIVE / "include"))
    include_dirs.append(str(NATIVE / "src"))
    for line in sources_txt.read_text(encoding="utf-8").splitlines():
        s = line.strip()
        if s:
            ext_sources.append(rel(NATIVE / s))
    if sys.platform == "win32":
        extra_compile.extend(
            ["/std:c11", "-DEMQ_PLATFORM_WINDOWS", "-D_CRT_SECURE_NO_WARNINGS"]
        )
    elif sys.platform == "darwin":
        extra_compile.extend(
            ["-std=c11", "-DEMQ_PLATFORM_POSIX", "-D_DARWIN_C_SOURCE"]
        )
    else:
        extra_compile.extend(["-std=c11", "-DEMQ_PLATFORM_POSIX", "-D_GNU_SOURCE"])

if sys.platform == "win32":
    libraries.append("Synchronization")
else:
    libraries.append("pthread")

ext = Extension(
    "embeddedmq._emq",
    sources=ext_sources,
    include_dirs=include_dirs,
    library_dirs=library_dirs,
    libraries=libraries,
    extra_compile_args=extra_compile,
)

setup(
    name="embeddedmq",
    version="1.0.0b2",
    package_dir={"": "src"},
    packages=["embeddedmq"],
    ext_modules=[ext],
    python_requires=">=3.9",
)
