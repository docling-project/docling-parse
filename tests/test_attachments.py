"""Attachment extraction tests — binary data on PdfAttachment."""

import logging
from io import BytesIO

import pytest

import docling_parse.pdf_parser as _parser_module
from docling_parse.pdf_parser import DoclingPdfParser


def _build_pdf(objects: dict[int, bytes]) -> bytes:
    """Build a minimal PDF from obj_num -> raw_content bytes."""
    header = b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n"
    parts: list[bytes] = [header]
    offsets: dict[int, int] = {}
    for num in sorted(objects.keys()):
        offsets[num] = sum(len(p) for p in parts)
        parts.append(f"{num} 0 obj\n".encode())
        parts.append(objects[num])
        if not objects[num].endswith(b"\n"):
            parts.append(b"\n")
        parts.append(b"endobj\n")
    xref_offset = sum(len(p) for p in parts)
    max_obj = max(objects.keys())
    parts.append(f"xref\n0 {max_obj + 1}\n".encode())
    parts.append(b"0000000000 65535 f \n")
    for i in range(1, max_obj + 1):
        off = offsets.get(i, 0)
        parts.append(f"{off:010d} 00000 n \n".encode())
    parts.append(f"trailer\n<< /Size {max_obj + 1} /Root 1 0 R >>\n".encode())
    parts.append(f"startxref\n{xref_offset}\n%%EOF\n".encode())
    return b"".join(parts)


def _pdf_single_no_annot(
    data: bytes = b"Hello Attachment\n", name: str = "hello.txt"
) -> bytes:
    ef_len = len(data)
    return _build_pdf(
        {
            1: b"<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles << /Names [ ("
            + name.encode()
            + b") 4 0 R ] >> >> >>",
            2: b"<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
            3: b"<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] >>",
            4: b"<< /Type /Filespec /F ("
            + name.encode()
            + b") /UF ("
            + name.encode()
            + b") /EF << /F 5 0 R /UF 5 0 R >> >>",
            5: b"<< /Type /EmbeddedFile /Subtype /text#2fplain /Length "
            + str(ef_len).encode()
            + b" /Params << /Size "
            + str(ef_len).encode()
            + b" >> >>\nstream\n"
            + data
            + b"\nendstream",
        }
    )


def _pdf_one_file_two_annots(data: bytes = b"Hello\n", name: str = "doc.txt") -> bytes:
    ef_len = len(data)
    return _build_pdf(
        {
            1: b"<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles << /Names [ ("
            + name.encode()
            + b") 4 0 R ] >> >> >>",
            2: b"<< /Type /Pages /Kids [ 3 0 R 6 0 R 7 0 R ] /Count 3 >>",
            3: b"<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] /Annots [ 8 0 R ] >>",
            4: b"<< /Type /Filespec /F ("
            + name.encode()
            + b") /UF ("
            + name.encode()
            + b") /EF << /F 5 0 R /UF 5 0 R >> >>",
            5: b"<< /Type /EmbeddedFile /Length "
            + str(ef_len).encode()
            + b" /Params << /Size "
            + str(ef_len).encode()
            + b" >> >>\nstream\n"
            + data
            + b"\nendstream",
            6: b"<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] >>",
            7: b"<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] /Annots [ 9 0 R ] >>",
            8: b"<< /Type /Annot /Subtype /FileAttachment /Rect [ 100 100 120 120 ] /FS 4 0 R /Name /Paperclip >>",
            9: b"<< /Type /Annot /Subtype /FileAttachment /Rect [ 200 200 220 220 ] /FS 4 0 R /Name /Paperclip >>",
        }
    )


def _pdf_same_name_different_bytes(name: str = "dup.txt") -> bytes:
    data_a = b"hello"
    data_b = b"world"
    return _build_pdf(
        {
            1: b"<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles << /Names [ ("
            + name.encode()
            + b") 4 0 R ("
            + name.encode()
            + b") 6 0 R ] >> >> >>",
            2: b"<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
            3: b"<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] >>",
            4: b"<< /Type /Filespec /F (" + name.encode() + b") /EF << /F 5 0 R >> >>",
            5: b"<< /Length "
            + str(len(data_a)).encode()
            + b" /Params << /Size "
            + str(len(data_a)).encode()
            + b" >> >>\nstream\n"
            + data_a
            + b"\nendstream",
            6: b"<< /Type /Filespec /F (" + name.encode() + b") /EF << /F 7 0 R >> >>",
            7: b"<< /Length "
            + str(len(data_b)).encode()
            + b" /Params << /Size "
            + str(len(data_b)).encode()
            + b" >> >>\nstream\n"
            + data_b
            + b"\nendstream",
        }
    )


def _load(pdf_bytes: bytes):
    parser = DoclingPdfParser(loglevel="fatal")
    return parser.load(path_or_stream=BytesIO(pdf_bytes))


