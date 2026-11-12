#!/usr/bin/env bash
# Build as needed, measure a pinned corpus, and generate a report without editing docs.
set -Eeuo pipefail

if [[ ${1:-} == --help ]]; then
  cat <<'HELP'
Usage: bash scripts/benchmarking/refresh_performance_benchmarks.sh [--memory-mode off|peak|profile] [--memory-interval-ms 50]

Environment:
  REPEATS=1                  Fresh-process repetitions (use 3 for publication)
  THREADS=1,4,8,12            Unique positive thread counts; must include 1
  RUN_COMPARISON=0            Set to 1 to also measure the five reference packages
  BENCHMARK_OUTPUT=compact    compact: progress/results; full: include configs/timings
  BUILD_MODE=incremental      Reuse build/externals; clean archives and rebuilds them
  BUILD_THREADS=8             Parallel build jobs
  DATASET=bo767               Select bo20, bo767 or bo10k
  DATASET_REVISION=<revision>  Override the selected dataset's pinned revision
  OUT=<new directory>         Default: a unique scratch-performance-refresh-* path
  RECOVER_FROM=<bundle>        Reuse validated native/replay results; recollect comparisons in a new bundle
  MEMORY_MODE=off              off, independent OS peak, or peak plus sampled RSS (profile)
  MEMORY_INTERVAL_MS=50        Positive sampling interval; CLI overrides environment

Default dataset pins (PDFs / pages):
  bo20   65b9971b3fa939fe4ef89be678237e6928049b67      20 / 496
  bo767  221c19e716d6ede5d18fd1560bc938ab19cc1a70     767 / 54,730
  bo10k  f85e203f95879e2f5d77059a6bfeffe667cd359e  10,000 / 213,538

Example: DATASET=bo20 THREADS=1,4 bash scripts/benchmarking/refresh_performance_benchmarks.sh

The launcher checks/builds both native targets incrementally by default.
BUILD_MODE=clean archives build/ and externals/ under OUT and rebuilds them.
Use clean mode after SDK/toolchain or bundled dependency upgrades. Both modes
sync the build/dev/perf dependency groups and install the Python extension.
It then runs the damaged-page tests, downloads the pinned dataset if needed,
and runs benchmarks sequentially. Native scaling measures rendering both without
and with word/line cell creation, using separate processes and output files.
It writes raw inputs, provenance, tables and
PNG figures under OUT. It does not update the documentation or commit files.
Full corpus sweeps can take hours, especially with RUN_COMPARISON=1 or REPEATS=3.
HELP
  exit 0
fi
MEMORY_MODE=${MEMORY_MODE:-off}
MEMORY_INTERVAL_MS=${MEMORY_INTERVAL_MS:-50}
while [[ $# != 0 ]]; do
  case "$1" in
    --memory-mode|--memory-interval-ms)
      if [[ $# -lt 2 ]]; then echo "Missing value for $1" >&2; exit 2; fi
      if [[ $1 == --memory-mode ]]; then MEMORY_MODE=$2; else MEMORY_INTERVAL_MS=$2; fi
      shift 2 ;;
    *) echo "Unexpected argument: $1; use --help." >&2; exit 2 ;;
  esac
done
case "$MEMORY_MODE" in off|peak|profile) ;; *) echo "Invalid memory mode." >&2; exit 2 ;; esac
if [[ ! $MEMORY_INTERVAL_MS =~ ^[1-9][0-9]*$ ]]; then
  echo "MEMORY_INTERVAL_MS must be a positive integer." >&2; exit 2
fi
export MEMORY_MODE MEMORY_INTERVAL_MS

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$REPO_ROOT"
if [[ -n ${RECOVER_FROM:-} ]]; then
  if [[ ${BUILD_MODE:-incremental} != incremental ]]; then
    echo "Recovery reuses recorded native binaries; BUILD_MODE must be incremental." >&2
    exit 2
  fi
  recovery=(uv run --no-sync python scripts/benchmarking/recover_performance_benchmarks.py "$RECOVER_FROM")
  if [[ -n ${OUT:-} ]]; then recovery+=(--output-dir "$OUT"); fi
  exec "${recovery[@]}"
