//-*-C++-*-

#ifndef QPDF_STRUCTURE_H
#define QPDF_STRUCTURE_H

#include <array>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjGen.hh>

namespace pdflib
{
  // The logical structure tree of a tagged PDF (ISO 32000-2, 14.7): every
  // structure element with its type, text entries, attributes and kids, in
  // depth-first order, which is the logical content order (14.8.2.5).
  //
  // Pages follow the conventions of pdf_outline: a page is reported as its
  // 1-based page number (`page_no`), omitted when it cannot be resolved. A
  // Layout /BBox attribute is additionally reported as `bbox`, in the frame the
  // page's cells are reported in: the page normalised by its /Rotate angle and
  // moved to the origin of the crop box (the default page boundary), bottom-left
  // origin. The raw attribute is kept as authored, in default user space.
  //
  // Only dictionary keys are read here. No content stream is decoded and no font
  // is loaded, which keeps the structure tree as cheap as the rest of the
  // annotations.
  class pdf_structure
  {
  public:

    pdf_structure(QPDF& qpdf_document,
                  std::vector<QPDFObjectHandle>& qpdf_pages);
    ~pdf_structure();

    nlohmann::json get();

  private:

    // The transform pdf_decoder<PAGE> applies to its cells, measured once per
    // page that carries a Layout /BBox.
    struct page_frame
    {
      int angle;                        // /Rotate, which the transform normalises away
      std::pair<double, double> delta;  // translation that follows the rotation
      std::pair<double, double> origin; // crop-box origin the sanitator subtracts
    };

    nlohmann::json to_json(QPDFObjectHandle elem, int level);

    nlohmann::json get_kid(QPDFObjectHandle kid, int elem_page_no, int level);

    int get_page_no(QPDFObjectHandle page);

    nlohmann::json get_attributes(QPDFObjectHandle attrs);

    nlohmann::json get_bbox(nlohmann::json& attributes, int page_no);

    const page_frame& get_page_frame(int page_no);

    static std::string get_id(QPDFObjectHandle obj);

    static void get_string(QPDFObjectHandle& elem, const std::string& key,
                           const std::string& name, nlohmann::json& out);

  private:

    QPDF& qpdf_document;
    std::vector<QPDFObjectHandle>& qpdf_pages;

    std::map<QPDFObjGen, int> page_numbers;  // page object -> 1-based page number
    std::map<int, page_frame> page_frames;

    std::set<QPDFObjGen> visited;
    int order;
  };

  pdf_structure::pdf_structure(QPDF& qpdf_document_,
                               std::vector<QPDFObjectHandle>& qpdf_pages_):
    qpdf_document(qpdf_document_),
    qpdf_pages(qpdf_pages_),

    page_numbers({}),
    page_frames({}),

    visited({}),
    order(0)
  {
    for(std::size_t ind=0; ind<qpdf_pages.size(); ind++)
      {
        page_numbers[qpdf_pages.at(ind).getObjGen()] = static_cast<int>(ind)+1;
      }
  }

  pdf_structure::~pdf_structure()
  {}

