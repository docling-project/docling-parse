//-*-C++-*-

#ifndef PAGE_ITEM_HYPERLINKS_H
#define PAGE_ITEM_HYPERLINKS_H

namespace pdflib
{

  template<>
  class page_item<PAGE_HYPERLINKS>
  {
    typedef typename std::vector<page_item<PAGE_HYPERLINK> >::iterator itr_type;

  public:

    page_item();
    ~page_item();

    nlohmann::json get();

    void rotate(int angle, std::pair<double, double> delta);

    page_item<PAGE_HYPERLINK>& operator[](size_t i);

    void clear();
    size_t size();

    void push_back(page_item<PAGE_HYPERLINK>& hyperlink);

    itr_type begin() { return hyperlinks.begin(); }
    itr_type end() { return hyperlinks.end(); }

    itr_type erase(itr_type itr) { return hyperlinks.erase(itr); }

  private:

    std::vector<page_item<PAGE_HYPERLINK> > hyperlinks;
  };

  page_item<PAGE_HYPERLINKS>::page_item()
  {}

  page_item<PAGE_HYPERLINKS>::~page_item()
  {}

  nlohmann::json page_item<PAGE_HYPERLINKS>::get()
  {
    nlohmann::json result = nlohmann::json::array();

    for(auto& hyperlink : hyperlinks)
      {
        result.push_back(hyperlink.get());
      }

    return result;
  }

  void page_item<PAGE_HYPERLINKS>::rotate(int angle, std::pair<double, double> delta)
  {
    LOG_S(INFO) << __FUNCTION__;

    for(auto& hyperlink : hyperlinks)
      {
        hyperlink.rotate(angle, delta);
      }
  }

  page_item<PAGE_HYPERLINK>& page_item<PAGE_HYPERLINKS>::operator[](size_t i)
  {
    return hyperlinks.at(i);
  }

  void page_item<PAGE_HYPERLINKS>::clear()
  {
    hyperlinks.clear();
  }

  size_t page_item<PAGE_HYPERLINKS>::size()
  {
    return hyperlinks.size();
  }

  void page_item<PAGE_HYPERLINKS>::push_back(page_item<PAGE_HYPERLINK>& hyperlink)
  {
    hyperlinks.push_back(hyperlink);
  }

}

#endif
