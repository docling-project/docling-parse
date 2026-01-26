//-*-C++-*-

#ifndef PDF_PAGE_FONT_CHAR_PROCESSOR_H
#define PDF_PAGE_FONT_CHAR_PROCESSOR_H

namespace pdflib
{

  class char_processor
  {

  public:

    char_processor();
    ~char_processor();

    char_description parse(std::vector<qpdf_instruction>& instructions_);

  private:
    
    //std::vector<qpdf_instruction> instructions
  };

  char_processor::char_processor()
  {}

  char_processor::~char_processor()
  {}

  char_description char_processor::parse(std::vector<qpdf_instruction>& instructions_)
  {
    char_description desc;
    desc.instructions = instructions_;

    return desc;

    /*
    std::vector<qpdf_instruction> parameters;

    for(auto& item:instructions)
      {
        if(item.key!="operator")
          {
            parameters.push_back(item);
          }
        else
          {
            if(item.val=="Char_ProcessName")
              }
      }
    */
  }

}

#endif
