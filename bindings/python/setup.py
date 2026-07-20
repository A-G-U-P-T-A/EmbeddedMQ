from pathlib import Path

from setuptools import Extension, setup

ROOT = Path(__file__).resolve().parent
REPO = Path(__file__).resolve().parents[2]

emq_root = Path(__import__("os").environ.get("EMQ_ROOT", REPO))
include_dir = emq_root / "core" / "include"
lib_dir = Path(__import__("os").environ.get("EMQ_LIB_DIR", emq_root / "build"))

extra_link = []
if __import__("sys").platform == "win32":
    extra_link.append("Synchronization")
else:
    extra_link.append("pthread")

ext = Extension(
    "embeddedmq._emq",
    sources=[str(ROOT / "src" / "embeddedmq" / "_emq.c")],
    include_dirs=[str(include_dir)],
    library_dirs=[str(lib_dir)],
    libraries=["emq", *extra_link],
    define_macros=[("Py_LIMITED_API", "0x03090000")],
)

setup(
    name="embeddedmq",
    version="0.1.0",
    package_dir={"": "src"},
    packages=["embeddedmq"],
    ext_modules=[ext],
    python_requires=">=3.9",
)
