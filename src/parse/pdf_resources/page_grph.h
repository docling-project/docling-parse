//-*-C++-*-

#ifndef PDF_PAGE_GRPH_RESOURCE_H
#define PDF_PAGE_GRPH_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_GRPH>
  {
  public:
    
    pdf_resource();
    ~pdf_resource();
    
    nlohmann::json get();

    void set(std::string&     key_, 
	     nlohmann::json&  json_graph, 
	     QPDFObjectHandle qpdf_grph);
    
  private:
    
    std::string    key;
    nlohmann::json val;
  };
  
  pdf_resource<PAGE_GRPH>::pdf_resource()
  {}
  
  pdf_resource<PAGE_GRPH>::~pdf_resource()
  {}
  
  nlohmann::json pdf_resource<PAGE_GRPH>::get()
  {
    return val;
  }
  
  void pdf_resource<PAGE_GRPH>::set(std::string& key_, 
				    nlohmann::json& json_graph, 
				    QPDFObjectHandle qpdf_grph)
  {
    key = key_;
    val = json_graph;

    LOG_S(INFO) << key << ": " << val.dump(2);
  }

}

#endif
