#!/usr/bin/env bash
[ -n "${BASH_VERSION:-}" ] || exec bash "$0" "$@"
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
exec uv run --project "$repo_root" --locked --group perf python -B \
  "$repo_root/scripts/benchmarking/run_quality_refresh.py" "$@"