def test_single_attachment_no_annot():
    doc = _load(_pdf_single_no_annot())
    atts = doc.get_attachments()
    assert len(atts) == 1
    att = atts[0]
    assert att.name == "hello.txt"
    assert att.mime_type == "text/plain"
    assert att.size == len(b"Hello Attachment\n")
    assert att.annotations == []
    assert att.data == b"Hello Attachment\n"


def test_one_file_two_annots():
    doc = _load(_pdf_one_file_two_annots())
    atts = doc.get_attachments()
    assert len(atts) == 1
    att = atts[0]
    assert att.name == "doc.txt"
    assert len(att.annotations) == 2
    pages = sorted(a.page_no for a in att.annotations)
    assert pages == [1, 3]
    annots_sorted = sorted(att.annotations, key=lambda a: a.page_no)
    b0 = annots_sorted[0].bbox
    b1 = annots_sorted[1].bbox
    assert (b0.r_x0, b0.r_y0, b0.r_x1, b0.r_y1, b0.r_x2, b0.r_y2, b0.r_x3, b0.r_y3) == (
        100.0,
        100.0,
        120.0,
        100.0,
        120.0,
        120.0,
        100.0,
        120.0,
    )
    assert (b1.r_x0, b1.r_y0, b1.r_x1, b1.r_y1, b1.r_x2, b1.r_y2, b1.r_x3, b1.r_y3) == (
        200.0,
        200.0,
        220.0,
        200.0,
        220.0,
        220.0,
        200.0,
        220.0,
    )
    assert att.data == b"Hello\n"


def test_same_name_different_bytes():
    doc = _load(_pdf_same_name_different_bytes())
    atts = doc.get_attachments()
    assert len(atts) == 2
    assert [a.name for a in atts] == ["dup.txt", "dup.txt"]
    assert atts[0].data == b"hello"
    assert atts[1].data == b"world"


def test_get_attachments_cached():
    doc = _load(_pdf_single_no_annot())
    first = doc.get_attachments()
    second = doc.get_attachments()
    assert first is second


def test_attachment_above_cap_has_no_data(monkeypatch, caplog):
    monkeypatch.setattr(_parser_module, "_MAX_ATTACHMENT_DATA_BYTES", 1024)
    with caplog.at_level(logging.WARNING, logger="docling_parse.pdf_parser"):
        doc = _load(_pdf_single_no_annot(data=b"x" * 2048, name="big.bin"))
        atts = doc.get_attachments()
    assert len(atts) == 1
    assert atts[0].size == 2048
    assert atts[0].data is None
    assert any("big.bin" in r.message for r in caplog.records)


def test_attachment_at_cap_is_decoded(monkeypatch):
    monkeypatch.setattr(_parser_module, "_MAX_ATTACHMENT_DATA_BYTES", 1024)
    doc = _load(_pdf_single_no_annot(data=b"y" * 1024, name="exact.bin"))
    atts = doc.get_attachments()
    assert atts[0].data == b"y" * 1024


def test_get_attachment_data_primitive_requires_max_size():
    doc = _load(_pdf_single_no_annot())
    assert doc.get_attachment_data(0, max_size=10_000) == b"Hello Attachment\n"
    with pytest.raises(TypeError):
        doc.get_attachment_data(0)  # type: ignore[call-arg]


def _pdf_no_attachments() -> bytes:
    return _build_pdf(
        {
            1: b"<< /Type /Catalog /Pages 2 0 R >>",
            2: b"<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
            3: b"<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] >>",
        }
    )


def test_page_attachments_anchored_vs_unanchored():
    doc = _load(_pdf_one_file_two_annots())
    page1 = doc.get_page(1)
    page2 = doc.get_page(2)
    page3 = doc.get_page(3)
    assert [a.name for a in page1.attachments] == ["doc.txt"]
    assert page1.attachments[0].data == b"Hello\n"
    assert page2.attachments == []
    assert [a.name for a in page3.attachments] == ["doc.txt"]

    doc2 = _load(_pdf_single_no_annot())
    assert [a.name for a in doc2.get_page(1).attachments] == []
    assert [a.name for a in doc2.get_attachments()] == ["hello.txt"]


def test_iterate_pages_carries_attachments():
    doc = _load(_pdf_one_file_two_annots())
    pages = dict(doc.iterate_pages())
    assert sorted(pages) == [1, 2, 3]
    assert [a.name for a in pages[1].attachments] == ["doc.txt"]
    assert pages[2].attachments == []
    assert [a.name for a in pages[3].attachments] == ["doc.txt"]


def test_empty_pdf_no_attachments():
    doc = _load(_pdf_no_attachments())
    assert doc.get_attachments() == []
    assert doc.get_page(1).attachments == []
