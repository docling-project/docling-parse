//-*-C++-*-

#ifndef PDF_RESOURCE_H
#define PDF_RESOURCE_H

namespace pdflib
{
  enum resource_name {
    
    PAGE_FONT,
    PAGE_FONTS,
    
    PAGE_GRPH,
    PAGE_GRPHS,

    PAGE_XOBJECT_IMAGE,
    PAGE_XOBJECT_FORM,
    PAGE_XOBJECT_POSTSCRIPT,

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