fi
REPEATS=${REPEATS:-1}
THREADS=${THREADS:-1,4,8,12}
RUN_COMPARISON=${RUN_COMPARISON:-0}
BENCHMARK_OUTPUT=${BENCHMARK_OUTPUT:-compact}
case "$BENCHMARK_OUTPUT" in
  compact|full) ;;
  *) echo "BENCHMARK_OUTPUT must be compact or full." >&2; exit 2 ;;
esac
BUILD_THREADS=${BUILD_THREADS:-8}
BUILD_MODE=${BUILD_MODE:-incremental}
case "$BUILD_MODE" in
  incremental|clean) ;;
  *) echo "BUILD_MODE must be incremental or clean." >&2; exit 2 ;;
esac
DATASET=${DATASET:-bo767}
case "$DATASET" in
  bo20)  dataset_default_revision=65b9971b3fa939fe4ef89be678237e6928049b67 ;;
  bo767) dataset_default_revision=221c19e716d6ede5d18fd1560bc938ab19cc1a70 ;;
  bo10k) dataset_default_revision=f85e203f95879e2f5d77059a6bfeffe667cd359e ;;
  *) echo "DATASET must be bo20, bo767 or bo10k." >&2; exit 2 ;;
esac
DATASET_REVISION=${DATASET_REVISION:-$dataset_default_revision}
DATASET_REPOSITORY="docling-project/performance-dataset-$DATASET"
for value in "$REPEATS" "$BUILD_THREADS"; do
  if [[ ! $value =~ ^[1-9][0-9]*$ ]]; then
    echo "REPEATS and BUILD_THREADS must be positive integers." >&2
    exit 2
  fi
done
if [[ $RUN_COMPARISON != 0 && $RUN_COMPARISON != 1 ]]; then
  echo "RUN_COMPARISON must be 0 or 1." >&2
  exit 2
fi
case ",$THREADS," in
  *,1,*) ;;
  *) echo "THREADS must include 1 for a measured scaling baseline." >&2; exit 2 ;;
esac
IFS=, read -r -a thread_counts <<< "$THREADS"
seen_threads=,
for count in "${thread_counts[@]}"; do
  if [[ ! $count =~ ^[1-9][0-9]*$ || $seen_threads == *",$count,"* ]]; then
    echo "THREADS must contain unique positive integers without spaces." >&2
    exit 2
  fi
  seen_threads+="$count,"
done
# A trailing comma must not silently disappear during read.
if [[ $THREADS == *, ]]; then
  echo "THREADS must not end with a comma." >&2
  exit 2
fi

if [[ -n ${OUT:-} ]]; then
  if [[ -e $OUT ]]; then
    echo "OUT already exists; choose a new directory to avoid appending timings." >&2
    exit 2
  fi
  mkdir -p "$OUT"
  OUT=$(cd "$OUT" && pwd)
else
  OUT=$(mktemp -d "$REPO_ROOT/scratch-performance-refresh-$(date +%Y%m%d-%H%M%S)-XXXXXX")
fi
echo "Results: $OUT"
case "$OUT/" in
  "$REPO_ROOT/build/"*|"$REPO_ROOT/externals/"*)
    echo "OUT must be outside build/ and externals/ so they can be archived." >&2
    exit 2
    ;;
esac
export MPLCONFIGDIR=${MPLCONFIGDIR:-"$OUT/matplotlib-cache"}
echo "Repetitions: $REPEATS; threads: $THREADS; cross-package suite: $RUN_COMPARISON"
echo "Dataset: $DATASET_REPOSITORY at $DATASET_REVISION"
echo "Benchmark output: $BENCHMARK_OUTPUT"
echo "Build mode: $BUILD_MODE"
echo "Memory collection: $MEMORY_MODE; sampling interval: $MEMORY_INTERVAL_MS ms"
echo "Full corpus measurements can take hours. Progress and commands are saved below this directory."
trap 'echo "Stopped. Inspect the latest log under $OUT; completed measurements are preserved." >&2' ERR

run_logged() {
  local log=$1
  local description=$2
  local started_seconds=$SECONDS
  shift 2
  printf '\n%s\nLog: %s\n' "$description" "$log"
  { printf '%q ' "$@"; printf '2>&1 | tee %q\n' "$log"; } >> "$OUT/commands.log"
  if "$@" 2>&1 | tee "$log"; then
    printf '\nCompleted: %s (%s s command elapsed)\n' "$description" "$((SECONDS - started_seconds))"
    return 0
  else
    local command_status=$?
    printf '\nFAILED (exit %s): %s\n' "$command_status" "$log" >&2
    tail -n 40 "$log" >&2
    return "$command_status"
  fi
}

