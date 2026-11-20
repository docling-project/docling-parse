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
  // The immutable bytes and password. A private one-use reader may hand off
  // its parsed QPDF as well, releasing its ownership before the import.
  class pdf_source
  {
  public:

    pdf_source();
    pdf_source(std::shared_ptr<std::string> buffer_,
               std::optional<std::string> password_,
               std::shared_ptr<QPDF> prepared_ = nullptr);

    bool is_valid() const;

    const std::shared_ptr<std::string>& get_buffer() const;
    const std::optional<std::string>& get_password() const;
    const std::shared_ptr<QPDF>& get_prepared() const { return prepared; }

  private:

    std::shared_ptr<std::string> buffer;
    std::optional<std::string> password;
    std::shared_ptr<QPDF> prepared;
  };

  // A source parsed for copying pages. QPDF copies stream data immediately,
  // so the importer can be destroyed once its copies are complete.
  class pdf_import_document
  {
  public:

    // Throws (QPDFExc) when the bytes cannot be parsed.
    pdf_import_document(const pdf_source& source, const std::string& description);

    QPDF& get_qpdf();

    // Built on first use: its constructor walks every form field and page.
    QPDFAcroFormDocumentHelper& get_acroform();

  private:

    pdf_source source;

    std::shared_ptr<QPDF> qpdf;
    std::unique_ptr<QPDFAcroFormDocumentHelper> acroform;
  };

  inline pdf_source::pdf_source():
    buffer(nullptr),
    password(std::nullopt)
  {}

  inline pdf_source::pdf_source(std::shared_ptr<std::string> buffer_,
                                std::optional<std::string> password_,
                                std::shared_ptr<QPDF> prepared_):
    buffer(buffer_),
    password(password_),
    prepared(std::move(prepared_))
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
    qpdf(source.get_prepared() ? source.get_prepared() : std::make_shared<QPDF>()),
    acroform(nullptr)
  {
    qpdf->setImmediateCopyFrom(true);
    if(source.get_prepared()) { return; }
    qpdf->setSuppressWarnings(true);

    const std::shared_ptr<std::string>& buffer = source.get_buffer();

    if(source.get_password().has_value())
      {
        qpdf->processMemoryFile(description.c_str(),
                               buffer->c_str(),
                               buffer->size(),
                               source.get_password().value().c_str());
      }
    else
      {
        qpdf->processMemoryFile(description.c_str(),
                               buffer->c_str(),
                               buffer->size());
      }
  }

  inline QPDF& pdf_import_document::get_qpdf()
  {
    return *qpdf;
  }

  inline QPDFAcroFormDocumentHelper& pdf_import_document::get_acroform()
  {
    if(acroform == nullptr)
      {
        acroform = std::make_unique<QPDFAcroFormDocumentHelper>(*qpdf);
      }

    return *acroform;
  }

}

#endif
