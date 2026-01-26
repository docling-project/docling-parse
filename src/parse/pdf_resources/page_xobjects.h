//-*-C++-*-

#ifndef PDF_PAGE_XOBJECTS_RESOURCE_H
#define PDF_PAGE_XOBJECTS_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_XOBJECTS>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();

    bool                        has(std::string name);
    pdf_resource<PAGE_XOBJECT>& get(std::string name);

    void set(nlohmann::json&   json_xobjects,
             QPDFObjectHandle& qpdf_xobjects_);

    pdf_resource<PAGE_XOBJECT>& operator[](std::string fort_name);

  private:

    std::map<std::string, pdf_resource<PAGE_XOBJECT> > page_xobjects;
  };

  pdf_resource<PAGE_XOBJECTS>::pdf_resource()
  {}

  pdf_resource<PAGE_XOBJECTS>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_XOBJECTS>::get()
  {
    nlohmann::json result;
    {
      for(auto itr=page_xobjects.begin(); itr!=page_xobjects.end(); itr++)
        {
          result[itr->first] = (itr->second).get();
        }
    }
    
    return result;
  }

  bool pdf_resource<PAGE_XOBJECTS>::has(std::string name)
  {
    return (page_xobjects.count(name)==1);
  }

  pdf_resource<PAGE_XOBJECT>& pdf_resource<PAGE_XOBJECTS>::get(std::string name)
  {
    return page_xobjects.at(name);
  }

  pdf_resource<PAGE_XOBJECT>& pdf_resource<PAGE_XOBJECTS>::operator[](std::string xobject_name)
  {
    //assert(page_xobjects.count(xobject_name)==1);
    if(page_xobjects.count(xobject_name)!=1)
      {
	std::string message = "page_xobjects.count(xobject_name)!=1";
	LOG_S(ERROR) << message;
	throw std::logic_error(message);
      }
    
    return page_xobjects[xobject_name];
  }

  void pdf_resource<PAGE_XOBJECTS>::set(nlohmann::json&   json_xobjects,
                                        QPDFObjectHandle& qpdf_xobjects)
  {
    LOG_S(INFO) << __FUNCTION__;
    //LOG_S(INFO) << json_xobjects.dump(2);

    int cnt = 0;
    int len = json_xobjects.size();
    
    for(auto& pair : json_xobjects.items()) 
      {
        LOG_S(INFO) << "decoding xobject: " << pair.key() << "\t" << (++cnt) << "/" << len;
        
        std::string key = pair.key();
	nlohmann::json& val = pair.value();
	
	if(page_xobjects.count(key)>0)
	  {
	    LOG_S(ERROR) << key << "is already in page_xobjects, overwriting ...";
	  }
	    
	pdf_resource<PAGE_XOBJECT> page_xobject;
	page_xobject.set(key, val, qpdf_xobjects.getKey(key));
	
	page_xobjects[key] = page_xobject;
      }
  }

}

#endif
