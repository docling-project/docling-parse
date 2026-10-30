#!/usr/bin/env python
"""Capture isolated page renders and compare a Blend2D patch against pypdfium2.

Run capture before and after rebuilding, then report on the same inputs::

    python scripts/compare_blend2d_patches.py capture --output /tmp/blend2d/before
    python scripts/compare_blend2d_patches.py capture --output /tmp/blend2d/after
    python scripts/compare_blend2d_patches.py report --before /tmp/blend2d/before \
        --after /tmp/blend2d/after --output /tmp/blend2d/report

Capture defaults to EVERY page of the local regression PDFs. Each page runs in
a child with a timeout, so native crashes/hangs are recorded. --pdf/--page allow
cited documents outside the dataset without redistributing them. --package-root
selects a saved baseline package; capture records the extension's SHA-256.
Reports contain every page, both reference metrics, failures, and three-panel
images for changed/failed pages. A failed baseline has no invented pixel metrics.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _worker(args: argparse.Namespace) -> None:
    if args.package_root:
        from importlib import import_module

        package_root = args.package_root.resolve()
        if not (package_root / "docling_parse/__init__.py").is_file():
            raise ValueError(f"missing baseline package: {package_root}")
        sys.path.insert(0, str(package_root))
        native = import_module("docling_parse.pdf_parsers")
        native_file = native.__file__
        if native_file is None or not Path(native_file).resolve().is_relative_to(
            package_root
        ):
            raise ValueError("requested package root did not supply the extension")
    from tests.pdf_builder import render_page

    result = render_page(args.pdf.read_bytes(), page_no=args.page, scale=args.scale)
    result.get_image().save(args.output)


def _capture_page(entry: dict[str, Any], args: argparse.Namespace) -> dict[str, Any]:
    started = time.monotonic()
    command = [
        sys.executable,
        str(Path(__file__).resolve()),
        "worker",
        "--pdf",
        entry["path"],
        "--page",
        str(entry["page"]),
        "--scale",
        str(args.scale),
        "--output",
        str(args.output / entry["image"]),
    ]
    if args.package_root:
        command.extend(["--package-root", str(args.package_root.resolve())])
    try:
        process = subprocess.run(
            command, capture_output=True, text=True, timeout=args.timeout, cwd=ROOT
        )
        status = "ok" if process.returncode == 0 else "failed"
        detail = process.stderr[-2000:] if process.returncode else ""
        code = process.returncode
    except subprocess.TimeoutExpired:
        status, detail, code = "timeout", f"exceeded {args.timeout}s", None
    return {
        **entry,
        "status": status,
        "returncode": code,
        "detail": detail,
        "seconds": time.monotonic() - started,
    }


def capture(args: argparse.Namespace) -> int:
    import pypdfium2 as pdfium

    args.output = args.output.resolve()
    if not args.pdf:
        from tests.constants import HF_DATASET_REVISION

        marker = ROOT / "tests/data/.hf_revision"
        if not marker.is_file() or marker.read_text().strip() != HF_DATASET_REVISION:
            raise ValueError("synchronize the pinned regression dataset before capture")
    paths = args.pdf or sorted((ROOT / "tests/data/regression").glob("*.pdf"))
    if not paths:
        raise ValueError("no PDF inputs")
    if args.output.exists() and any(args.output.iterdir()):
        raise ValueError("capture output must be empty to avoid mixing builds")
    args.output.mkdir(parents=True, exist_ok=True)
    entries: list[dict[str, Any]] = []
    for path in paths:
        path = path.resolve()
        digest = _sha256(path)
        with pdfium.PdfDocument(path) as document:
            pages = args.page or list(range(1, len(document) + 1))
            if any(page < 1 or page > len(document) for page in pages):
                raise ValueError(f"invalid page selection for {path}")
        entries.extend(
            {
                "document": path.name,
                "path": str(path),
                "sha256": digest,
                "page": page,
                "image": f"{path.name}.{digest[:12]}.page-{page}.png",
            }
            for page in pages
        )
    keys = [(entry["document"], entry["sha256"], entry["page"]) for entry in entries]
    if len(keys) != len(set(keys)):
        raise ValueError("duplicate PDF/page inputs")
    package = (args.package_root or ROOT) / "docling_parse"
    extensions = sorted(package.glob("pdf_parsers*.so")) + sorted(
        package.glob("pdf_parsers*.pyd")
    )
    if not extensions:
        raise ValueError(f"no extension found in {package}")
    metadata = {
        "label": args.label,
        "scale": args.scale,
        "timeout": args.timeout,
        "expected_pages": len(entries),
        "python": sys.version,
        "corpus_revision": (ROOT / "tests/data/.hf_revision").read_text().strip()
        if (ROOT / "tests/data/.hf_revision").exists()
        else None,
        "extensions": {str(path): _sha256(path) for path in extensions},
        "entries": entries,
    }
    (args.output / "manifest.json").write_text(json.dumps(metadata, indent=2))
    failures = 0
    with (args.output / "results.jsonl").open("w") as results:
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(_capture_page, entry, args) for entry in entries]
            for count, future in enumerate(as_completed(futures), 1):
                record = future.result()
                results.write(json.dumps(record) + "\n")
                results.flush()
                failures += record["status"] != "ok"
                if count % 100 == 0 or record["status"] != "ok":
                    print(
                        f"{count}/{len(entries)}: {record['document']}@"
                        f"{record['page']} {record['status']}",
                        flush=True,
                    )
    print(f"Captured {len(entries)} pages; {failures} failed/timed out.")
    return int(failures > 0)


def _read_capture(folder: Path) -> tuple[dict[str, Any], dict[tuple, dict]]:
    manifest = json.loads((folder / "manifest.json").read_text())
    records = [
        json.loads(line) for line in (folder / "results.jsonl").read_text().splitlines()
    ]
    indexed = {
        (record["document"], record["sha256"], record["page"]): record
        for record in records
    }
    if len(indexed) != manifest["expected_pages"] or len(indexed) != len(records):
        raise ValueError(f"incomplete or duplicate capture: {folder}")
    expected = {
        (entry["document"], entry["sha256"], entry["page"])
        for entry in manifest["entries"]
    }
    if set(indexed) != expected:
        raise ValueError(f"capture results differ from manifest: {folder}")
    return manifest, indexed


def report(args: argparse.Namespace) -> int:
    from dataclasses import asdict

    from PIL import Image, ImageDraw, ImageFont

    from tests.rendering_regression import (
        flatten_on_white,
        measure_image_pair,
        render_pypdfium_page,
    )
    from tests.test_regression_pypdfium_render import PYPDFIUM_IMAGE_TOLERANCE

    before_manifest, before = _read_capture(args.before)
    after_manifest, after = _read_capture(args.after)
    if (
        before.keys() != after.keys()
        or before_manifest["scale"] != after_manifest["scale"]
    ):
        raise ValueError("before/after inputs or scales differ")
    args.output.mkdir(parents=True, exist_ok=True)
    rows, errors = [], []
    for key in sorted(before):
        old, new = before[key], after[key]
        row: dict[str, Any] = {
            "document": new["document"],
            "page": new["page"],
            "sha256": new["sha256"],
        }
        try:
            path = Path(new["path"])
            if _sha256(path) != new["sha256"]:
                raise ValueError("PDF changed since capture")
            reference = flatten_on_white(
                render_pypdfium_page(path, new["page"], scale=after_manifest["scale"])
            )
            images: dict[str, Any] = {}
            for label, record, folder in (
                ("before", old, args.before),
                ("after", new, args.after),
            ):
                row[label] = {"status": record["status"], "detail": record["detail"]}
                if record["status"] == "ok":
                    with Image.open(folder / record["image"]) as captured_image:
                        actual = flatten_on_white(captured_image).copy()
                    images[label] = actual
                    row[label]["metrics"] = asdict(
                        measure_image_pair(
                            new["document"],
                            new["page"],
                            actual,
                            reference,
                            tolerance=PYPDFIUM_IMAGE_TOLERANCE,
                        )
                    )
            if new["status"] != "ok":
                errors.append(f"{new['document']}@{new['page']}: patched render failed")
            elif old["status"] == "ok":
                if (
                    row["before"]["metrics"]["size_matches"]
                    and not row["after"]["metrics"]["size_matches"]
                ):
                    errors.append(
                        f"{new['document']}@{new['page']}: new image-size mismatch"
                    )
                for metric in ("mean_abs_error", "changed_pixels_ratio"):
                    increase = (
                        row["after"]["metrics"][metric]
                        - row["before"]["metrics"][metric]
                    )
                    if increase > getattr(PYPDFIUM_IMAGE_TOLERANCE, metric):
                        errors.append(
                            f"{new['document']}@{new['page']}: {metric} increased by {increase}"
                        )
            changed = (
                len(images) != 2
                or images["before"].tobytes() != images["after"].tobytes()
                or images["before"].size != images["after"].size
            )
            if changed or args.all_panels:
                width, height = reference.size
                panel = Image.new("RGB", (width * 3, height + 32), "white")
                draw = ImageDraw.Draw(panel)
                try:
                    font = ImageFont.load_default(size=max(12, min(24, width // 40)))
                except TypeError:  # Pillow 10.0 lacks the size argument.
                    font = ImageFont.load_default()
                for column, label in enumerate(("pypdfium2", "before", "after")):
                    draw.text((column * width + 8, 4), label, fill="black", font=font)
                    image = reference if column == 0 else images.get(label)
                    if image is None:
                        failed = old if label == "before" else new
                        description = (
                            f"{failed['status']} (exit {failed['returncode']})"
                        )
                        draw.text(
                            (column * width + 16, height // 2),
                            description,
                            fill="red",
                            font=font,
                        )
                    else:
                        panel.paste(image.resize(reference.size), (column * width, 32))
                panel.save(args.output / new["image"])
            row["pixels_changed"] = changed
        except Exception as exc:
            row["error"] = f"{type(exc).__name__}: {exc}"
            errors.append(f"{new['document']}@{new['page']}: {row['error']}")
        rows.append(row)
        if len(rows) % 100 == 0:
            print(f"Compared {len(rows)}/{len(before)} pages", flush=True)
    summary = {
        "before": before_manifest,
        "after": after_manifest,
        "pages": rows,
        "errors": errors,
    }
    (args.output / "report.json").write_text(json.dumps(summary, indent=2))
    lines = [
        "# Blend2D before/after comparison",
        "",
        "Tolerance applies to increases: MAE <= 9.0; changed-pixel ratio <= 0.09.",
        "Metrics use the existing RGBA/white-background comparison; changed pixels have grayscale error > 12.",
        "",
        "| Document | Page | Before MAE | After MAE | Before changed ratio | After changed ratio |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in rows:
        values = []
        for metric in ("mean_abs_error", "changed_pixels_ratio"):
            for label in ("before", "after"):
                part = row.get(label, {})
                metrics = part.get("metrics")
                values.append(
                    f"{metrics[metric]:.8f}" if metrics else part.get("status", "error")
                )
        lines.append(
            f"| {row['document']} | {row['page']} | " + " | ".join(values) + " |"
        )
    comparable = [
        row
        for row in rows
        if all("metrics" in row.get(label, {}) for label in ("before", "after"))
    ]
    lines.extend(
        [
            "",
            f"Pages: {len(rows)}; paired metrics: {len(comparable)}; errors: {len(errors)}.",
            "",
        ]
    )
    for metric in ("mean_abs_error", "changed_pixels_ratio"):
        means = [
            sum(row[label]["metrics"][metric] for row in comparable) / len(comparable)
            if comparable
            else None
            for label in ("before", "after")
        ]
        lines.append(
            f"Page-weighted {metric} (paired pages): {means[0]} -> {means[1]}."
        )
    lines.extend(["", *errors])
    (args.output / "report.md").write_text("\n".join(lines) + "\n")
    print(
        f"Compared {len(rows)} pages; {len(errors)} errors. Report: {args.output / 'report.md'}"
    )
    return int(bool(errors))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="mode", required=True)
    for mode in ("capture", "worker"):
        sub = subparsers.add_parser(mode)
        sub.add_argument(
            "--pdf",
            type=Path,
            **({"action": "append"} if mode == "capture" else {"required": True}),
        )
        sub.add_argument(
            "--page",
            type=int,
            **({"action": "append"} if mode == "capture" else {"required": True}),
        )
        sub.add_argument("--scale", type=float, default=2.0)
        sub.add_argument("--output", type=Path, required=True)
        sub.add_argument("--package-root", type=Path)
        if mode == "capture":
            sub.add_argument("--timeout", type=float, default=60)
            sub.add_argument("--jobs", type=int, default=4)
            sub.add_argument("--label", default="")
    sub = subparsers.add_parser("report")
    for name in ("before", "after", "output"):
        sub.add_argument(f"--{name}", type=Path, required=True)
    sub.add_argument("--all-panels", action="store_true")
    args = parser.parse_args()
    if args.mode == "worker":
        _worker(args)
        return 0
    if args.mode == "capture":
        if args.scale <= 0 or args.jobs < 1 or args.timeout <= 0:
            parser.error("scale, jobs, and timeout must be positive")
        return capture(args)
    return report(args)


if __name__ == "__main__":
    raise SystemExit(main())
