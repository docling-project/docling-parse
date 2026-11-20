"""Verify atomic output and interoperability at the public PDF API."""

from __future__ import annotations

import os
import shutil
import stat
import subprocess
import sys
from io import BytesIO

import pypdf
import pytest

from docling_parse import PdfEditor, PdfReader, PdfWriter, _pdf_output
from docling_parse.pdf_errors import PdfWriteError
from tests.test_unit_pdf_compat import writer_bytes
from tests.test_unit_pdf_editor import THREE


class ShortStream(BytesIO):
    def write(self, data):
        return super().write(data[:53])


def test_write_completes_short_stream_writes():
    with PdfEditor(source=THREE) as editor:
        stream = ShortStream()
        editor.write(target=stream)
        assert stream.getvalue() == editor.to_bytes()
        assert not stream.closed


@pytest.mark.parametrize("count", [None, 0, -1, 10**9])
def test_write_rejects_invalid_stream_write_counts(count):
    class StalledStream(BytesIO):
        def write(self, data):
            return count

    with PdfEditor(source=THREE) as editor:
        stream = StalledStream()
        with pytest.raises(PdfWriteError, match="write count"):
            editor.write(target=stream)
        assert not stream.closed


@pytest.mark.skipif(os.name == "nt", reason="POSIX ownership and mode bits")
def test_new_and_replaced_outputs_have_expected_permissions(tmp_path):
    target = tmp_path / "out.pdf"
    with PdfEditor(source=THREE) as editor:
        editor.write(target=target)
        assert stat.S_IMODE(target.stat().st_mode) == 0o600
        target.chmod(0o640)
        before = target.stat()
        editor.add_metadata(infos={"Title": "New"})
        editor.write(target=target)
    after = target.stat()
    assert (after.st_uid, after.st_gid) == (before.st_uid, before.st_gid)
    assert stat.S_IMODE(after.st_mode) == 0o640
    assert pypdf.PdfReader(target).metadata.title == "New"


@pytest.mark.skipif(os.name == "nt", reason="Creating symlinks can require privileges")
def test_path_write_follows_symlinks_including_dangling_links(tmp_path):
    target = tmp_path / "file.pdf"
    alias = tmp_path / "alias.pdf"
    alias.symlink_to(target.name)
    with PdfEditor(source=THREE) as editor:
        editor.write(target=alias)
        assert alias.is_symlink()
        assert target.read_bytes() == editor.to_bytes()
        editor.add_metadata(infos={"Title": "New"})
        editor.write(target=alias)
    assert alias.is_symlink()
    assert pypdf.PdfReader(target).metadata.title == "New"


def test_path_write_rejects_a_multiply_linked_file(tmp_path):
    target = tmp_path / "file.pdf"
    target.write_bytes(b"original")
    alias = tmp_path / "alias.pdf"
    os.link(target, alias)
    with PdfEditor(source=THREE) as editor:
        with pytest.raises(PdfWriteError, match="multiple hard links"):
            editor.write(target=target)
    assert target.read_bytes() == alias.read_bytes() == b"original"
    assert set(tmp_path.iterdir()) == {target, alias}


@pytest.mark.parametrize("operation", ["fsync", "replace"])
def test_failed_path_write_preserves_existing_file_and_cleans_up(
    monkeypatch, tmp_path, operation
):
    target = tmp_path / "out.pdf"
    target.write_bytes(b"original")

    def fail(*args):
        raise OSError(f"{operation} failed")

    monkeypatch.setattr(_pdf_output.os, operation, fail)
    with PdfEditor(source=THREE) as editor:
        with pytest.raises(PdfWriteError, match=f"{operation} failed"):
            editor.write(target=target)
    assert target.read_bytes() == b"original"
    assert list(tmp_path.iterdir()) == [target]


@pytest.mark.skipif(sys.platform != "darwin", reason="macOS extended ACL")
def test_path_write_preserves_extended_acl(tmp_path):
    target = tmp_path / "out.pdf"
    target.write_bytes(b"original")
    subprocess.run(["chmod", "+a", "everyone deny execute", str(target)], check=True)

    def acl():
        return subprocess.check_output(
            ["ls", "-le", str(target)], text=True
        ).splitlines()[1:]

    before = acl()
    assert before
    with PdfEditor(source=THREE) as editor:
        editor.write(target=target)
    assert acl() == before


