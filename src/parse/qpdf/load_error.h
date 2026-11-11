//-*-C++-*-

#ifndef QPDF_LOAD_ERROR_H
#define QPDF_LOAD_ERROR_H

#include <string>

#include <qpdf/Constants.h>

namespace pdflib
{
  // Why a document could not be loaded. The strings cross into Python as
  // docling_parse.pdf_parser.PdfLoadErrorCategory, so they change together.
  namespace load_error
  {
    constexpr const char* PASSWORD    = "password";    // encrypted; password missing or wrong
    constexpr const char* DAMAGED     = "damaged";     // qpdf could not repair the file
    constexpr const char* UNSUPPORTED = "unsupported"; // a feature qpdf does not implement
    constexpr const char* IO          = "io";          // file missing or unreadable
    constexpr const char* UNKNOWN     = "unknown";

    inline std::string category_of(qpdf_error_code_e code)
    {
      switch(code)
        {
        case qpdf_e_password:    return PASSWORD;
        case qpdf_e_damaged_pdf: return DAMAGED;
        case qpdf_e_unsupported: return UNSUPPORTED;
        case qpdf_e_system:      return IO;
        default:                 return UNKNOWN;
        }
    }
  }
}

#endif
