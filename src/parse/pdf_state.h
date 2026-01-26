//-*-C++-*-

#ifndef PDF_STATE_H
#define PDF_STATE_H

namespace pdflib
{

  enum state_type { 
    GLOBAL,
    TEXT,
    LINE,
    GRPH
  };

  template<state_type name>
  class pdf_state
  {
  public:

    pdf_state();
    ~pdf_state();

  private:
    
  };

}

#endif
