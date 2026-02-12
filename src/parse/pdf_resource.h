//-*-C++-*-

#ifndef PDF_RESOURCE_H
#define PDF_RESOURCE_H

namespace pdflib
{
  enum item_name {
    PAGE_DIMENSION,

    PAGE_CELL,
    PAGE_CELLS,

    PAGE_SHAPE,
    PAGE_SHAPES,

    PAGE_IMAGE,
    PAGE_IMAGES,
  };
  
  template<item_name name>
  class page_item
  {
  public:

    page_item();
    ~page_item();

  private:

  };
  
  enum resource_name {

    // former resources and need to be renamed to page_item
    /* 
    PAGE_DIMENSION,

    PAGE_CELL,
    PAGE_CELLS,

    PAGE_SHAPE,
    PAGE_SHAPES,

    PAGE_IMAGE,
    PAGE_IMAGES,
    */
    
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