@pytest.mark.skipif(
    not sys.platform.startswith("linux") or not shutil.which("setfacl"),
    reason="Linux ACL test needs setfacl",
)
def test_path_write_preserves_posix_acl(tmp_path):
    target = tmp_path / "out.pdf"
    target.write_bytes(b"original")
    subprocess.run(["setfacl", "-m", "u:65534:r", str(target)], check=True)
    before = os.getxattr(target, "system.posix_acl_access")
    with PdfEditor(source=THREE) as editor:
        editor.write(target=target)
    assert os.getxattr(target, "system.posix_acl_access") == before


@pytest.mark.skipif(os.name != "nt", reason="Windows file access descriptor")
def test_windows_path_write_preserves_security_descriptor(tmp_path):
    import ntsecuritycon  # type: ignore[import-untyped]
    import win32security as security  # type: ignore[import-untyped]

    target = tmp_path / "out.pdf"
    flags = (
        security.DACL_SECURITY_INFORMATION
        | security.OWNER_SECURITY_INFORMATION
        | security.GROUP_SECURITY_INFORMATION
    )

    def descriptor():
        return security.GetNamedSecurityInfo(
            str(target), security.SE_FILE_OBJECT, flags
        )

    def sddl():
        return security.ConvertSecurityDescriptorToStringSecurityDescriptor(
            descriptor(), 1, flags
        )

    with PdfEditor(source=THREE) as editor:
        editor.write(target=target)
        acl = descriptor().GetSecurityDescriptorDacl()
        system = security.CreateWellKnownSid(security.WinLocalSystemSid, None)
        acl.AddAccessAllowedAce(
            security.ACL_REVISION, ntsecuritycon.FILE_GENERIC_READ, system
        )
        security.SetNamedSecurityInfo(
            str(target),
            security.SE_FILE_OBJECT,
            security.DACL_SECURITY_INFORMATION
            | security.PROTECTED_DACL_SECURITY_INFORMATION,
            None,
            None,
            acl,
            None,
        )
        before = sddl()
        editor.write(target=target)
        assert sddl() == before


@pytest.mark.parametrize("writer_class", [PdfWriter, pypdf.PdfWriter])
def test_writer_context_writes_bound_path_or_stream(writer_class, tmp_path):
    path = tmp_path / "out.pdf"
    with writer_class(path) as writer:
        writer.add_blank_page(width=100, height=200)
        assert not path.exists()
    original = path.read_bytes()
    assert len(pypdf.PdfReader(BytesIO(original)).pages) == 1

    with writer_class(path) as writer:
        writer.add_blank_page(width=100, height=200)
    assert path.read_bytes() == original

    stream = BytesIO()
    with (
        pytest.raises(RuntimeError, match="body failed"),
        writer_class(stream) as writer,
    ):
        writer.add_blank_page(width=100, height=200)
        raise RuntimeError("body failed")
    assert not stream.closed
    assert len(pypdf.PdfReader(stream).pages) == 1


@pytest.mark.parametrize(
    "algorithm", ["RC4-40", "RC4-128", "AES-128", "AES-256-R5", "AES-256"]
)
@pytest.mark.parametrize(
    ("user", "owner"),
    [
        ("user", "owner"),
        ("clé-élève", "maître-ö"),
        ("Å\N{SOFT HYPHEN}東京", "①maître"),
        (b"user-\x87\xfe", b"owner-\x92\xff"),
    ],
)
def test_password_interoperability_in_both_directions(algorithm, user, owner):
    with PdfWriter(clone_from=THREE) as writer:
        writer.encrypt(user, owner, algorithm=algorithm)
        data = writer_bytes(writer)
    for password, expected in ((user, 1), (owner, 2)):
        reader = pypdf.PdfReader(BytesIO(data))
        assert reader.decrypt(password) == expected
        assert reader.pages[0].extract_text() == "alpha"

    writer = pypdf.PdfWriter(clone_from=BytesIO(THREE))
    writer.encrypt(user, owner, algorithm=algorithm)
    data = writer_bytes(writer)
    for password in (user, owner):
        with PdfReader(data, password=password) as reader:
            assert reader.pages[0].extract_text() == "alpha"


def test_password_fallback_remains_available_when_cloning_and_importing(tmp_path):
    writer = pypdf.PdfWriter(clone_from=BytesIO(THREE))
    writer.encrypt("clé", algorithm="AES-128")
    path = tmp_path / "encrypted.pdf"
    path.write_bytes(writer_bytes(writer))

    with PdfEditor(source=path, password="clé") as source, PdfEditor() as editor:
        editor.append(source=source, page_nos=[1])
        assert editor.extract_text(page_no=1) == "alpha"
        source.rotate_page(page_no=1, angle=90)
        assert source.get_page(page_no=1).rotation == 90


