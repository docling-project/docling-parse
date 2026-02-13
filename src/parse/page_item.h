//-*-C++-*-

#ifndef PAGE_ITEM_H
#define PAGE_ITEM_H

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

    PAGE_WIDGET,
    PAGE_WIDGETS,

    PAGE_HYPERLINK,
    PAGE_HYPERLINKS   
  };
  
  template<item_name name>
  class page_item
  {
  public:

    page_item();
    ~page_item();

  private:

  };

}

#endif
