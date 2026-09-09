//-*-C++-*-

#ifndef PDF_DOCUMENT_EDITOR_H
#define PDF_DOCUMENT_EDITOR_H

#include <memory>
#include <optional>
#include <string>

#define POINTERHOLDER_TRANSITION 0 // eliminate warnings from QPDF
#include <qpdf/QPDF.hh>

#define LOGURU_WITH_STREAMS 1
#include <loguru.hpp>

namespace pdflib
{

  class pdf_editor_tools
  {
    
  public:

    static int get_number_of_pages(std::string& _filename,
                                    std::optional<std::string>& _password,
                                    bool keep_qpdf_warnings = false);

    static int get_number_of_pages(std::shared_ptr<std::string> _buffer,
				   std::optional<std::string>& _password,
				   std::string description = "processing buffer",
				   bool keep_qpdf_warnings = false);    
  };

  int pdf_editor_tools::get_number_of_pages(std::string& _filename,
                                    std::optional<std::string>& _password,
                                    bool keep_qpdf_warnings)
  {
    QPDF qpdf_document;

    try
      {
        qpdf_document.setSuppressWarnings(not keep_qpdf_warnings);

        // qpdf opens the file itself: the filename arrives UTF-8 encoded from the
        // Python layer and processFile is UTF-8 aware on every platform. It also
        // reads lazily, so only the page tree is touched here.
        if(_password.has_value())
          {
            qpdf_document.processFile(_filename.c_str(), _password.value().c_str());
          }
        else
          {
            qpdf_document.processFile(_filename.c_str());
          }

        return qpdf_document.getAllPages().size();
      }
    catch(const std::exception& exc)
      {
        LOG_S(ERROR) << "could not process file '" << _filename << "' by qpdf: " << exc.what();
        return -1;
      }
  }

  int pdf_editor_tools::get_number_of_pages(std::shared_ptr<std::string> _buffer,
				      std::optional<std::string>& _password,
				      std::string description,
				      bool keep_qpdf_warnings)
  {
    if(_buffer==nullptr)
      {
        LOG_S(ERROR) << "no buffer provided for '" << description << "'";
        return -1;
      }

    QPDF qpdf_document;

    try
      {
        qpdf_document.setSuppressWarnings(not keep_qpdf_warnings);

        if(_password.has_value())
          {
            qpdf_document.processMemoryFile(description.c_str(),
                                            _buffer->c_str(),
                                            _buffer->size(),
                                            _password.value().c_str());
          }
        else
          {
            qpdf_document.processMemoryFile(description.c_str(),
                                            _buffer->c_str(),
                                            _buffer->size());
          }

        return qpdf_document.getAllPages().size();
      }
    catch(const std::exception& exc)
      {
        LOG_S(ERROR) << "could not process buffer for '" << description << "' by qpdf: " << exc.what();
        return -1;
      }
  }

}

#endif
