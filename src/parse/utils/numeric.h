//-*-C++-*-

// Locale-independent numeric parsing utilities.
//
// Problem: std::stod() and std::atof() honour LC_NUMERIC, so under
// locales that use ',' as the decimal separator (e.g. fr_FR, de_DE,
// pt_BR) every floating-point value read from a PDF is silently
// corrupted — "72.5" parses as 72.0 because the '.' is not recognised
// as a decimal point.
//
// Solution: parse using the classic C locale instead of the process locale.
// We provide two helpers:
//
//   1. locale_safe_stod(str)         — drop-in replacement for std::stod
//   2. locale_safe_numeric_value(obj) — safe wrapper around QPDF's
//      QPDFObjectHandle::getNumericValue(), which internally calls
//      the locale-sensitive atof().
//
// See: https://github.com/docling-project/docling/issues/1455

#ifndef PDF_UTILS_NUMERIC_H
#define PDF_UTILS_NUMERIC_H

#include <locale>
#include <sstream>
#include <string>
#include <stdexcept>

namespace utils
{
  namespace numeric
  {

    // Locale-independent replacement for std::stod().
    //
    // Uses a stream imbued with std::locale::classic(), which ignores the
    // current LC_NUMERIC setting and remains portable across the older
    // standard libraries used by the wheel build matrix.
    //
    // Throws std::invalid_argument on parse failure, matching the
    // contract of std::stod().
    inline double locale_safe_stod(const std::string& str)
    {
      std::istringstream stream(str);
      stream.imbue(std::locale::classic());

      double value = 0.0;
      stream >> value;

      if (stream.fail())
        {
          throw std::invalid_argument(
            "locale_safe_stod: no valid conversion for \"" + str + "\"");
        }

      return value;
    }

    // Locale-independent wrapper around QPDFObjectHandle::getNumericValue().
    //
    // QPDF's getNumericValue() calls atof() internally for real
    // numbers, which is locale-sensitive.  For integers, getIntValue()
    // is safe (no decimal point involved).  For reals, we re-parse
    // the string representation using locale_safe_stod().
    //
    // This function is a drop-in replacement for obj.getNumericValue()
    // anywhere a QPDFObjectHandle is known to be a number.
    inline double locale_safe_numeric_value(QPDFObjectHandle& obj)
    {
      if (obj.isInteger())
        {
          return static_cast<double>(obj.getIntValue());
        }
      else if (obj.isReal())
        {
          // Re-parse from the string representation instead of
          // relying on QPDF's atof()-based getNumericValue().
          return locale_safe_stod(obj.getRealValue());
        }
      else
        {
          throw std::invalid_argument(
            "locale_safe_numeric_value: QPDF object is neither integer nor real");
        }
    }

  }
}

#endif
