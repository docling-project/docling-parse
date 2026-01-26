//-*-C++-*-

#ifndef PDF_PAGE_FONT_CHAR_DESCRIPTION_H
#define PDF_PAGE_FONT_CHAR_DESCRIPTION_H

namespace pdflib
{

  class char_description
  {

  public:

    char_description();
    ~char_description();

  public:

    std::vector<qpdf_instruction> instructions;
  };

  char_description::char_description()
  {}

  char_description::~char_description()
  {}

}

#endif