def test_read_only_serialization_keeps_source_state_and_handles():
    writer = pypdf.PdfWriter(clone_from=BytesIO(THREE))
    writer.encrypt("secret", algorithm="AES-128")
    with PdfEditor(source=writer_bytes(writer), password="secret") as editor:
        page = editor.get_page(page_no=1)
        before = editor.encryption
        data = editor.to_bytes()
        assert editor.to_bytes() == data
        assert not editor.is_modified
        assert editor.encryption == before
        assert editor.get_page_no(page=page) == 1
        assert not pypdf.PdfReader(BytesIO(data)).is_encrypted

        editor.encrypt(user_password=b"other-\xf0", algorithm="AES-128")
        pending = editor.encryption
        assert pypdf.PdfReader(BytesIO(editor.to_bytes())).is_encrypted
        assert editor.is_modified
        assert editor.encryption == pending
        assert editor.get_page_no(page=page) == 1
        assert editor.extract_text(page_no=1) == "alpha"


@pytest.mark.parametrize(
    "algorithm", ["RC4-40", "RC4-128", "AES-128", "AES-256-R5", "AES-256"]
)
def test_live_encryption_metadata_matches_the_serialized_permissions(algorithm):
    with PdfEditor(source=THREE) as editor:
        editor.encrypt(
            user_password="secret",
            algorithm=algorithm,
            allow_print="low",
            allow_modify=False,
            allow_accessibility=False,
            allow_annotate=False,
            allow_fill_forms=False,
            allow_extract=False,
            allow_assemble=False,
        )
        editor.add_metadata(infos={"Title": "Edited"})
        assert editor.metadata["Title"] == "Edited"
        projected = editor.encryption
        with PdfReader(editor.to_bytes(), password="secret") as output:
            assert output.encryption == projected


def test_embedded_nul_password_is_rejected_instead_of_truncated():
    with PdfEditor(source=THREE) as editor:
        with pytest.raises(ValueError, match="NUL"):
            editor.encrypt(user_password=b"secret\0suffix")
        assert not editor.is_modified
    with pytest.raises(ValueError, match="NUL"):
        PdfReader(THREE, password=b"secret\0suffix")


def test_slash_prefixed_annotation_text_stays_a_pdf_string():
    with PdfEditor(source=THREE) as editor:
        editor.add_uri(page_no=1, uri="/relative/path", rect=(1, 1, 10, 10))
        editor.add_annotation(
            page_no=1,
            annotation={
                "/Subtype": "/Text",
                "/Rect": [1, 1, 10, 10],
                "/Contents": "/review ✓",
                "/Name": "/Comment",
            },
        )
        pdf = pypdf.PdfReader(BytesIO(editor.to_bytes()))
    link, note = [reference.get_object() for reference in pdf.pages[0]["/Annots"]]
    assert isinstance(link["/A"]["/URI"], pypdf.generic.TextStringObject)
    assert link["/A"]["/URI"] == "/relative/path"
    assert isinstance(note["/Contents"], pypdf.generic.TextStringObject)
    assert note["/Contents"] == "/review ✓"
    assert isinstance(note["/Name"], pypdf.generic.NameObject)


def test_auto_javascript_names_fill_unused_slots():
    with PdfEditor(source=THREE) as editor:
        editor.add_js(javascript="custom", name="script-2")
        editor.add_js(javascript="first")
        editor.add_js(javascript="third")
        assert editor.javascript == {
            "script-1": "first",
            "script-2": "custom",
            "script-3": "third",
        }
        assert editor.remove_javascript() == 3
        editor.add_js(javascript="after removal")
        with PdfReader(editor.to_bytes()) as reader:
            assert reader.javascript == {"script-1": "after removal"}


@pytest.mark.parametrize("encoding", ["utf-16", "utf-16-be", "utf-16-le", "iso-8859-1"])
@pytest.mark.parametrize("as_bytes", [True, False])
def test_xmp_round_trip_respects_byte_order_and_xml_encoding(encoding, as_bytes):
    xml = f'<?xml version="1.0" encoding="{encoding}"?><title>café</title>'
    packet = xml.encode(encoding) if as_bytes else xml
    with PdfEditor(source=THREE) as editor:
        editor.set_xmp_metadata(packet=packet)
        assert editor.xmp_metadata == xml
        with PdfReader(editor.to_bytes()) as reader:
            assert reader.xmp_metadata == xml
