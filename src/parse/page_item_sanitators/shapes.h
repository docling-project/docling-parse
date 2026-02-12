//-*-C++-*-

#ifndef PAGE_ITEM_SHAPES_SANITATOR_H
#define PAGE_ITEM_SHAPES_SANITATOR_H

namespace pdflib
{

  template<>
  class page_item_sanitator<PAGE_SHAPES>
  {
  public:

    page_item_sanitator();
    ~page_item_sanitator();

    void sanitize(page_item<PAGE_DIMENSION>& page_dims,
                  page_item<PAGE_SHAPES>& old_page_shapes,    
                  page_item<PAGE_SHAPES>& new_page_shapes);

  private:

    void filter_by_cropbox();

    void contract_cells_into_shapes();  
  };
  
}

#endif
