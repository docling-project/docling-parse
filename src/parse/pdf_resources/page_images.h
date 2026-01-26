//-*-C++-*-

#ifndef PDF_PAGE_IMAGES_RESOURCE_H
#define PDF_PAGE_IMAGES_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_IMAGES>
  {
    typedef typename std::vector<pdf_resource<PAGE_IMAGE> >::iterator itr_type;
    
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();

    void rotate(int angle, std::pair<double, double> delta);
    
    pdf_resource<PAGE_IMAGE>& operator[](size_t i);

    void clear();
    size_t size();

    void push_back(pdf_resource<PAGE_IMAGE>& image);    

    itr_type begin() { return images.begin(); }
    itr_type end() { return images.end(); }

    itr_type erase(itr_type itr) { return images.erase(itr); }
    
  private:

    std::vector<pdf_resource<PAGE_IMAGE> > images;
  };

  pdf_resource<PAGE_IMAGES>::pdf_resource()
  {}

  pdf_resource<PAGE_IMAGES>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_IMAGES>::get()
  {
    nlohmann::json result;

    result["header"] = pdf_resource<PAGE_IMAGE>::header;

    auto& data = result["data"];
    data = nlohmann::json::array();

    for(auto& item:images)
      {
        data.push_back(item.get());
      }

    return result;
  }

  void pdf_resource<PAGE_IMAGES>::rotate(int angle, std::pair<double, double> delta)
  {
    LOG_S(INFO) << __FUNCTION__;

    for(auto& image:images)
      {
	image.rotate(angle, delta);
      }
  }
  
  pdf_resource<PAGE_IMAGE>& pdf_resource<PAGE_IMAGES>::operator[](size_t i)
  {
    return images.at(i);
  }

  void pdf_resource<PAGE_IMAGES>::clear()
  {
    images.clear();
  }

  size_t pdf_resource<PAGE_IMAGES>::size()
  {
    return images.size();
  }

  void pdf_resource<PAGE_IMAGES>::push_back(pdf_resource<PAGE_IMAGE>& image)
  {
    images.push_back(image);
  }

}

#endif
