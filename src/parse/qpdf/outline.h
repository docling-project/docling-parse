//-*-C++-*-

#ifndef QPDF_OUTLINE_H
#define QPDF_OUTLINE_H

#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjGen.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>
#include <qpdf/QPDFOutlineObjectHelper.hh>

namespace pdflib
{
  // The document outline (12.3.3), also known as the bookmarks or the
  // table-of-contents, with the destination (12.3.2) of every item resolved to a
  // page and, where the destination specifies one, to a position on that page.
  //
  // A destination's coordinates are reported in the frame that page's cells are
  // reported in: the page normalised by its /Rotate angle, bottom-left origin.
  // The transform is the one pdf_decoder<PAGE>::rotate_contents() applies, run
  // through the very same page_item<PAGE_DIMENSION>, so the two cannot drift.
  //
  // Only dictionary keys are read here. No content stream is decoded and no font
  // is loaded, which keeps the outline as cheap as the rest of the annotations.
  class pdf_outline
  {
  public:

    pdf_outline(QPDF& qpdf_document,
                std::vector<QPDFObjectHandle>& qpdf_pages);
    ~pdf_outline();

    nlohmann::json get();

  private:

    // Everything needed to place a destination on one page, measured once per
    // page that is actually targeted by the outline.
    struct page_frame
    {
      int angle;                        // /Rotate, which the transform normalises away
      std::pair<double, double> delta;  // translation that follows the rotation
      std::array<double, 4> crop_bbox;  // page rectangle, after the transform
      double default_x;                 // left edge of the page, before the transform
      double default_y;                 // top edge of the page, before the transform
    };

    nlohmann::json to_json(QPDFOutlineObjectHelper& item, int level);

    nlohmann::json get_destination(QPDFOutlineObjectHelper& item);

    int get_destination_page(QPDFObjectHandle page);
    bool get_destination_arg(QPDFObjectHandle& dest, int arg_index, double& value);

    const page_frame& get_page_frame(int page_number);

    std::string get_title(QPDFOutlineObjectHelper& item);

  private:

    QPDF& qpdf_document;
    std::vector<QPDFObjectHandle>& qpdf_pages;

    std::map<QPDFObjGen, int> page_numbers;  // page object -> 1-based page number
    std::map<int, page_frame> page_frames;
  };

  pdf_outline::pdf_outline(QPDF& qpdf_document_,
                           std::vector<QPDFObjectHandle>& qpdf_pages_):
    qpdf_document(qpdf_document_),
    qpdf_pages(qpdf_pages_),

    page_numbers({}),
    page_frames({})
  {
    for(std::size_t ind=0; ind<qpdf_pages.size(); ind++)
      {
        page_numbers[qpdf_pages.at(ind).getObjGen()] = static_cast<int>(ind)+1;
      }
  }

  pdf_outline::~pdf_outline()
  {}

  nlohmann::json pdf_outline::get()
  {
    LOG_S(INFO) << __FUNCTION__;

    nlohmann::json outline = nlohmann::json::value_t::null;

    QPDFOutlineDocumentHelper helper(qpdf_document);

    if(not helper.hasOutlines())
      {
        LOG_S(INFO) << "no /Outlines (=table-of-contents) detected ...";
        return outline;
      }

    LOG_S(INFO) << "/Outlines (=table-of-contents) detected!";

    // The helper has already walked the tree, breaking /First and /Next cycles by
    // object identity and capping the nesting depth, so the recursion below is
    // bounded and cycle-free by construction.
    std::vector<QPDFOutlineObjectHelper> items = helper.getTopLevelOutlines();

    outline = nlohmann::json::array({});
    for(auto& item : items)
      {
        outline.push_back(to_json(item, 0));
      }

    return outline;
  }

  nlohmann::json pdf_outline::to_json(QPDFOutlineObjectHelper& item, int level)
  {
    nlohmann::json entry = nlohmann::json::object({});

    entry["title"] = get_title(item);
    entry["level"] = level;

    nlohmann::json destination = get_destination(item);
    if(not destination.is_null())
      {
        entry["destination"] = destination;
      }

    std::vector<QPDFOutlineObjectHelper> kids = item.getKids();

    if(not kids.empty())
      {
        entry["children"] = nlohmann::json::array({});
        for(auto& kid : kids)
          {
            entry["children"].push_back(to_json(kid, level+1));
          }
      }

    return entry;
  }

  std::string pdf_outline::get_title(QPDFOutlineObjectHelper& item)
  {
    return utils::string::fix_into_valid_utf8(item.getTitle());
  }

