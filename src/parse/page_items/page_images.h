//-*-C++-*-

#ifndef PAGE_ITEM_IMAGES_H
#define PAGE_ITEM_IMAGES_H

namespace pdflib
{

  template<>
  class page_item<PAGE_IMAGES>
  {
    typedef typename std::vector<page_item<PAGE_IMAGE> >::iterator itr_type;
    
  public:

    page_item();
    ~page_item();

    nlohmann::json get();

    void rotate(int angle, std::pair<double, double> delta);
    
    page_item<PAGE_IMAGE>& operator[](size_t i);

    void clear();
    size_t size();

    void push_back(page_item<PAGE_IMAGE>& image);    

    itr_type begin() { return images.begin(); }
    itr_type end() { return images.end(); }

    itr_type erase(itr_type itr) { return images.erase(itr); }
    
  private:

    std::vector<page_item<PAGE_IMAGE> > images;
  };

  page_item<PAGE_IMAGES>::page_item()
  {}

  page_item<PAGE_IMAGES>::~page_item()
  {}

  nlohmann::json page_item<PAGE_IMAGES>::get()
  {
    nlohmann::json result;

    result["header"] = page_item<PAGE_IMAGE>::header;

    auto& data = result["data"];
    data = nlohmann::json::array();

    for(auto& item:images)
      {
        data.push_back(item.get());
      }

    return result;
  }

  void page_item<PAGE_IMAGES>::rotate(int angle, std::pair<double, double> delta)
  {
    LOG_S(INFO) << __FUNCTION__;

    for(auto& image:images)
      {
	image.rotate(angle, delta);
      }
  }
  
  page_item<PAGE_IMAGE>& page_item<PAGE_IMAGES>::operator[](size_t i)
  {
    return images.at(i);
  }

  void page_item<PAGE_IMAGES>::clear()
  {
    images.clear();
  }

  size_t page_item<PAGE_IMAGES>::size()
  {
    return images.size();
  }

  void page_item<PAGE_IMAGES>::push_back(page_item<PAGE_IMAGE>& image)
  {
    images.push_back(image);
  }

}

#endif
