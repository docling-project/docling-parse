//-*-C++-*-

#ifndef PDF_DECODER_H
#define PDF_DECODER_H

namespace pdflib
{
  enum decode_type { 
    DOCUMENT,
    PAGE, 
    
    //FONT,
    STREAM,

    CMAP
  };

  template<decode_type name>
  class pdf_decoder
  {
  public:

    pdf_decoder();
    ~pdf_decoder();

  private:
    
  };

}

#endif
