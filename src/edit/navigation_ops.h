//-*-C++-*-

#ifndef PDF_NAVIGATION_OPS_H
#define PDF_NAVIGATION_OPS_H

#include <string>
#include <vector>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>

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
    if(destination.isName() or destination.isString())
      {
        destination = resolver.resolveNamedDest(destination);
      }
    if(destination.isDictionary())
      {
        destination = destination.getKey("/D");
      }
    return destination.isArray() and destination.getArrayNItems() > 0 and
      destination.getArrayItem(0).isIndirect() and
      destination.getArrayItem(0).getObjGen() == page;
  }

  inline void pdf_navigation_ops::drop_destinations_to(QPDF& qpdf, QPDFObjGen page)
  {
    QPDFOutlineDocumentHelper resolver(qpdf);

    // Links, bookmarks and the document open action can all use names.
    // Inspect indirect dictionaries without decoding any content streams.
    for(QPDFObjectHandle object : qpdf.getAllObjects())
      {
        if(not object.isDictionary())
          {
            continue;
          }
        if(points_at(object.getKey("/Dest"), page, resolver))
          {
            object.removeKey("/Dest");
          }
        for(const std::string key : {"/A", "/OpenAction"})
          {
            QPDFObjectHandle action = object.getKey(key);
            if((key == "/OpenAction" and points_at(action, page, resolver)) or
               (action.isDictionary() and action.getKey("/S").isNameAndEquals("/GoTo") and
                points_at(action.getKey("/D"), page, resolver)))
              {
                object.removeKey(key);
              }
          }
      }

    QPDFObjectHandle root = qpdf.getRoot();
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
