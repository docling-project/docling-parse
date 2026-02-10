//-*-C++-*-

#ifndef PDF_SHAPES_SANITATOR_H
#define PDF_SHAPES_SANITATOR_H

namespace pdflib
{

  template<>
  class pdf_sanitator<PAGE_SHAPES>
  {
  public:

    pdf_sanitator();
    ~pdf_sanitator();

    void sanitize(pdf_resource<PAGE_DIMENSION>& page_dims,
                  pdf_resource<PAGE_SHAPES>& old_page_shapes,    
                  pdf_resource<PAGE_SHAPES>& new_page_shapes);

  private:

    void filter_by_cropbox();

    void contract_cells_into_shapes();  
  };
  
}

#endif
