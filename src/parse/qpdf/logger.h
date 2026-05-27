//-*-C++-*-

#ifndef PDF_QPDF_LOGGER_H
#define PDF_QPDF_LOGGER_H

#include <loguru.hpp>
#include <qpdf/QPDF.hh>

namespace pdflib
{

  inline void configure_qpdf_warnings(QPDF& qpdf_document)
  {
    if(loguru::g_stderr_verbosity==loguru::Verbosity_INFO or
       loguru::g_stderr_verbosity==loguru::Verbosity_WARNING)
      {
        // ignore ...
      }
    else if(loguru::g_stderr_verbosity==loguru::Verbosity_ERROR or
            loguru::g_stderr_verbosity==loguru::Verbosity_FATAL)
      {
        qpdf_document.setSuppressWarnings(true);
        //qpdf_document.setMaxWarnings(0); only for later versions ...
      }
    else
      {

      }
  }

}

#endif
