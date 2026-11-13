"""Run exactly one comparison series in a fresh process; internal entry point."""

from __future__ import annotations

import json
import sys
from dataclasses import asdict
from pathlib import Path
from typing import Any, Callable


def main() -> None:
    from run_performance_benchmarking import COMPARISON_BACKENDS

    request = json.loads(Path(sys.argv[1]).read_text())
    backend = request.pop("backend")
    schedule = [(Path(p), pages) for p, pages in request.pop("pdf_schedule")]
    total_pages = request.pop("total_pages")
    runner: Callable[..., Any] = COMPARISON_BACKENDS[backend][0]  # type: ignore[assignment]
    try:
        run = runner(schedule, total_pages, **request)
    except ImportError as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(5)
    output = Path(sys.argv[2])
    temporary = output.with_suffix(".tmp")
    # Stream the result: a second corpus-sized list/string would inflate the
    # worker's lifetime peak solely to transport observations to its parent.
    with temporary.open("w") as stream:
        stream.write('{"samples":[')
        for index, row in enumerate(run.samples):
            if index:
                stream.write(",")
            json.dump(asdict(row), stream)
        stream.write("]")
        for key in (
            "backend",
            "task",
            "threads",
            "wall_s",
            "per_page_source",
            "doc_errors",
        ):
            stream.write("," + json.dumps(key) + ":")
            json.dump(getattr(run, key), stream)
        stream.write("}\n")
    temporary.replace(output)


if __name__ == "__main__":
    main()
