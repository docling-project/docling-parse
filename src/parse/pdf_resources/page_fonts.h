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
    pdf_resource(std::shared_ptr<pdf_resource<PAGE_FONTS>> parent);
    ~pdf_resource();

    nlohmann::json get();

    size_t size();

    int count(std::string key);

    std::unordered_set<std::string> keys();

    pdf_resource<PAGE_FONT>& operator[](std::string fort_name);

    void set(QPDFObjectHandle& qpdf_fonts_,
             pdf_timings& timings);

  private:

    std::shared_ptr<pdf_resource<PAGE_FONTS>> parent_;
    std::unordered_map<std::string, pdf_resource<PAGE_FONT> > page_fonts;
  };

  pdf_resource<PAGE_FONTS>::pdf_resource():
    parent_(nullptr)
  {}

  pdf_resource<PAGE_FONTS>::pdf_resource(std::shared_ptr<pdf_resource<PAGE_FONTS>> parent):
    parent_(parent)
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
    if(page_fonts.count(key)==1)
      {
        return 1;
      }
    if(parent_)
      {
        return parent_->count(key);
      }
    return 0;
  }

  std::unordered_set<std::string> pdf_resource<PAGE_FONTS>::keys()
  {
    std::unordered_set<std::string> keys_;

    if(parent_)
      {
        keys_ = parent_->keys();
      }

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

    if(parent_)
      {
        return (*parent_)[font_name];
      }

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
  
  void pdf_resource<PAGE_FONTS>::set(QPDFObjectHandle& qpdf_fonts,
                                     pdf_timings& timings)
  {
    LOG_S(INFO) << __FUNCTION__;

    double total_font_time = 0.0;

    for(auto& key : qpdf_fonts.getKeys())
      {
        LOG_S(INFO) << "decoding font: " << key;

	utils::timer font_timer;

	QPDFObjectHandle qpdf_font = qpdf_fonts.getKey(key);
	nlohmann::json json_font = to_json(qpdf_font);

	pdf_resource<PAGE_FONT> page_font(timings);
	page_font.set(key, json_font, qpdf_font);

	if(page_fonts.count(key)==1)
	  {
	    LOG_S(WARNING) << "We are overwriting a font!";
	    page_fonts.erase(key);
	  }

	page_fonts.emplace(key, std::move(page_font));

	double font_time = font_timer.get_time();
	total_font_time += font_time;
	timings.add_timing(pdf_timings::PREFIX_DECODE_FONT + key, font_time);
      }

    timings.add_timing(pdf_timings::KEY_DECODE_FONTS_TOTAL, total_font_time);
  }

}

#endif