if [[ $(uname -s) == Darwin ]]; then
  export SDKROOT
  SDKROOT=$(xcrun --sdk macosx --show-sdk-path)
  if [[ ! -d $SDKROOT ]]; then
    echo "Selected macOS SDK does not exist: $SDKROOT" >&2
    exit 1
  fi
  printf 'macOS SDK: %s\n' "$SDKROOT"
fi

# Check before uv sync: installing the local project may itself invoke CMake.
# Check again afterwards in case dependency synchronization changed Python.
check_build_cache() {
  if [[ ! -f build/CMakeCache.txt ]]; then
    echo "No CMake cache found; configure and build as needed."
    return 0
  fi
  if [[ ! -x .venv/bin/python ]]; then
    echo "Cannot validate the existing CMake cache without .venv/bin/python. Rerun with BUILD_MODE=clean." >&2
    return 1
  fi
  .venv/bin/python - "$REPO_ROOT" "${SDKROOT:-}" <<'BUILD_CACHE_PY'
import os
import re
import shlex
import shutil
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
cache = {}
for line in (root / "build/CMakeCache.txt").read_text().splitlines():
    if line and not line.startswith(("#", "//")) and "=" in line:
        key, value = line.split("=", 1)
        cache[key.split(":", 1)[0]] = value
problems = []
source = cache.get("CMAKE_HOME_DIRECTORY")
if not source or Path(source).resolve() != root:
    problems.append(f"cached source directory is {source!r}, expected {str(root)!r}")
for key in ("PYTHON_EXECUTABLE", "Python_EXECUTABLE"):
    cached = cache.get(key)
    if cached:
        path = Path(cached)
        current = Path(sys.executable)
        if (not path.is_file() or path.resolve() != current.resolve()
                or path.parent.resolve() != current.parent.resolve()):
            problems.append(f"{key} is {cached!r}, expected this virtualenv's Python {sys.executable!r}")
if not any(cache.get(key) for key in ("PYTHON_EXECUTABLE", "Python_EXECUTABLE")):
    problems.append("cached Python executable is missing")
cached_version = cache.get("PYTHON_VERSION") or cache.get("PYTHON_VERSION_STRING")
if cached_version:
    match = re.match(r"(\d+)\.(\d+)(?:\.(\d+))?", cached_version)
    if match:
        version = tuple(int(part) for part in match.groups() if part is not None)
        if version != sys.version_info[:len(version)]:
            problems.append(f"cached Python version is {cached_version}, current is {sys.version.split()[0]}")
sdk = sys.argv[2]
cached_sdk = cache.get("CMAKE_OSX_SYSROOT")
if sdk and (not cached_sdk or Path(cached_sdk).resolve() != Path(sdk).resolve()):
    problems.append(f"cached SDK is {cached_sdk!r}, selected SDK is {sdk!r}")
for variable, key in (("CC", "CMAKE_C_COMPILER"), ("CXX", "CMAKE_CXX_COMPILER")):
    compiler = cache.get(key)
    if not compiler or not os.access(compiler, os.X_OK):
        problems.append(f"cached {key} executable is missing: {compiler!r}")
    requested = shlex.split(os.environ.get(variable, ""))
    if requested:
        selected = shutil.which(requested[0])
        if not selected or not compiler or Path(selected).resolve() != Path(compiler).resolve():
            problems.append(f"{variable} selects {requested[0]!r}, cached {key} is {compiler!r}")
generator = os.environ.get("CMAKE_GENERATOR")
if generator and generator != cache.get("CMAKE_GENERATOR"):
    problems.append(f"CMAKE_GENERATOR selects {generator!r}, cached generator is {cache.get('CMAKE_GENERATOR')!r}")
if problems:
    raise SystemExit("Incompatible CMake cache:\n  - " + "\n  - ".join(problems)
                     + "\nRerun with BUILD_MODE=clean to archive and rebuild build/ and externals/.")
print("CMake cache paths are compatible; CMake will check which targets need rebuilding.")
BUILD_CACHE_PY
}