  nlohmann::json pdf_structure::get()
  {
    LOG_S(INFO) << __FUNCTION__;

    nlohmann::json structure = nlohmann::json::value_t::null;

    QPDFObjectHandle root = qpdf_document.getRoot();

    if(not root.hasKey("/StructTreeRoot") or not root.getKey("/StructTreeRoot").isDictionary())
      {
        LOG_S(INFO) << "no /StructTreeRoot detected ...";
        return structure;
      }

    QPDFObjectHandle tree = root.getKey("/StructTreeRoot");
    structure = nlohmann::json::object({});

    // /MarkInfo /Marked declares the file as tagged (14.7.1)
    bool marked = false;
    if(root.hasKey("/MarkInfo") and root.getKey("/MarkInfo").isDictionary())
      {
        QPDFObjectHandle mark_info = root.getKey("/MarkInfo");
        if(mark_info.hasKey("/Marked") and mark_info.getKey("/Marked").isBool())
          {
            marked = mark_info.getKey("/Marked").getBoolValue();
          }
      }
    structure["marked"] = marked;

    nlohmann::json role_map = nlohmann::json::object({});
    if(tree.hasKey("/RoleMap") and tree.getKey("/RoleMap").isDictionary())
      {
        QPDFObjectHandle rm = tree.getKey("/RoleMap");
        for(auto key : rm.getKeys())
          {
            QPDFObjectHandle val = rm.getKey(key);
            if(val.isName()) { role_map[key] = val.getName(); }
          }
      }
    structure["role_map"] = role_map;

    if(tree.hasKey("/Namespaces") and tree.getKey("/Namespaces").isArray())
      {
        nlohmann::json namespaces = nlohmann::json::array({});
        QPDFObjectHandle nss = tree.getKey("/Namespaces");
        for(int i=0; i<nss.getArrayNItems(); i++)
          {
            QPDFObjectHandle ns = nss.getArrayItem(i);
            if(ns.isDictionary() and ns.hasKey("/NS") and ns.getKey("/NS").isString())
              {
                namespaces.push_back(ns.getKey("/NS").getUTF8Value());
              }
          }
        structure["namespaces"] = namespaces;
      }

    std::vector<QPDFObjectHandle> roots;
    if(tree.hasKey("/K"))
      {
        QPDFObjectHandle k = tree.getKey("/K");
        if(k.isArray())
          {
            for(int i=0; i<k.getArrayNItems(); i++) { roots.push_back(k.getArrayItem(i)); }
          }
        else if(k.isDictionary())
          {
            roots.push_back(k);
          }
      }

    nlohmann::json elements = nlohmann::json::array({});
    for(auto& elem : roots)
      {
        nlohmann::json e = to_json(elem, 0);
        if(not e.empty()) { elements.push_back(e); }
      }
    structure["elements"] = elements;

    LOG_S(INFO) << "structure tree: " << order << " element(s), marked=" << marked;
    return structure;
  }

  nlohmann::json pdf_structure::to_json(QPDFObjectHandle elem, int level)
  {
    nlohmann::json result = nlohmann::json::object({});

    if(level>=64 or not elem.isDictionary())
      {
        return result;
      }

    // cycles are broken by object identity; direct objects cannot be referenced
    // twice, so they need no entry
    if(elem.isIndirect())
      {
        if(not visited.insert(elem.getObjGen()).second)
          {
            LOG_S(WARNING) << "cyclic structure element reference, skipping: " << get_id(elem);
            return result;
          }
      }

    result["id"]    = get_id(elem);
    result["order"] = order++;

    if(elem.hasKey("/S") and elem.getKey("/S").isName())
      {
        result["type"] = elem.getKey("/S").getName();
      }
    else
      {
        result["type"] = "";
      }

    // PDF 2.0 namespace (14.7.4): /NS is a namespace dictionary with a /NS string
    if(elem.hasKey("/NS") and elem.getKey("/NS").isDictionary())
      {
        QPDFObjectHandle ns = elem.getKey("/NS");
        if(ns.hasKey("/NS") and ns.getKey("/NS").isString())
          {
            result["namespace"] = ns.getKey("/NS").getUTF8Value();
          }
      }

    get_string(elem, "/T",          "title",       result);
    get_string(elem, "/Lang",       "lang",        result);
    get_string(elem, "/Alt",        "alt",         result);
    get_string(elem, "/ActualText", "actual_text", result);
    get_string(elem, "/E",          "expansion",   result);

    if(elem.hasKey("/ID") and elem.getKey("/ID").isString())
      {
        result["element_id"] = elem.getKey("/ID").getUTF8Value();
      }

    int page_no = -1;
    if(elem.hasKey("/Pg"))
      {
        page_no = get_page_no(elem.getKey("/Pg"));
        if(page_no>=1) { result["page_no"] = page_no; }
      }

    if(elem.hasKey("/A"))
      {
        nlohmann::json attributes = get_attributes(elem.getKey("/A"));

        nlohmann::json bbox = get_bbox(attributes, page_no);
        if(not bbox.is_null()) { result["bbox"] = bbox; }

        result["attributes"] = attributes;
      }

    if(elem.hasKey("/Ref") and elem.getKey("/Ref").isArray())
      {
        nlohmann::json refs = nlohmann::json::array({});
        QPDFObjectHandle ref = elem.getKey("/Ref");
        for(int i=0; i<ref.getArrayNItems(); i++)
          {
            refs.push_back(get_id(ref.getArrayItem(i)));
          }
        result["ref"] = refs;
      }

    // Kids (14.7.5.2): a single kid or an array of kids, each being an integer
    // MCID on the element's page, a marked-content reference dict (/Type /MCR),
    // an object reference dict (/Type /OBJR), or a child element.
    std::vector<QPDFObjectHandle> kid_items;
    if(elem.hasKey("/K"))
      {
        QPDFObjectHandle k = elem.getKey("/K");
        if(k.isArray())
          {
            for(int i=0; i<k.getArrayNItems(); i++) { kid_items.push_back(k.getArrayItem(i)); }
          }
        else if(not k.isNull())
          {
            kid_items.push_back(k);
          }
      }

    nlohmann::json kids = nlohmann::json::array({});
    for(auto& kid : kid_items)
      {
        nlohmann::json item = get_kid(kid, page_no, level);
        if(not item.is_null()) { kids.push_back(item); }
      }
    result["kids"] = kids;

    return result;
  }

