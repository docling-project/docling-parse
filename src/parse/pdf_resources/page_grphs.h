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
    ~pdf_resource();

    nlohmann::json get();

    size_t size();

    int count(std::string key);
    
    std::set<std::string> keys();

    pdf_resource<PAGE_GRPH>& operator[](std::string fort_name);

    void set(nlohmann::json&   json_grphs,
             QPDFObjectHandle& qpdf_grphs_);

  private:

    std::map<std::string, pdf_resource<PAGE_GRPH> > page_grphs;
  };

  pdf_resource<PAGE_GRPHS>::pdf_resource()
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
    return page_grphs.count(key);
  }

  std::set<std::string> pdf_resource<PAGE_GRPHS>::keys()
  {
    std::set<std::string> keys_;

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
    else
      {
        std::stringstream ss;
        for(auto itr=page_grphs.begin(); itr!=page_grphs.end(); itr++)
	  {
	    ss << itr->first << ", ";
	  }

	{
	  std::stringstream ss;
	  ss << "graphics state with name '" << grph_name << "' is not known: " << ss.str();
	  
	  LOG_S(ERROR) << ss.str();
	  throw std::logic_error(ss.str());
	}
      }

    return (page_grphs.begin()->second);
  }
  
  void pdf_resource<PAGE_GRPHS>::set(nlohmann::json&   json_grphs,
                                     QPDFObjectHandle& qpdf_grphs)
  {
    LOG_S(INFO) << __FUNCTION__;

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
	
        pdf_resource<PAGE_GRPH> page_grph;
        page_grph.set(key, val, qpdf_grphs.getKey(key));

        if(page_grphs.count(key)==1)
          {
	    std::string ss= "we are overwriting a grph!";
	    LOG_S(WARNING) << ss;
          }

        page_grphs[key] = page_grph;
      }
  }

}

#endif
