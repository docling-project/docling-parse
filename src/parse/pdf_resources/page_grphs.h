//-*-C++-*-

#ifndef PDF_PAGE_GRPHS_RESOURCE_H
#define PDF_PAGE_GRPHS_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_GRPHS>
  {
  public:

    pdf_resource();
    pdf_resource(std::shared_ptr<pdf_resource<PAGE_GRPHS>> parent);
    ~pdf_resource();

    nlohmann::json get();

    size_t size();

    int count(std::string key);

    std::unordered_set<std::string> keys();

    pdf_resource<PAGE_GRPH>& operator[](std::string fort_name);

    void set(nlohmann::json&   json_grphs,
             QPDFObjectHandle& qpdf_grphs_,
             pdf_timings& timings);

  private:

    std::shared_ptr<pdf_resource<PAGE_GRPHS>> parent_;
    std::unordered_map<std::string, pdf_resource<PAGE_GRPH> > page_grphs;
  };

  pdf_resource<PAGE_GRPHS>::pdf_resource():
    parent_(nullptr)
  {}

  pdf_resource<PAGE_GRPHS>::pdf_resource(std::shared_ptr<pdf_resource<PAGE_GRPHS>> parent):
    parent_(parent)
  {}

  pdf_resource<PAGE_GRPHS>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_GRPHS>::get()
  {
    nlohmann::json result;
    {
      for(auto itr=page_grphs.begin(); itr!=page_grphs.end(); itr++)
        {
          result[itr->first] = (itr->second).get();
        }
    }
    
    return result;
  }

  size_t pdf_resource<PAGE_GRPHS>::size()
  {
    return page_grphs.size();
  }

  int pdf_resource<PAGE_GRPHS>::count(std::string key)
  {
    if(page_grphs.count(key)==1)
      {
        return 1;
      }
    if(parent_)
      {
        return parent_->count(key);
      }
    return 0;
  }

  std::unordered_set<std::string> pdf_resource<PAGE_GRPHS>::keys()
  {
    std::unordered_set<std::string> keys_;

    if(parent_)
      {
        keys_ = parent_->keys();
      }

    for(auto itr=page_grphs.begin(); itr!=page_grphs.end(); itr++)
      {
        keys_.insert(itr->first);
      }

    return keys_;
  }

  pdf_resource<PAGE_GRPH>& pdf_resource<PAGE_GRPHS>::operator[](std::string grph_name)
  {
    if(page_grphs.count(grph_name)==1)
      {
        return page_grphs[grph_name];
      }

    if(parent_)
      {
        return (*parent_)[grph_name];
      }

    {
      std::stringstream ss;
      ss << "graphics state with name '" << grph_name << "' is not known: ";
      for(auto itr=page_grphs.begin(); itr!=page_grphs.end(); itr++)
        {
          ss << itr->first << ", ";
        }

      LOG_S(ERROR) << ss.str();
      throw std::logic_error(ss.str());
    }

    return (page_grphs.begin()->second);
  }
  
  void pdf_resource<PAGE_GRPHS>::set(nlohmann::json&   json_grphs,
                                     QPDFObjectHandle& qpdf_grphs,
                                     pdf_timings& timings)
  {
    LOG_S(INFO) << __FUNCTION__;

    double total_grph_time = 0.0;

    for(auto& pair : json_grphs.items())
      {
        std::string     key = pair.key();
        nlohmann::json& val = pair.value();

        LOG_S(INFO) << "decoding graphics state: " << key;
        //assert(qpdf_grphs.hasKey(key));

	if(not qpdf_grphs.hasKey(key))
	  {
	    std::string message = "not qpdf_grphs.hasKey(key)";
	    LOG_S(ERROR) << message;

	    //throw std::logic_error(message);
	    continue;
	  }

	utils::timer grph_timer;

        pdf_resource<PAGE_GRPH> page_grph;
        page_grph.set(key, val, qpdf_grphs.getKey(key));

        if(page_grphs.count(key)==1)
          {
	    std::string ss= "we are overwriting a grph!";
	    LOG_S(WARNING) << ss;
          }

        page_grphs[key] = page_grph;

	double grph_time = grph_timer.get_time();
	total_grph_time += grph_time;
	timings.add_timing(pdf_timings::PREFIX_DECODE_GRPH + key, grph_time);
      }

    timings.add_timing(pdf_timings::KEY_DECODE_GRPHS_TOTAL, total_grph_time);
  }

}

#endif
