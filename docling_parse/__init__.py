"""docling-parse: Extract text with coordinates from programmatic PDFs."""

import locale as _locale
import logging as _logging

_log = _logging.getLogger(__name__)


def _ensure_safe_numeric_locale() -> None:
    """Ensure LC_NUMERIC uses a period as decimal separator.

    PDF coordinate parsing (both in QPDF's C library and in this
    package's C++ layer) relies on '.' as the decimal separator.
    Locales that use ',' (French, German, Portuguese, etc.) silently
    corrupt every floating-point value extracted from a PDF.

    This function is called at import time as a defence-in-depth
    measure.  The primary fix is in the C++ layer (from_chars), but
    this protects against any locale-sensitive path we may have missed
    — including QPDF's own atof() calls that we cannot patch.

    See: https://github.com/docling-project/docling/issues/1455
    """
    try:
        current = _locale.getlocale(_locale.LC_NUMERIC)
        # setlocale returns the *actual* locale string; checking the
        # decimal point via localeconv is the most reliable test.
        conv = _locale.localeconv()
        if conv.get("decimal_point", ".") != ".":
            _locale.setlocale(_locale.LC_NUMERIC, "C")
            _log.info(
                "docling-parse: overrode LC_NUMERIC from %s to 'C' "
                "to prevent PDF coordinate corruption",
                current,
            )
    except (_locale.Error, ValueError):
        # If we can't query or set the locale, the C++ from_chars
        # layer will still protect us.
        pass


_ensure_safe_numeric_locale()

# PdfReader, PdfWriter and PdfEditor are importable from the package, but only
# loaded on first use: `import docling_parse` stays as cheap as before.
_LAZY_EXPORTS = {
    "PdfReader": "docling_parse.pdf_reader",
    "PdfWriter": "docling_parse.pdf_writer",
    "PdfEditor": "docling_parse.pdf_editor",
}


def __getattr__(name: str):
    module_name = _LAZY_EXPORTS.get(name)
    if module_name is None:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")

    import importlib

    value = getattr(importlib.import_module(module_name), name)
    globals()[name] = value
    return value
