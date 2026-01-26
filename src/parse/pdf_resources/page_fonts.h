//-*-C++-*-

#ifndef PDF_PAGE_FONTS_RESOURCE_H
#define PDF_PAGE_FONTS_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_FONTS>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();

    size_t size();

    int count(std::string key);
    
    std::set<std::string> keys();

    pdf_resource<PAGE_FONT>& operator[](std::string fort_name);

    void set(nlohmann::json&   json_fonts,
             QPDFObjectHandle& qpdf_fonts_);

  private:

    std::map<std::string, pdf_resource<PAGE_FONT> > page_fonts;
  };

  pdf_resource<PAGE_FONTS>::pdf_resource()
  {}

  pdf_resource<PAGE_FONTS>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_FONTS>::get()
  {
    nlohmann::json result;
    {
      for(auto itr=page_fonts.begin(); itr!=page_fonts.end(); itr++)
        {
          result[itr->first] = (itr->second).get();
        }
    }
    
    return result;
  }

  size_t pdf_resource<PAGE_FONTS>::size()
  {
    return page_fonts.size();
  }

  int pdf_resource<PAGE_FONTS>::count(std::string key)
  {
    return page_fonts.count(key);
  }

  std::set<std::string> pdf_resource<PAGE_FONTS>::keys()
  {
    std::set<std::string> keys_;

    for(auto itr=page_fonts.begin(); itr!=page_fonts.end(); itr++)
      {
        keys_.insert(itr->first);
      }

    return keys_;
  }

  pdf_resource<PAGE_FONT>& pdf_resource<PAGE_FONTS>::operator[](std::string font_name)
  {
    if(page_fonts.count(font_name)==1)
      {
        return page_fonts.at(font_name);
      }
    else
      {
        std::stringstream ss;
	ss << "font_name [" << font_name << "] is not known: ";
        for(auto itr=page_fonts.begin(); itr!=page_fonts.end(); itr++)
	  {
	    if(itr==page_fonts.begin())
	      {
		ss << itr->first;
	      }
	    else
	      {
		ss << ", " << itr->first; 
	      }
	  }

	throw std::logic_error(ss.str());
      }

    return (page_fonts.begin()->second);
  }
  
  void pdf_resource<PAGE_FONTS>::set(nlohmann::json&   json_fonts,
                                     QPDFObjectHandle& qpdf_fonts)
  {
    LOG_S(INFO) << __FUNCTION__;

    for(auto& pair : json_fonts.items()) 
      {
        std::string     key = pair.key();
        nlohmann::json& val = pair.value();

        LOG_S(INFO) << "decoding font: " << key;// << "\n" << val.dump(2);

	if(qpdf_fonts.hasKey(key))
	  {
	    pdf_resource<PAGE_FONT> page_font;
	    page_font.set(key, val, qpdf_fonts.getKey(key));
	    
	    if(page_fonts.count(key)==1)
	      {
		LOG_S(WARNING) << "We are overwriting a font!";
	      }
	    
	    page_fonts[key] = page_font;
	  }
	else
	  {
	    LOG_S(ERROR) << "qpdf does not have key: " << key;
	  }
      }
  }

}

#endif
