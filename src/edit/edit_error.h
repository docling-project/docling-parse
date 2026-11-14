//-*-C++-*-

#ifndef PDF_EDIT_ERROR_H
#define PDF_EDIT_ERROR_H

#include <stdexcept>
#include <string>

#include <parse/qpdf/load_error.h>

namespace pdflib
{
  // Why an edit, an inspection or a write failed. These extend the load
  // categories of parse/qpdf/load_error.h (password, damaged, unsupported, io,
  // unknown); the strings cross into Python as docling_parse.pdf_errors, so
  // they change together.
  namespace edit_error
  {
    constexpr const char* INVALID_EDIT = "invalid_edit"; // bad argument or wrong state
    constexpr const char* STALE_HANDLE = "stale_handle"; // page handle from before a page-list change
    constexpr const char* WRITE        = "write";        // the output could not be produced
  }

  // The only exception type that leaves docling_editor: a category plus the
  // message of the underlying failure. No QPDF exception reaches the binding.
  class pdf_edit_error: public std::runtime_error
  {
  public:

    pdf_edit_error(const std::string& category_, const std::string& message_);

    const std::string& get_category() const;

  private:

    std::string category;
  };

  inline pdf_edit_error::pdf_edit_error(const std::string& category_,
                                        const std::string& message_):
    std::runtime_error(message_),
    category(category_)
  {}

  inline const std::string& pdf_edit_error::get_category() const
  {
    return category;
  }

}

#endif
