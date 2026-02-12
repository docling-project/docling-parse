//-*-C++-*-

#ifndef PAGE_ITEM_HYPERLINK_H
#define PAGE_ITEM_HYPERLINK_H

namespace pdflib
{

  template<>
  class page_item<PAGE_HYPERLINK>
  {
  public:

    page_item();
    ~page_item();

    nlohmann::json get();

    void rotate(int angle, std::pair<double, double> delta);

  public:

    // Bounding box (in page coordinates)
    double x0;
    double y0;
    double x1;
    double y1;

    // Hyperlink-specific fields
    std::string uri;
  };

  page_item<PAGE_HYPERLINK>::page_item():
    x0(0), y0(0), x1(0), y1(0),
    uri()
  {}

  page_item<PAGE_HYPERLINK>::~page_item()
  {}

  nlohmann::json page_item<PAGE_HYPERLINK>::get()
  {
    nlohmann::json result;
    {
      result["x0"] = utils::values::round(x0);
      result["y0"] = utils::values::round(y0);
      result["x1"] = utils::values::round(x1);
      result["y1"] = utils::values::round(y1);

      result["uri"] = uri;
    }
    return result;
  }

  void page_item<PAGE_HYPERLINK>::rotate(int angle, std::pair<double, double> delta)
  {
    utils::values::rotate_inplace(angle, x0, y0);
    utils::values::rotate_inplace(angle, x1, y1);

    utils::values::translate_inplace(delta, x0, y0);
    utils::values::translate_inplace(delta, x1, y1);
  }

}

#endif