  nlohmann::json pdf_structure::get_kid(QPDFObjectHandle kid, int elem_page_no, int level)
  {
    nlohmann::json result = nlohmann::json::value_t::null;

    if(kid.isInteger())
      {
        result = {{"kind", "mcid"}, {"mcid", static_cast<int>(kid.getIntValue())}};
        if(elem_page_no>=1) { result["page_no"] = elem_page_no; }

        return result;
      }

    if(not kid.isDictionary())
      {
        return result;
      }

    std::string type = "";
    if(kid.hasKey("/Type") and kid.getKey("/Type").isName())
      {
        type = kid.getKey("/Type").getName();
      }

    if(type!="/MCR" and type!="/OBJR")
      {
        result = to_json(kid, level+1);
        if(result.empty())
          {
            return nlohmann::json::value_t::null;
          }

        result["kind"] = "element";
        return result;
      }

    // a marked-content or object reference lives on its own /Pg when it has one
    int page_no = kid.hasKey("/Pg") ? get_page_no(kid.getKey("/Pg")) : elem_page_no;

    if(type=="/MCR")
      {
        int mcid = -1;
        if(kid.hasKey("/MCID") and kid.getKey("/MCID").isInteger())
          {
            mcid = static_cast<int>(kid.getKey("/MCID").getIntValue());
          }
        result = {{"kind", "mcid"}, {"mcid", mcid}};
      }
    else
      {
        std::string obj = "";
        std::string subtype = "";
        if(kid.hasKey("/Obj"))
          {
            QPDFObjectHandle target = kid.getKey("/Obj");
            obj = get_id(target);
            if(target.isDictionary() and target.hasKey("/Subtype") and
               target.getKey("/Subtype").isName())
              {
                subtype = target.getKey("/Subtype").getName();
              }
          }
        result = {{"kind", "objref"}, {"obj", obj}, {"subtype", subtype}};
      }

    if(page_no>=1) { result["page_no"] = page_no; }

    return result;
  }

  int pdf_structure::get_page_no(QPDFObjectHandle page)
  {
    if(not page.isDictionary())
      {
        return -1;
      }

    auto itr = page_numbers.find(page.getObjGen());

    if(itr==page_numbers.end())
      {
        LOG_S(WARNING) << "structure element page is not part of the page-tree";
        return -1;
      }

    return itr->second;
  }

