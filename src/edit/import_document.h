//-*-C++-*-

#ifndef PDF_IMPORT_DOCUMENT_H
#define PDF_IMPORT_DOCUMENT_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#define POINTERHOLDER_TRANSITION 0 // eliminate warnings from QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

namespace pdflib
{
  // The bytes a document can be parsed from, plus the password that opens
  // them. This is all one editor ever takes from another: the importer parses
  // the bytes into a QPDF of its own, so two editors never share a QPDF and
  // never need each other's lock. The buffer is immutable once published.
  class pdf_source
  {
  public:

    pdf_source();
    pdf_source(std::shared_ptr<std::string> buffer_,
               std::optional<std::string> password_);

    bool is_valid() const;

    const std::shared_ptr<std::string>& get_buffer() const;
    const std::optional<std::string>& get_password() const;

  private:

    std::shared_ptr<std::string> buffer;
    std::optional<std::string> password;
  };

  // A source parsed for the purpose of copying pages out of it. It keeps the
  // bytes and the QPDF alive for as long as the destination may still read
  // from them: QPDF copies foreign stream data lazily, at write time.
  class pdf_import_document
  {
  public:

    // Throws (QPDFExc) when the bytes cannot be parsed.
    pdf_import_document(const pdf_source& source, const std::string& description);

    QPDF& get_qpdf();

    std::vector<QPDFPageObjectHelper> get_pages();

    // Built on first use: its constructor walks every form field and page.
    QPDFAcroFormDocumentHelper& get_acroform();

  private:

    pdf_source source;

    QPDF qpdf;
    std::unique_ptr<QPDFAcroFormDocumentHelper> acroform;
  };

  inline pdf_source::pdf_source():
    buffer(nullptr),
    password(std::nullopt)
  {}

  inline pdf_source::pdf_source(std::shared_ptr<std::string> buffer_,
                                std::optional<std::string> password_):
    buffer(buffer_),
    password(password_)
  {}

  inline bool pdf_source::is_valid() const
  {
    return buffer != nullptr;
  }

  inline const std::shared_ptr<std::string>& pdf_source::get_buffer() const
  {
    return buffer;
  }

  inline const std::optional<std::string>& pdf_source::get_password() const
  {
    return password;
  }

  inline pdf_import_document::pdf_import_document(const pdf_source& source_,
                                                  const std::string& description):
    source(source_),
    qpdf(),
    acroform(nullptr)
  {
    qpdf.setSuppressWarnings(true);

    const std::shared_ptr<std::string>& buffer = source.get_buffer();

    if(source.get_password().has_value())
      {
        qpdf.processMemoryFile(description.c_str(),
                               buffer->c_str(),
                               buffer->size(),
                               source.get_password().value().c_str());
      }
    else
      {
        qpdf.processMemoryFile(description.c_str(),
                               buffer->c_str(),
                               buffer->size());
      }
  }

  inline QPDF& pdf_import_document::get_qpdf()
  {
    return qpdf;
  }

  inline std::vector<QPDFPageObjectHelper> pdf_import_document::get_pages()
  {
    return QPDFPageDocumentHelper(qpdf).getAllPages();
  }

  inline QPDFAcroFormDocumentHelper& pdf_import_document::get_acroform()
  {
    if(acroform == nullptr)
      {
        acroform = std::make_unique<QPDFAcroFormDocumentHelper>(qpdf);
      }

    return *acroform;
  }

}

#endif
