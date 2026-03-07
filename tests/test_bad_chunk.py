import os
import tempfile
from io import BytesIO
from pathlib import Path


def build_base_pdf(num_pages: int = 5) -> bytes:
    from pypdf import PdfWriter

    writer = PdfWriter()
    for _ in range(num_pages):
        writer.add_blank_page(612, 792)
    buf = BytesIO()
    writer.write(buf)
    return buf.getvalue()


def corrupt_count_zero(pdf_bytes: bytes, actual_pages: int) -> bytes:
    count_str = f"/Count {actual_pages}".encode()
    if count_str not in pdf_bytes:
        raise ValueError(f"/Count {actual_pages} not found in PDF")
    return pdf_bytes.replace(count_str, b"/Count 0", 1)


def corrupt_count_invalid(pdf_bytes: bytes, actual_pages: int) -> bytes:
    count_str = f"/Count {actual_pages}".encode()
    if count_str not in pdf_bytes:
        raise ValueError(f"/Count {actual_pages} not found in PDF")
    return pdf_bytes.replace(count_str, b"/Count~1", 1)


def corrupt_count_wrong(pdf_bytes: bytes, actual_pages: int) -> bytes:
    count_str = f"/Count {actual_pages}".encode()
    if count_str not in pdf_bytes:
        raise ValueError(f"/Count {actual_pages} not found in PDF")
    return pdf_bytes.replace(count_str, b"/Count 999", 1)


def corrupt_pages_type(pdf_bytes: bytes, actual_pages: int) -> bytes:
    _ = actual_pages
    if b"/Type /Pages" not in pdf_bytes:
        raise ValueError("/Type /Pages not found in PDF")
    return pdf_bytes.replace(b"/Type /Pages", b"/Type /Xxxxx", 1)


def extract_and_corrupt(pdf_path: Path, start_page: int, end_page: int) -> dict[str, bytes]:
    from pypdf import PdfReader, PdfWriter

    reader = PdfReader(pdf_path)
    end_page = min(end_page, len(reader.pages) - 1)
    actual_pages = end_page - start_page + 1

    writer = PdfWriter()
    for page_num in range(start_page, end_page + 1):
        writer.add_page(reader.pages[page_num])

    buf = BytesIO()
    writer.write(buf)
    clean = buf.getvalue()

    return {
        "clean_chunk": clean,
        "count_zero": corrupt_count_zero(clean, actual_pages),
        "count_invalid": corrupt_count_invalid(clean, actual_pages),
        "count_wrong": corrupt_count_wrong(clean, actual_pages),
        "pages_type_broken": corrupt_pages_type(clean, actual_pages),
    }


def build_dangling_kids_pdf(num_valid: int = 2, num_dangling: int = 1) -> bytes:
    total = num_valid + num_dangling
    font_id = 3 + num_valid
    dangling_start = font_id + 1 + num_valid

    kids_refs = []
    for i in range(num_valid):
        kids_refs.append(f"{3 + i} 0 R")
    for i in range(num_dangling):
        kids_refs.append(f"{dangling_start + i} 0 R")

    objects: list[bytes] = []
    offsets: list[int] = []
    next_obj_id = 1

    def add_obj(data: bytes) -> int:
        nonlocal next_obj_id
        oid = next_obj_id
        objects.append(b"%d 0 obj\n%s\nendobj\n" % (oid, data))
        next_obj_id += 1
        return oid

    add_obj(b"<< /Type /Catalog /Pages 2 0 R >>")

    kids_str = " ".join(kids_refs)
    add_obj(
        b"<< /Type /Pages /Kids ["
        + kids_str.encode()
        + b"] /Count "
        + str(total).encode()
        + b" >>"
    )

    add_obj(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")

    for i in range(num_valid):
        content = f"BT /F1 12 Tf 100 700 Td (Page {i + 1}) Tj ET"
        content_bytes = content.encode("latin-1")

        stream_id = add_obj(
            b"<< /Length "
            + str(len(content_bytes)).encode()
            + b" >>\nstream\n"
            + content_bytes
            + b"\nendstream"
        )

        add_obj(
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792]"
            b" /Contents "
            + str(stream_id).encode()
            + b" 0 R"
            b" /Resources << /Font << /F1 3 0 R >> >> >>"
        )

    total_objects = next_obj_id - 1

    header = b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n"
    body = b""
    for obj_data in objects:
        offsets.append(len(header) + len(body))
        body += obj_data

    xref_offset = len(header) + len(body)
    xref = b"xref\n0 %d\n" % (total_objects + 1)
    xref += b"0000000000 65535 f \n"
    for offset in offsets:
        xref += b"%010d 00000 n \n" % offset

    trailer = (
        b"trailer\n<< /Size %d /Root 1 0 R >>\n" % (total_objects + 1)
        + b"startxref\n%d\n%%%%EOF\n" % xref_offset
    )

    return header + body + xref + trailer


