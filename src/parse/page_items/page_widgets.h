//-*-C++-*-

#ifndef PAGE_ITEM_WIDGETS_H
#define PAGE_ITEM_WIDGETS_H

namespace pdflib
{

  template<>
  class page_item<PAGE_WIDGETS>
  {
    typedef typename std::vector<page_item<PAGE_WIDGET> >::iterator itr_type;

  public:

    page_item();
    ~page_item();

    nlohmann::json get();

    void rotate(int angle, std::pair<double, double> delta);

    page_item<PAGE_WIDGET>& operator[](size_t i);

    void clear();
    size_t size();

    void push_back(page_item<PAGE_WIDGET>& widget);

    itr_type begin() { return widgets.begin(); }
    itr_type end() { return widgets.end(); }

    itr_type erase(itr_type itr) { return widgets.erase(itr); }

  private:

    std::vector<page_item<PAGE_WIDGET> > widgets;
  };

  page_item<PAGE_WIDGETS>::page_item()
  {}

  page_item<PAGE_WIDGETS>::~page_item()
  {}

  nlohmann::json page_item<PAGE_WIDGETS>::get()
  {
    nlohmann::json result = nlohmann::json::array();

    for(auto& widget : widgets)
      {
        result.push_back(widget.get());
      }

    return result;
  }

  void page_item<PAGE_WIDGETS>::rotate(int angle, std::pair<double, double> delta)
  {
    LOG_S(INFO) << __FUNCTION__;

    for(auto& widget : widgets)
      {
        widget.rotate(angle, delta);
      }
  }

  page_item<PAGE_WIDGET>& page_item<PAGE_WIDGETS>::operator[](size_t i)
  {
    return widgets.at(i);
  }

  void page_item<PAGE_WIDGETS>::clear()
  {
    widgets.clear();
  }

  size_t page_item<PAGE_WIDGETS>::size()
  {
    return widgets.size();
  }

  void page_item<PAGE_WIDGETS>::push_back(page_item<PAGE_WIDGET>& widget)
  {
    widgets.push_back(widget);
  }

}

#endif
