#!/usr/bin/env python

import sys
import zipfile
from pathlib import Path


# Guards that release wheels do not ship Blend2D's debug assertion path, i.e.
# that Blend2D was compiled with BL_BUILD_RELEASE (see cmake/extlib_blend2d.cmake).
#
# Do NOT scan for "[Blend2D] ASSERTION FAILURE" or "bl_runtime_assertion_failure".
# Blend2D compiles bl_runtime_assertion_failure() unconditionally in
# blend2d/core/runtime.cpp -- there is no #ifdef around it -- and its object file
# is always pulled out of libblend2d.a because other symbols in it are needed.
# BL_BUILD_RELEASE only removes the *call sites* (BL_ASSERT in blend2d/core/api.h,
# BL_NOT_REACHED in blend2d/core/api-internal_p.h). So the function and its message
# string enter the link on every platform, and whether they survive into the final
# artifact depends purely on how well the linker dead-strips:
#
#   macOS        ld64 drops unreferenced cstring literal atoms, plus pybind11's
#                POST_BUILD `strip -x`.
#   manylinux    -ffunction-sections + -Wl,--gc-sections; the literal lives in a
#                SHF_MERGE|SHF_STRINGS .rodata.str1.1 and is dropped along with it.
#   win_arm64    MSVC: /O2 implies /Gy, the linker's /OPT:REF drops the COMDAT, and
#                symbol names go to the PDB rather than into the image.
#   win_amd64    MinGW GCC targeting PE/COFF: nothing removes it. --gc-sections for
#                COFF/PE is experimental, and COFF has no SHF_MERGE/SHF_STRINGS
#                equivalent -- mingw GCC emits all of a translation unit's string
#                literals into one shared .rdata, and -fdata-sections does not split
#                anonymous string constants. The rest of runtime.cpp.obj keeps that
#                .rdata alive, so section-granularity GC can never reach the string.
#
# Those markers therefore produced a false positive on win_amd64 (the only GNU-on-PE
# job in the matrix) for a correctly configured Release build.
#
# BL_NOT_REACHED()'s expression string is a marker that is impossible in a release
# build: only the BL_BUILD_DEBUG expansion emits it as a string literal, while the
# GCC/Clang release expansion is __builtin_unreachable(). With ~100 call sites across
# the library, at least one lands in any linked debug build.
BLEND2D_ASSERTION_MARKERS = (b"BL_NOT_REACHED()",)
NATIVE_SUFFIXES = {".so", ".pyd", ".dll", ".dylib"}


def native_files(path: Path) -> list[Path]:
    if path.is_file():
        if path.suffix.lower() == ".whl" or path.suffix.lower() in NATIVE_SUFFIXES:
            return [path]
        return []
    return [
        item
        for item in path.rglob("*")
        if item.is_file() and item.suffix.lower() in NATIVE_SUFFIXES
    ]


def scan_file(path: Path) -> list[str]:
    if path.suffix.lower() == ".whl":
        findings: list[str] = []
        with zipfile.ZipFile(path) as zf:
            for name in zf.namelist():
                if Path(name).suffix.lower() not in NATIVE_SUFFIXES:
                    continue
                data = zf.read(name)
                if has_blend2d_assertion_marker(data):
                    findings.append(f"{path}!{name}")
        return findings

    try:
        data = path.read_bytes()
    except OSError as exc:
        print(f"warning: could not read {path}: {exc}", file=sys.stderr)
        return []

    return [str(path)] if has_blend2d_assertion_marker(data) else []


def has_blend2d_assertion_marker(data: bytes) -> bool:
    return any(marker in data for marker in BLEND2D_ASSERTION_MARKERS)


def main(argv: list[str]) -> int:
    if not argv:
        print(
            "usage: check_no_blend2d_assertions.py <native-file|wheel|directory> ...",
            file=sys.stderr,
        )
        return 2

    findings: list[str] = []
    for arg in argv:
        path = Path(arg)
        if not path.exists():
            print(f"warning: path does not exist: {path}", file=sys.stderr)
            continue
        for item in native_files(path):
            findings.extend(scan_file(item))

    if findings:
        print(
            "Blend2D debug assertion code found in release artifact "
            "(Blend2D was not compiled with BL_BUILD_RELEASE):",
            file=sys.stderr,
        )
        for finding in findings:
            print(f"  {finding}", file=sys.stderr)
        return 1

    print("No Blend2D debug assertion code found.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
