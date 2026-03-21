//-*-C++-*-

// Locale-independent numeric parsing utilities.
//
// Problem: std::stod() and std::atof() honour LC_NUMERIC, so under
// locales that use ',' as the decimal separator (e.g. fr_FR, de_DE,
// pt_BR) every floating-point value read from a PDF is silently
// corrupted — "72.5" parses as 72.0 because the '.' is not recognised
// as a decimal point.
//
// Solution: std::from_chars (C++17/20) is specified to be
// locale-independent.  We provide two helpers:
//
//   1. locale_safe_stod(str)         — drop-in replacement for std::stod
//   2. locale_safe_numeric_value(obj) — safe wrapper around QPDF's
//      QPDFObjectHandle::getNumericValue(), which internally calls
//      the locale-sensitive atof().
//
// See: https://github.com/docling-project/docling/issues/1455

#ifndef PDF_UTILS_NUMERIC_H
#define PDF_UTILS_NUMERIC_H

#include <charconv>
#include <string>
#include <system_error>
#include <stdexcept>

namespace utils
{
  namespace numeric
  {

    // Locale-independent replacement for std::stod().
    //
    // Uses std::from_chars which is guaranteed by the C++ standard
    // (since C++17, double support mandated in C++20 / all major
    // compilers since GCC 11, Clang 16, MSVC 19.24) to ignore the
    // current LC_NUMERIC setting.
    //
    // Throws std::invalid_argument on parse failure, matching the
    // contract of std::stod().
    inline double locale_safe_stod(const std::string& str)
    {
      double value = 0.0;
      const char* first = str.data();
      const char* last  = first + str.size();

      auto [ptr, ec] = std::from_chars(first, last, value);

      if (ec == std::errc::invalid_argument)
        {
          throw std::invalid_argument(
            "locale_safe_stod: no valid conversion for \"" + str + "\"");
        }
      if (ec == std::errc::result_out_of_range)
        {
          throw std::out_of_range(
            "locale_safe_stod: out of range for \"" + str + "\"");
        }

      return value;
    }

    // Locale-independent wrapper around QPDFObjectHandle::getNumericValue().
    //
    // QPDF's getNumericValue() calls atof() internally for real
    // numbers, which is locale-sensitive.  For integers, getIntValue()
    // is safe (no decimal point involved).  For reals, we re-parse
    // the string representation using from_chars.
    //
    // This function is a drop-in replacement for obj.getNumericValue()
    // anywhere a QPDFObjectHandle is known to be a number.
    inline double locale_safe_numeric_value(QPDFObjectHandle& obj)
    {
      if (obj.isInteger())
        {
          return static_cast<double>(obj.getIntValue());
        }

      // obj.isReal() — re-parse from the string representation
      // instead of relying on QPDF's atof()-based getNumericValue().
      std::string repr = obj.getRealValue();
      return locale_safe_stod(repr);
    }

  }
}

#endif
