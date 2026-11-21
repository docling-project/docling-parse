//-*-C++-*-

#ifndef PDF_NAVIGATION_OPS_H
#define PDF_NAVIGATION_OPS_H

#include <string>
#include <vector>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>

#include <edit/object_json.h>

namespace pdflib
{
  // Maintain local destinations when a page leaves the page tree. Resolve
  // names before removing their definitions, including legacy /Dests entries.
  class pdf_navigation_ops
  {
  public:
    static void drop_destinations_to(QPDF& qpdf, QPDFObjGen page);

  private:
    static bool points_at(QPDFObjectHandle destination,
                          QPDFObjGen page,
                          QPDFOutlineDocumentHelper& resolver);
  };

  inline bool pdf_navigation_ops::points_at(QPDFObjectHandle destination,
                                             QPDFObjGen page,
                                             QPDFOutlineDocumentHelper& resolver)
  {
    destination = pdf_object_json::resolve_destination(destination, resolver);
    return destination.isArray() and destination.getArrayNItems() > 0 and
      destination.getArrayItem(0).isIndirect() and
      destination.getArrayItem(0).getObjGen() == page;
  }

  inline void pdf_navigation_ops::drop_destinations_to(QPDF& qpdf, QPDFObjGen page)
  {
    QPDFOutlineDocumentHelper resolver(qpdf);

    auto drop_target = [&](QPDFObjectHandle object) {
        if(points_at(object.getKey("/Dest"), page, resolver))
          {
            object.removeKey("/Dest");
          }
        auto action = object.getKey("/A");
        if(action.isDictionary() and action.getKey("/S").isNameAndEquals("/GoTo") and
           points_at(action, page, resolver))
          {
            object.removeKey("/A");
          }
      };

    // Resolve references before their named destination definitions are removed.
    for(auto object : qpdf.getAllPages())
      {
        auto annotations = object.getKey("/Annots");
        if(annotations.isArray())
          {
            for(auto annotation : annotations.getArrayAsVector())
              {
                if(annotation.isDictionary())
                  {
                    drop_target(annotation);
                  }
              }
          }
      }

    auto outlines = resolver.getTopLevelOutlines();
    while(not outlines.empty())
      {
        auto item = outlines.back();
        outlines.pop_back();
        drop_target(item.getObjectHandle());
        auto children = item.getKids();
        outlines.insert(outlines.end(), children.begin(), children.end());
      }

    QPDFObjectHandle root = qpdf.getRoot();
    if(points_at(root.getKey("/OpenAction"), page, resolver))
      {
        root.removeKey("/OpenAction");
      }
    QPDFObjectHandle legacy = root.getKey("/Dests");
    if(legacy.isDictionary())
      {
        for(const std::string& name : legacy.getKeys())
          {
            if(points_at(legacy.getKey(name), page, resolver))
              {
                legacy.removeKey(name);
              }
          }
      }
    QPDFObjectHandle tree_object = root.getKey("/Names").getKey("/Dests");
    if(tree_object.isDictionary())
      {
        QPDFNameTreeObjectHelper tree(tree_object, qpdf);
        std::vector<std::string> removed;
        for(auto item = tree.begin(); item != tree.end(); ++item)
          {
            if(points_at(item->second, page, resolver))
              {
                removed.push_back(item->first);
              }
          }
        for(const std::string& name : removed)
          {
            tree.remove(name);
          }
      }
  }
}

#endif
