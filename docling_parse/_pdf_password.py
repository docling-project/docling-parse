"""Encode text passwords for the PDF Standard security handler."""

from __future__ import annotations

import stringprep
import unicodedata

_AES_256 = {"AES-256", "AES-256-R5"}
_PROHIBITED = (
    stringprep.in_table_a1,
    stringprep.in_table_c12,
    stringprep.in_table_c21,
    stringprep.in_table_c22,
    stringprep.in_table_c3,
    stringprep.in_table_c4,
    stringprep.in_table_c5,
    stringprep.in_table_c6,
    stringprep.in_table_c7,
    stringprep.in_table_c8,
    stringprep.in_table_c9,
)


def _saslprep(text: str) -> str:
    """RFC 4013 preparation with the permissive UTF-8 fallback used by pypdf."""
    mapped = "".join(
        " " if stringprep.in_table_c12(char) else char
        for char in text
        if not stringprep.in_table_b1(char)
    )
    normalized = unicodedata.normalize("NFKC", mapped)
    if any(check(char) for char in normalized for check in _PROHIBITED):
        return text
    if any(map(stringprep.in_table_d1, normalized)) and (
        any(map(stringprep.in_table_d2, normalized))
        or not stringprep.in_table_d1(normalized[0])
        or not stringprep.in_table_d1(normalized[-1])
    ):
        return text
    return normalized


def _encoded(password: str | bytes, *, aes256: bool) -> bytes:
    if isinstance(password, bytes):
        data = password
    elif isinstance(password, str):
        if aes256:
            data = _saslprep(password).encode("utf-8")
        else:
            try:
                data = password.encode("latin-1")
            except UnicodeEncodeError:
                data = password.encode("utf-8")
    else:
        raise TypeError("a PDF password must be str or bytes")
    if b"\0" in data:
        raise ValueError("the PDF password API cannot represent a NUL byte")
    return data


def output_password(password: str | bytes, algorithm: str) -> bytes:
    return _encoded(password, aes256=algorithm in _AES_256)


def input_passwords(password: str | bytes | None) -> tuple[bytes | None, list[bytes]]:
    if password is None:
        return None, []
    primary = _encoded(password, aes256=True)
    if isinstance(password, bytes) or password.isascii():
        return primary, []
    # The revision is unknown until the document opens. Include the legacy
    # encoding and raw UTF-8 for documents saved before Unicode preparation.
    alternatives = dict.fromkeys(
        (_encoded(password, aes256=False), password.encode("utf-8"))
    )
    alternatives.pop(primary, None)
    return primary, list(alternatives)