def validate_chunk(pdf_bytes: bytes, name: str) -> dict:
    _ = name
    result = {
        "size_bytes": len(pdf_bytes),
        "pypdfium2_pages": None,
        "docling_parse_pages": None,
        "is_mismatch": False,
    }

    import pypdfium2

    pdf = pypdfium2.PdfDocument(BytesIO(pdf_bytes))
    result["pypdfium2_pages"] = len(pdf)
    pdf.close()

    from docling_parse.pdf_parser import DoclingPdfParser

    with tempfile.NamedTemporaryFile(suffix=".pdf", delete=False) as tmp:
        tmp.write(pdf_bytes)
        tmp_path = tmp.name

    try:
        parser = DoclingPdfParser()
        doc = parser.load(tmp_path)
        if not doc.is_loaded():
            result["docling_parse_pages"] = -1
        else:
            result["docling_parse_pages"] = doc.number_of_pages()
    finally:
        os.unlink(tmp_path)

    pp = result["pypdfium2_pages"]
    dp = result["docling_parse_pages"]

    if pp is not None and dp is not None and pp != dp:
        result["is_mismatch"] = True
    if dp is not None and dp < 0:
        result["is_mismatch"] = True
    if dp == 0 and pp is not None and pp > 0:
        result["is_mismatch"] = True

    return result


def _collect_script_style_chunks(include_extracted: bool) -> dict[str, bytes]:
    all_chunks: dict[str, bytes] = {}

    base_pdf = build_base_pdf(num_pages=10)
    all_chunks["synthetic_clean"] = base_pdf
    all_chunks["synthetic_count_zero"] = corrupt_count_zero(base_pdf, actual_pages=10)
    all_chunks["synthetic_count_invalid"] = corrupt_count_invalid(base_pdf, actual_pages=10)
    all_chunks["synthetic_count_wrong"] = corrupt_count_wrong(base_pdf, actual_pages=10)
    all_chunks["synthetic_pages_type_broken"] = corrupt_pages_type(base_pdf, actual_pages=10)

    all_chunks["dangling_2valid_1missing"] = build_dangling_kids_pdf(2, 1)
    all_chunks["dangling_5valid_3missing"] = build_dangling_kids_pdf(5, 3)
    all_chunks["dangling_8valid_2missing"] = build_dangling_kids_pdf(8, 2)

    if include_extracted:
        extracted = extract_and_corrupt(Path("tests/data/cases/case_18.pdf"), 0, 9)
        for name, chunk_bytes in extracted.items():
            all_chunks[f"extracted_{name}"] = chunk_bytes

    return all_chunks


def _print_validation_matrix(results: dict[str, dict]) -> None:
    print(f"\n{'='*70}")
    print("VALIDATION RESULTS")
    print(f"{'='*70}\n")

    col_name = 35
    print(
        f"  {'Name':<{col_name}} {'Size':>8}  {'pypdfium2':>10}  {'docling-parse':>14}  {'Result'}"
    )
    print(f"  {'-'*col_name} {'-'*8}  {'-'*10}  {'-'*14}  {'-'*10}")

    for name, result in results.items():
        pp_str = (
            str(result["pypdfium2_pages"])
            if result["pypdfium2_pages"] is not None
            else "ERR"
        )
        dp_str = (
            str(result["docling_parse_pages"])
            if result["docling_parse_pages"] is not None
            else "ERR"
        )
        match_str = "MISMATCH" if result["is_mismatch"] else "ok"

        print(
            f"  {name:<{col_name}} {result['size_bytes']:>7}B  {pp_str:>10}  {dp_str:>14}  {match_str}"
        )


def _validate_and_assert_no_mismatch(all_chunks: dict[str, bytes]) -> None:
    results = {name: validate_chunk(pdf_bytes, name) for name, pdf_bytes in all_chunks.items()}
    _print_validation_matrix(results)

    mismatches = [name for name, result in results.items() if result["is_mismatch"]]
    assert not mismatches, f"Found mismatches: {', '.join(mismatches)}"


def test_script_equivalent_without_input_pdf():
    all_chunks = _collect_script_style_chunks(include_extracted=False)
    _validate_and_assert_no_mismatch(all_chunks=all_chunks)


def test_script_equivalent_with_case_18_input_pdf():
    all_chunks = _collect_script_style_chunks(include_extracted=True)
    _validate_and_assert_no_mismatch(all_chunks=all_chunks)
