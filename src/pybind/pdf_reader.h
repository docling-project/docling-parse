//-*-C++-*-

#ifndef PYBIND_PDF_READER_H
#define PYBIND_PDF_READER_H

#include <pybind/docling_editor.h>

namespace docling
{
  // The read-only face of docling_editor. It implements nothing: protected
  // inheritance hides the editor, and the using-declarations publish the
  // operations that cannot change a document. Add logic to docling_editor,
  // never here.
  class pdf_reader: protected docling_editor
  {
  public:

    pdf_reader(std::string loglevel = "fatal");

    using docling_editor::ANY_GENERATION;

    using docling_editor::open_file;
    using docling_editor::open_buffer;
    using docling_editor::open_source;
    using docling_editor::take_open_error;
    using docling_editor::is_open;
    using docling_editor::close;

    using docling_editor::page_count;
    using docling_editor::generation;
    using docling_editor::document_info;
    using docling_editor::page_info;
    using docling_editor::extract_text;
    using docling_editor::page_images;
    using docling_editor::outline;
    using docling_editor::get_source;
  };

  inline pdf_reader::pdf_reader(std::string loglevel):
    docling_editor(loglevel)
  {}

}

#endif