  nlohmann::json pdf_outline::get_destination(QPDFOutlineObjectHelper& item)
  {
    nlohmann::json result = nlohmann::json::value_t::null;

    // Resolves /Dest as well as an /A << /S /GoTo /D >> action, and explicit as
    // well as named destinations (12.3.2.3). A /GoToR or /GoToE action targets
    // another document and yields nothing here, which is correct: it has no page
    // in this one.
    QPDFObjectHandle dest = item.getDest();

    if(not dest.isInitialized() or
       not dest.isArray() or
       dest.getArrayNItems()==0)
      {
        return result;
      }

    int page_number = get_destination_page(dest.getArrayItem(0));
    if(page_number<1)
      {
        return result;
      }

    pdf_destination_kind kind = DESTINATION_UNKNOWN;

    if(dest.getArrayNItems()>1)
      {
        QPDFObjectHandle name = dest.getArrayItem(1);
        if(name.isName())
          {
            kind = to_destination_kind(name.getName());
          }
      }

    const page_frame& frame = get_page_frame(page_number);

    result = nlohmann::json::object({});

    result["page_no"] = page_number;
    result["kind"] = to_string(kind);
    result["coord_origin"] = "BOTTOMLEFT";

    result["page_size"]["width"] = frame.crop_bbox[2]-frame.crop_bbox[0];
    result["page_size"]["height"] = frame.crop_bbox[3]-frame.crop_bbox[1];

    double x = frame.default_x;
    double y = frame.default_y;

    // The horizontal coordinate is only carried along so that the rotation below
    // is well defined; it is the vertical one that locates an item on the page.
    get_destination_arg(dest, destination_left_index(kind), x);
    bool has_top = get_destination_arg(dest, destination_top_index(kind), y);

    // A destination may leave any of its coordinates null, meaning "retain the
    // current value" (12.3.2.2). There is no current view to retain here, so the
    // page edge stands in for a coordinate the destination does not give, and the
    // point is only reported when the vertical position is an actual one.
    if(has_top)
      {
        utils::values::rotate_inplace(frame.angle, x, y);
        utils::values::translate_inplace(frame.delta, x, y);

        result["point"] = {x, y};
      }

    return result;
  }

  int pdf_outline::get_destination_page(QPDFObjectHandle page)
  {
    if(page.isInteger())
      {
        // A destination names its page with a page *number* only when it points
        // into another document (12.3.2.2). Producers write one in a local
        // destination too, so it is read as a 0-based page index here -- a
        // deliberate deviation from the specification.
        int page_number = static_cast<int>(page.getIntValue())+1;

        if(page_number<1 or page_number>static_cast<int>(qpdf_pages.size()))
          {
            LOG_S(WARNING) << "outline destination has an out-of-range page-index: " << page_number;
            return -1;
          }

        LOG_S(WARNING) << "outline destination uses a page-index instead of a page object";
        return page_number;
      }

    auto itr = page_numbers.find(page.getObjGen());

    if(itr==page_numbers.end())
      {
        LOG_S(WARNING) << "outline destination page is not part of the page-tree";
        return -1;
      }

    return itr->second;
  }

  bool pdf_outline::get_destination_arg(QPDFObjectHandle& dest, int arg_index, double& value)
  {
    if(arg_index<0)
      {
        return false;
      }

    // the destination array is [page /Kind arg-0 arg-1 ...]
    int index = 2+arg_index;

    if(index>=dest.getArrayNItems())
      {
        return false;
      }

    QPDFObjectHandle arg = dest.getArrayItem(index);

    if(not arg.isNumber())
      {
        return false;
      }

    value = utils::numeric::locale_safe_numeric_value(arg);
    return true;
  }

  const pdf_outline::page_frame& pdf_outline::get_page_frame(int page_number)
  {
    auto itr = page_frames.find(page_number);

    if(itr!=page_frames.end())
      {
        return itr->second;
      }

    page_item<PAGE_DIMENSION> dimension;
    dimension.execute(qpdf_pages.at(page_number-1));

    // captured before rotate(), which transforms the page boxes in place
    std::array<double, 4> media_bbox = dimension.get_media_bbox();

    page_frame frame;
    frame.angle = dimension.get_angle();
    frame.delta = {0.0, 0.0};
    frame.default_x = media_bbox[0];
    frame.default_y = media_bbox[3];

    // pdf_decoder<PAGE>::rotate_contents() leaves a page whose angle is a whole
    // number of turns untouched, so this frame has to leave it untouched too.
    if((frame.angle%360)!=0)
      {
        frame.delta = dimension.rotate(frame.angle);
      }
    else
      {
        frame.angle = 0;
      }

    frame.crop_bbox = dimension.get_crop_bbox();

    return page_frames.emplace(page_number, frame).first->second;
  }

}

#endif