if [[ $BUILD_MODE == clean ]]; then
  # Preserve previous outputs, but carry no caches/libraries into a clean run.
  for directory in build externals; do
    if [[ -e $directory || -L $directory ]]; then
      printf 'Preserving %s/ in %s/previous-%s\n' "$directory" "$OUT" "$directory"
      mv "$directory" "$OUT/previous-$directory"
    fi
  done
else
  echo "Reusing build/ and externals/; unchanged CMake targets will not be recompiled."
  check_build_cache
fi

run_logged "$OUT/dependencies.log" "Setup 1/5: install build, test and benchmark dependencies" \
  uv sync --group build --group dev --group perf
if [[ $BUILD_MODE == incremental ]]; then
  check_build_cache
fi
run_logged "$OUT/build.log" "Setup 2/5: build/install Python extension and native dependencies as needed" \
  env USE_SYSTEM_DEPS=OFF BUILD_THREADS="$BUILD_THREADS" \
  uv run --no-sync python local_build.py
run_logged "$OUT/build-scaling.log" "Setup 3/5: check/build the standalone C++ scaling executable" \
  uv run --no-sync cmake --build build \
  --target run_scaling.exe --parallel "$BUILD_THREADS"
run_logged "$OUT/tests.log" "Setup 4/5: validate damaged-page handling" \
  uv run --no-sync pytest tests/test_unit_damaged_pages.py -q

# Resolve the same snapshot for every runner. Record ordered file identities,
# counts and hashes before timing; download/hash/page-count work is excluded.
printf '\nSetup 5/5: resolve %s, verify PDF hashes and count pages\n' "$DATASET_REPOSITORY"
uv run --no-sync python - "$OUT" "$DATASET_REVISION" "$REPEATS" "$THREADS" "$RUN_COMPARISON" \
  "$DATASET_REPOSITORY" "$dataset_default_revision" <<'PY'
import hashlib
import importlib.metadata
import importlib.util
import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path

import pypdfium2
from huggingface_hub import snapshot_download
from tqdm import tqdm

