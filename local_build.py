#!/usr/bin/env python

import os

import sys

import subprocess
from typing import List
import pybind11

ROOT_DIR = os.path.abspath("./")
BUILD_DIR = os.path.join(ROOT_DIR, "build")

def get_pybind11_cmake_args():
        pybind11_sys_path = os.getenv("PYBIND11_SYSPATH")
        if pybind11_sys_path:
            # pybind11_include_dir = os.path.join(pybind11_sys_path, "include")
            pybind11_cmake_dir = os.path.join(pybind11_sys_path, "share", "cmake", "pybind11")
        else:
            # pybind11_include_dir = pybind11.get_include()
            pybind11_cmake_dir = pybind11.get_cmake_dir()
        # print(f"{pybind11_include_dir=}")
        print(f"{pybind11_cmake_dir=}")
        return [f"-Dpybind11_DIR={pybind11_cmake_dir}"]


def get_cross_python_cmake_args():
    """Forward the target-Python headers/libs to CMake when cibuildwheel
    cross-compiles (e.g. win_arm64 built on an amd64 host for CPython < 3.12,
    which has no native arm64 runner Python).

    In that mode cibuildwheel runs a host (amd64) interpreter and passes the
    arm64 target's include/library dirs to distutils build_ext through the file
    named by DIST_EXTRA_CONFIG. This build drives CMake directly and never runs
    distutils build_ext, so pybind11 would otherwise resolve the host amd64
    pythonXY.lib and the arm64 extension fails to link (unresolved __imp_Py*
    symbols). Re-export those dirs to CMake's modern FindPython instead.
    """
    dist_extra = os.getenv("DIST_EXTRA_CONFIG")
    if not dist_extra or not os.path.isfile(dist_extra):
        return []

    import configparser
    import glob

    # RawConfigParser: paths contain characters (e.g. '%') that the default
    # interpolation would choke on.
    cfg = configparser.RawConfigParser()
    cfg.read(dist_extra)
    if not cfg.has_section("build_ext"):
        return []

    include_dir = cfg.get("build_ext", "include_dirs", fallback="").strip().splitlines()
    library_dir = cfg.get("build_ext", "library_dirs", fallback="").strip().splitlines()

    args = []
    # Switch pybind11 to CMake's modern FindPython, which honors the explicit
    # Python_INCLUDE_DIR / Python_LIBRARY hints below (the legacy
    # FindPythonLibsNew derives them from the host interpreter's arch).
    args.append("-DPYBIND11_FINDPYTHON=ON")
    # FindPython uses the mixed-case name; pin it to the build interpreter so it
    # does not pick a different Python off PATH.
    args.append(f"-DPython_EXECUTABLE={sys.executable}")
    if include_dir:
        args.append(f"-DPython_INCLUDE_DIR={include_dir[0].strip()}")
    if library_dir:
        libs = glob.glob(os.path.join(library_dir[0].strip(), "python*.lib"))
        # Prefer the versioned python3XY.lib over the stable-ABI python3.lib.
        versioned = [l for l in libs if not l.lower().endswith("python3.lib")]
        chosen = (versioned or libs)
        if chosen:
            args.append(f"-DPython_LIBRARY={chosen[0]}")

    print(f"cross-compile python cmake args: {args}")
    return args

def run(cmd: List[str], cwd: str="./"):

    print_cmd = " ".join(cmd)
    print(f"\nlaunch: {print_cmd}")

    message = subprocess.run(cmd, cwd=cwd)

    if "returncode=0" in str(message):
        print(f" -> SUCCESS")
        return True

    print(f" -> ERROR with message: '{message}'\n")
    return False


def build_local(num_threads: int):

    USE_SYSTEM_DEPS = os.getenv("USE_SYSTEM_DEPS", "OFF")

    print("python prefix: ", sys.exec_prefix)
    print("python executable: ", sys.executable)
    config_cmd = [
        "cmake",
        "-B", f"{BUILD_DIR}",
        f"-DUSE_SYSTEM_DEPS={USE_SYSTEM_DEPS}",
        f"-DPYTHON_EXECUTABLE={sys.executable}",
    ]

    # Forward CMAKE_GENERATOR so callers (e.g. the CI workflow) can switch the
    # generator without patching this script (e.g. "Visual Studio 17 2022" for
    # MSVC on Windows ARM64 vs "MSYS Makefiles" for MinGW on Windows AMD64).
    cmake_generator = os.getenv("CMAKE_GENERATOR")
    if cmake_generator:
        config_cmd.extend(["-G", cmake_generator])

    # Forward extra cmake flags supplied via CMAKE_ARGS (e.g. "-A ARM64
    # -DZLIB_ROOT=...").  Split on whitespace but respect quoted strings.
    cmake_args_env = os.getenv("CMAKE_ARGS", "").strip()
    if cmake_args_env:
        import shlex
        config_cmd.extend(shlex.split(cmake_args_env))

    config_cmd.extend(get_pybind11_cmake_args())
    config_cmd.extend(get_cross_python_cmake_args())
    success = run(config_cmd, cwd=ROOT_DIR)
    if not success:
        raise RuntimeError("Error building.")

    build_cmd = [
        "cmake",
        "--build", f"{BUILD_DIR}",
        "--target=install",
        # Authoritative for multi-config generators (e.g. Visual Studio on Windows
        # ARM64): builds Release so MSVC uses /O2 and the redistributable release
        # CRT. Ignored by single-config generators (Makefiles/Ninja on Linux,
        # macOS and MinGW), where the optimization level comes from CMAKE_CXX_FLAGS.
        "--config", "Release",
    ]
    if num_threads > 1:
        build_cmd.extend(["-j", f"{num_threads}"])
    success = run(build_cmd, cwd=ROOT_DIR)
    if not success:
        raise RuntimeError("Error building.")


if "__main__" == __name__:
    num_threads = int(os.getenv("BUILD_THREADS", "4"))
    print(f"Building with {num_threads} threads (BUILD_THREADS={os.getenv('BUILD_THREADS', 'not set, defaulting to 4')})")
    build_local(num_threads=num_threads)
