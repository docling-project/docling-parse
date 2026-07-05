from __future__ import annotations

import subprocess
import sys


def main() -> int:
    pytest_args = list(sys.argv[1:] or ["./tests"])

    if "--update-groundtruth" not in pytest_args:
        pytest_args.append("--update-groundtruth")
    if not any(arg in {"-q", "-v", "-vv"} for arg in pytest_args):
        pytest_args.append("-q")

    return subprocess.call([sys.executable, "-m", "pytest", *pytest_args])


if __name__ == "__main__":
    raise SystemExit(main())