out = Path(sys.argv[1])
revision = sys.argv[2]
repository = sys.argv[6]
snapshot = Path(snapshot_download(
    repo_id=repository, repo_type="dataset",
    revision=revision, allow_patterns=["pdf/**"],
))
root = snapshot / "pdf"
files = []
for path in tqdm(sorted(root.rglob("*.pdf")), desc="Verify PDFs and count pages", unit="PDF"):
    with pypdfium2.PdfDocument(path) as document:
        pages = len(document)
    files.append({"name": str(path.relative_to(root)), "path": str(path),
                  "resolved_path": str(path.resolve()), "pages": pages,
                  "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
if not files or any(f["pages"] < 1 for f in files):
    raise SystemExit("Dataset must contain readable, nonempty PDFs")
expected_counts = None
if snapshot.name == sys.argv[7]:
    expected_counts = {"bo20": (20, 496), "bo767": (767, 54730),
                       "bo10k": (10000, 213538)}[repository.rsplit("-", 1)[-1]]
elif (repository == "docling-project/performance-dataset-bo767"
      and snapshot.name == "c684df54de2081a6de1192cee12713b54759c6bb"):
    expected_counts = (753, 54584)
actual_counts = (len(files), sum(f["pages"] for f in files))
if expected_counts is not None and actual_counts != expected_counts:
    raise SystemExit(f"Unexpected counts for {repository} at {snapshot.name}: "
                     f"expected {expected_counts[0]} PDFs / {expected_counts[1]} pages; "
                     f"found {actual_counts[0]} PDFs / {actual_counts[1]} pages")

def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

extension = importlib.util.find_spec("docling_parse.pdf_parsers").origin
header = Path("externals/include/qpdf/DLL.h").read_text()
qpdf = re.search(r'^#\s*define QPDF_VERSION "([^"+]+)"', header, re.M).group(1)
metadata = {
    "schema_version": 1, "dataset": {"repository": repository,
        "requested_revision": revision, "revision": snapshot.name,
        "root": str(root), "files": files},
    "repeats": int(sys.argv[3]), "threads": [int(x) for x in sys.argv[4].split(",")],
    "comparison": sys.argv[5] == "1", "scale": 1.0, "max_concurrent_results": 32,
    "build_mode": os.environ.get("BUILD_MODE", "incremental"),
    "native_profiles": {
        "render-only": {"create_word_cells": False, "create_line_cells": False},
        "render-with-text": {"create_word_cells": True, "create_line_cells": True},
    },
    "platform": platform.platform(), "machine": platform.machine(), "python": sys.version,
    "environment": {key: os.environ.get(key) for key in (
        "SDKROOT", "OMP_NUM_THREADS", "DOCLING_NUM_THREADS", "DOCLING_PARSE_FALLBACK_FONT",
        "DOCLING_PARSE_CJK_FALLBACK_FONT", "DOCLING_PARSE_ARABIC_FALLBACK_FONT",
    )},
    "source_revision": git("rev-parse", "HEAD"),
    "source_diff_sha256": hashlib.sha256(git("diff", "HEAD").encode()).hexdigest(),
    "git_status": git("status", "--porcelain"), "qpdf": qpdf,
    "native_sha256": digest("build/run_scaling.exe"),
    "extension": extension, "extension_sha256": digest(extension),
    "packages": {name: importlib.metadata.version(name) for name in
                 ["docling-parse", "docling-core", "pypdfium2", "pillow", "rtree",
                  "pymupdf", "pdfplumber", "pdfminer.six", "pypdf", "matplotlib"]},
    "script_sha256": {str(p): digest(p) for p in Path("scripts/benchmarking").glob("*.py")},
    "launcher_sha256": digest("scripts/benchmarking/refresh_performance_benchmarks.sh"),
    "memory_collection": {"mode": os.environ.get("MEMORY_MODE", "off"),
        "interval_ms": int(os.environ.get("MEMORY_INTERVAL_MS", "50")) if os.environ.get("MEMORY_MODE") == "profile" else None},
}
metadata["comparison_memory"] = dict(metadata["memory_collection"])
for command, key in [(["sysctl", "-n", "machdep.cpu.brand_string"], "cpu"),
                     (["sysctl", "-n", "hw.memsize"], "memory_bytes")]:
    if sys.platform == "darwin":
        metadata[key] = subprocess.check_output(command, text=True).strip()
if sys.platform.startswith("linux"):
    metadata["cpuinfo"] = Path("/proc/cpuinfo").read_text()
    metadata["meminfo"] = Path("/proc/meminfo").read_text()
(out / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
(out / "dataset-path.txt").write_text(str(root) + "\n")
(out / "dataset-revision.txt").write_text(snapshot.name + "\n")
print(f"Dataset: {len(files)} PDFs, {sum(f['pages'] for f in files)} pages; QPDF {qpdf}")
PY
IFS= read -r PDF_ROOT < "$OUT/dataset-path.txt"
IFS= read -r DATASET_REVISION < "$OUT/dataset-revision.txt"
cp build/CMakeCache.txt "$OUT/CMakeCache.txt"
if [[ -f build/CMakeFiles/run_scaling.exe.dir/flags.make ]]; then
  cp build/CMakeFiles/run_scaling.exe.dir/flags.make "$OUT/native-flags.make"
fi
if [[ $RUN_COMPARISON == 1 ]]; then
  run_logged "$OUT/reference-preflight.log" "Preflight: check PyMuPDF regression before corpus measurements" \
    uv run --no-sync python scripts/benchmarking/_performance_runtime.py "$OUT/metadata.json"
fi

# Keep each command array nonempty for Bash 3.2 with nounset (macOS default).
native_runner=(./build/run_scaling.exe)
replay_runner=(uv run --no-sync python scripts/benchmarking/run_performance_benchmarking_with_docling_settings.py)
comparison_runner=(uv run --no-sync python scripts/benchmarking/run_performance_benchmarking.py)
if [[ $BENCHMARK_OUTPUT == compact ]]; then
  native_runner+=(-v WARNING --suppress-config --suppress-timings)
  replay_runner+=(--suppress-config)
  comparison_runner+=(--suppress-config)
fi

experiment=0
total_experiments=$((REPEATS * (3 * ${#thread_counts[@]} + 1 + 2 * RUN_COMPARISON)))
run_experiment() {
  local log=$1
  local description=$2
  shift 2
  experiment=$((experiment + 1))
  local command=("$@")
  if [[ $MEMORY_MODE != off ]]; then
    if [[ $log == */comparison/*/run.log ]]; then
      # The comparison runner isolates its backend/thread series internally.
      command+=(--memory-mode "$MEMORY_MODE" --memory-interval-ms "$MEMORY_INTERVAL_MS")
    else
      # Observe the actual Python process, not uv and its child together.
      if [[ ${command[0]} == uv ]]; then
        command=("$REPO_ROOT/.venv/bin/python" "${command[@]:4}")
      fi
      command=("$REPO_ROOT/.venv/bin/python" scripts/benchmarking/_performance_memory.py
        --memory-mode "$MEMORY_MODE" --memory-interval-ms "$MEMORY_INTERVAL_MS"
        --output "${log%.log}.memory.json" -- "${command[@]}")
    fi
  fi
  run_logged "$log" "Experiment $experiment/$total_experiments | $description" "${command[@]}"
}

for (( repetition=1; repetition<=REPEATS; repetition++ )); do
  replay_dir="$OUT/replay/r$repetition"
  mkdir -p "$replay_dir"
  for profile in render-only render-with-text; do
    native_dir="$OUT/native/$profile/r$repetition"
    mkdir -p "$native_dir"
    create_text=false
    native_label="render images (no word/line cell creation)"
    if [[ $profile == render-with-text ]]; then
      create_text=true
      native_label="render images + construct word/line text geometry"
    fi
    for count in "${thread_counts[@]}"; do
      run_experiment "$native_dir/t$count.log" \
        "Repetition $repetition/$REPEATS: native C++ $native_label, $count thread(s)" \
        "${native_runner[@]}" "$PDF_ROOT" \
        --recursive --mode render --threads "$count" --max-concurrent-results 32 --scale 1 \
        --create-word-cells="$create_text" --create-line-cells="$create_text" \
        --enable-timing --timing-csv "$native_dir/t$count.csv"
    done
  done
  run_experiment "$replay_dir/pdfium.log" \
    "Repetition $repetition/$REPEATS: Python backend replay, PDFium, 1 worker" \
    "${replay_runner[@]}" \
    "$PDF_ROOT" --backend pdfium --dataset-revision "$DATASET_REVISION" \
    --output-dir "$replay_dir/pdfium"
  for count in "${thread_counts[@]}"; do
    run_experiment "$replay_dir/t$count.log" \
      "Repetition $repetition/$REPEATS: Python backend replay, docling-parse, $count thread(s)" \
      "${replay_runner[@]}" \
      "$PDF_ROOT" --backend threaded --threads "$count" \
      --dataset-revision "$DATASET_REVISION" --output-dir "$replay_dir/t$count"
  done
  if [[ $RUN_COMPARISON == 1 ]]; then
    for mode in parse-without-bitmap render; do
      comparison_dir="$OUT/comparison/r$repetition/$mode"
      mkdir -p "$comparison_dir"
      scene=false
      if [[ $mode == render ]]; then scene=true; fi
      run_experiment "$comparison_dir/run.log" \
        "Repetition $repetition/$REPEATS: cross-package $mode comparison, references at 1 worker; docling threads $THREADS" \
        "${comparison_runner[@]}" "$PDF_ROOT" --recursive \
        --mode "$mode" --threads "$THREADS" --scale 1 --max-concurrent-results 32 \
        --3rd-party-backends all --only-threaded \
        --keep-char-cells=true --create-word-cells=false --create-line-cells=false \
        --keep-shapes="$scene" --keep-bitmaps="$scene" \
        --materialize-char-cells=true --materialize-word-cells=false \
        --materialize-line-cells=false --materialize-shapes=false --materialize-bitmaps=false \
        --checkpoint-dir "$comparison_dir/checkpoints" \
        --output-dir "$comparison_dir"
    done
  fi
done

run_logged "$OUT/report.log" "Reporting: validate all measurements and generate tables and figures" \
  uv run --no-sync python \
  scripts/benchmarking/run_performance_report.py "$OUT"
printf '\nComplete. Tables: %s/report/summary.md\nFigures: %s/report/\n' "$OUT" "$OUT"
