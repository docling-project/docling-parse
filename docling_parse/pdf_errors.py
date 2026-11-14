"""Exceptions raised by docling-parse when a PDF cannot be loaded or edited.

This module imports nothing but the standard library, so the reader, writer
and editor can raise the same load errors as `docling_parse.pdf_parser`
without importing docling-core or pydantic. `docling_parse.pdf_parser`
re-exports the load errors under their former names.
"""

from __future__ import annotations

from enum import Enum


class PdfLoadErrorCategory(str, Enum):
    """Why docling-parse could not load a document.

    The values are the categories the native loader reports, see
    `src/parse/qpdf/load_error.h`.
    """

    PASSWORD = "password"
    """The document is encrypted and the password is missing or wrong."""
    DAMAGED = "damaged"
    """qpdf could not repair the file."""
    UNSUPPORTED = "unsupported"
    """The file uses a feature qpdf does not implement."""
    IO = "io"
    """The file is missing or unreadable."""
    UNKNOWN = "unknown"


class PdfLoadError(RuntimeError):
    """docling-parse could not load a document.

    A RuntimeError, so handlers written for the former generic error keep
    working, and the message keeps its former prefix. `category` says what
    went wrong; `detail` is the message of the underlying (qpdf) failure.
    """

    def __init__(
        self,
        key: str,
        category: PdfLoadErrorCategory,
        detail: str,
        *,
        reason: str | None = None,
    ):
        self.key = key
        self.category = category
        self.detail = detail

        message = f"Failed to load document with key {key}"
        description = reason or detail
        if description:
            message = f"{message}: {description}"
        super().__init__(message)


class PdfPasswordError(PdfLoadError):
    """The document is encrypted and was opened without a valid password.

    `password_supplied` tells whether the caller gave a (wrong) password or
    none at all: qpdf reports both as the same invalid-password error.
    """

    def __init__(self, key: str, detail: str, *, password_supplied: bool):
        self.password_supplied = password_supplied
        if password_supplied:
            reason = "the supplied password is incorrect"
        else:
            reason = "the document is encrypted and requires a password"
        super().__init__(key, PdfLoadErrorCategory.PASSWORD, detail, reason=reason)


class PdfEditErrorCategory(str, Enum):
    """Why an inspection, an edit or a write failed.

    The values are the categories the native editor reports: the load
    categories of `src/parse/qpdf/load_error.h` plus those of
    `src/edit/edit_error.h`.
    """

    PASSWORD = "password"
    DAMAGED = "damaged"
    UNSUPPORTED = "unsupported"
    IO = "io"
    UNKNOWN = "unknown"
    INVALID_EDIT = "invalid_edit"
    """A bad argument, or an operation the document's state does not allow."""
    STALE_HANDLE = "stale_handle"
    """A page handle obtained before the page list changed."""
    WRITE = "write"
    """The output could not be produced."""


class PdfEditError(RuntimeError):
    """An operation on a PdfReader, PdfWriter or PdfEditor failed.

    `category` says what went wrong; `detail` is the message of the
    underlying (qpdf) failure.
    """

    def __init__(self, category: PdfEditErrorCategory, detail: str):
        self.category = category
        self.detail = detail
        super().__init__(detail)


class PdfStaleHandleError(PdfEditError):
    """A page handle was used after pages were inserted, removed or moved."""


class PdfWriteError(PdfEditError):
    """The document could not be serialized or the output could not be written."""


class PdfUnsupportedError(PdfEditError):
    """The document uses a feature that is not supported."""
