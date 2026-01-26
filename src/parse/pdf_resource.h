//-*-C++-*-

#ifndef PDF_RESOURCE_H
#define PDF_RESOURCE_H

namespace pdflib
{
  enum resource_name {
    PAGE_DIMENSION,

    PAGE_FONT,
    PAGE_FONTS,

    PAGE_GRPH,
    PAGE_GRPHS,

    PAGE_CELL,
    PAGE_CELLS,

    PAGE_LINE,
    PAGE_LINES,

    PAGE_IMAGE,
    PAGE_IMAGES,

    PAGE_XOBJECT,
    PAGE_XOBJECTS
  };

  template<resource_name name>
  class pdf_resource
  {
  public:

    pdf_resource();
    ~pdf_resource();

  private:

  };

}

#endif
