//-*-C++-*-

#ifndef PDF_LINES_SANITATOR_H
#define PDF_LINES_SANITATOR_H

namespace pdflib
{

  template<>
  class pdf_sanitator<PAGE_LINES>
  {
  public:

    pdf_sanitator();
    ~pdf_sanitator();

    void sanitize(pdf_resource<PAGE_DIMENSION>& page_dims,
                  pdf_resource<PAGE_LINES>& old_page_lines,    
                  pdf_resource<PAGE_LINES>& new_page_lines);

  private:

    void filter_by_cropbox();

    void contract_cells_into_lines();  
  };
  
}

#endif
