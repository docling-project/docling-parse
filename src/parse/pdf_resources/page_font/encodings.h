//-*-C++-*-

#ifndef PDF_PAGE_FONT_ENCODINGS_H
#define PDF_PAGE_FONT_ENCODINGS_H

namespace pdflib
{

  class font_encodings
  {

  public:

    font_encodings();
    ~font_encodings();

    font_encoding& get(font_encoding_name name);

    template<typename glyphs_type>
    void initialise(std::string dirname, glyphs_type& glyphs);

  private:

    bool initialized;
    
    std::map<font_encoding_name, font_encoding> name_to_encoding;
  };

  font_encodings::font_encodings():
    initialized(false)
  {}

  font_encodings::~font_encodings()
  {}

  font_encoding& font_encodings::get(font_encoding_name name)
  {
    return name_to_encoding.at(name);
  }    

  template<typename glyphs_type>
  void font_encodings::initialise(std::string dirname, glyphs_type& glyphs)
  {
    if(initialized)
      {
	LOG_S(WARNING) << "skipping font_encodings::initialise, already initialized ...";
	return;
      }
    
    std::vector<std::pair<font_encoding_name, std::string> > items = {
      {STANDARD, "std.dat"},
      {MACROMAN, "macroman.dat"},
      {MACEXPERT, "macexpert.dat"},
      {WINANSI, "winansi.dat"}
    };

    for(auto item:items)
      {
        font_encoding& encoding = name_to_encoding[item.first];
        encoding.initialise(item.first, dirname+"/"+item.second, glyphs);
      }

    initialized = true;
  }

}

#endif