  // Attribute objects (/A) are a dictionary or an array of dictionaries, each
  // keyed by its owner (/O): Layout, List, Table, PrintField, Artifact,
  // ARIA-1.1, ... Returned as {owner: {name: value}} with values via to_json.
  nlohmann::json pdf_structure::get_attributes(QPDFObjectHandle attrs)
  {
    nlohmann::json result = nlohmann::json::object({});

    std::vector<QPDFObjectHandle> dicts;
    if(attrs.isDictionary())
      {
        dicts.push_back(attrs);
      }
    else if(attrs.isArray())
      {
        for(int i=0; i<attrs.getArrayNItems(); i++)
          {
            QPDFObjectHandle item = attrs.getArrayItem(i);
            if(item.isDictionary()) { dicts.push_back(item); }
            // integers interleaved in the array are revision numbers: skipped
          }
      }

    for(auto& dict : dicts)
      {
        std::string owner = "";
        if(dict.hasKey("/O") and dict.getKey("/O").isName())
          {
            owner = dict.getKey("/O").getName();
          }

        nlohmann::json values = nlohmann::json::object({});
        for(auto key : dict.getKeys())
          {
            if(key=="/O") { continue; }
            values[key] = pdflib::to_json(dict.getKey(key), {}, 0, 4);
          }
        result[owner] = values;
      }

    return result;
  }

  // The Layout /BBox (14.8.5.4.3) of an element on a known page, as a bottom-left
  // box in the frame of that page's cells.
  nlohmann::json pdf_structure::get_bbox(nlohmann::json& attributes, int page_no)
  {
    nlohmann::json result = nlohmann::json::value_t::null;

    if(page_no<1 or
       not attributes.contains("/Layout") or
       not attributes["/Layout"].contains("/BBox"))
      {
        return result;
      }

    const nlohmann::json& raw = attributes["/Layout"]["/BBox"];
    if(not raw.is_array() or raw.size()!=4)
      {
        return result;
      }

    std::array<double, 4> bbox;
    for(std::size_t i=0; i<4; i++)
      {
        if(not raw.at(i).is_number())
          {
            return result;
          }
        bbox[i] = raw.at(i).get<double>();
      }

    // a rectangle may be written with its corners in either order
    bbox = {std::min(bbox[0], bbox[2]), std::min(bbox[1], bbox[3]),
            std::max(bbox[0], bbox[2]), std::max(bbox[1], bbox[3])};

    const page_frame& frame = get_page_frame(page_no);

    // the rotation rotate_contents() applies ...
    utils::values::transform_bottomleft_bbox_inplace(frame.angle, frame.delta, bbox);

    // ... followed by the translation the dimension sanitator applies
    bbox[0] -= frame.origin.first;
    bbox[1] -= frame.origin.second;
    bbox[2] -= frame.origin.first;
    bbox[3] -= frame.origin.second;

    result = nlohmann::json::object({});
    result["l"] = bbox[0];
    result["b"] = bbox[1];
    result["r"] = bbox[2];
    result["t"] = bbox[3];
    result["coord_origin"] = "BOTTOMLEFT";

    return result;
  }

  const pdf_structure::page_frame& pdf_structure::get_page_frame(int page_no)
  {
    auto itr = page_frames.find(page_no);

    if(itr!=page_frames.end())
      {
        return itr->second;
      }

    page_item<PAGE_DIMENSION> dimension;
    dimension.execute(qpdf_pages.at(page_no-1));

    page_frame frame;
    frame.angle = dimension.get_angle();
    frame.delta = {0.0, 0.0};

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

    // read after rotate(), which transforms the page boxes in place, exactly as
    // pdf_decoder<PAGE> reads the boundary it subtracted from the cells
    std::array<double, 4> crop_bbox = dimension.get_crop_bbox();
    frame.origin = {crop_bbox[0], crop_bbox[1]};

    return page_frames.emplace(page_no, frame).first->second;
  }

  std::string pdf_structure::get_id(QPDFObjectHandle obj)
  {
    // "obj gen" for indirect objects, an empty string for direct ones (rare, and
    // then not referenceable anyway)
    if(obj.isIndirect())
      {
        return std::to_string(obj.getObjectID())+" "+std::to_string(obj.getGeneration());
      }
    return "";
  }

  void pdf_structure::get_string(QPDFObjectHandle& elem, const std::string& key,
                                 const std::string& name, nlohmann::json& out)
  {
    if(elem.hasKey(key) and elem.getKey(key).isString())
      {
        out[name] = utils::string::fix_into_valid_utf8(elem.getKey(key).getUTF8Value());
      }
  }

}

#endif
