//-*-C++-*-

#ifndef PDF_PAGE_FONT_ENCODING_H
#define PDF_PAGE_FONT_ENCODING_H

#include <fstream>

namespace pdflib
{

  class font_encoding
  {

  public:

    font_encoding();
    ~font_encoding();

    std::map<uint32_t, std::string>& get_numb_to_utf8();

    template<typename glyphs_type>
    void initialise(font_encoding_name name_, 
                    std::string file_name,
                     glyphs_type& glyphs);
    

  private:

    font_encoding_name name;
    
    std::map<uint32_t, std::string> numb_to_name;
    std::map<uint32_t, std::string> numb_to_utf8;
  };

  font_encoding::font_encoding()
  {}

  font_encoding::~font_encoding()
  {}

  std::map<uint32_t, std::string>& font_encoding::get_numb_to_utf8()
  {
    return numb_to_utf8;
  }

  template<typename glyphs_type>
  void font_encoding::initialise(font_encoding_name name_,
                                 std::string file_name,
                                 glyphs_type& glyphs)
  {
    LOG_S(WARNING) << __FUNCTION__ << ": " << file_name;

    name = name_;

    std::ifstream file(file_name.c_str());
    
    std::string line;
    while (std::getline(file, line))
      {
        if(line.size()==0 or (line.front()=='#'))
          continue;

        size_t ind=line.find(";");
        if(ind!=std::string::npos)
          {
            std::string key = line.substr(0    , ind);
            std::string val = line.substr(ind+1, line.size()-(ind+1));

            uint32_t ind = utils::string::hex_to_numb(key);

            numb_to_name[ind] = val;
            numb_to_utf8[ind] = glyphs[val];

            //LOG_S(INFO) << "key: " << key << " [ind=" << ind << "] => " 
            //            << numb_to_name[ind] << "\t" << numb_to_utf8[ind];
          }
      }
  }

}

#endif
