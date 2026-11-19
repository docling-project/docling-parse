//-*-C++-*-

#ifndef PDF_ANNOTATION_OPS_H
#define PDF_ANNOTATION_OPS_H

#include <array>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#define POINTERHOLDER_TRANSITION 0 // eliminate warnings from QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <edit/catalog_ops.h>
#include <edit/edit_error.h>
#include <edit/form_ops.h>
#include <edit/object_json.h>

namespace pdflib
{
  // The annotations of a page (ISO 32000-1, 12.5): reading them, adding one
  // given as a dictionary, and removing them by subtype.
  class pdf_annotation_ops
  {
  public:

    // One JSON object per annotation: its dictionary, without the entries
    // that lead back into the document (/P, /Parent, /Popup) or hold drawing
    // code (/AP). A link's target page is added as "page" when the link
    // points into the document.
    static nlohmann::json read(QPDF& qpdf, QPDFPageObjectHelper page);

    // Add the annotation written as a dictionary in PDF syntax. /Subtype and
    // /Rect are required; /Type is filled in.
    static void add(QPDF& qpdf, QPDFPageObjectHelper page, const std::string& dictionary);

    // Add a link over `rectangle` that opens page `target` of this document.
    static void add_page_link(QPDF& qpdf,
                              QPDFPageObjectHelper page,
                              const std::array<double, 4>& rectangle,
                              int target);

    // Remove the annotations whose /Subtype is in `subtypes` (with their
    // slash); an empty set removes all except form widgets, which have to be
    // named to be removed, and then take their form fields with them.
    // Returns the number removed.
    static int remove(QPDF& qpdf, QPDFPageObjectHelper page, const std::set<std::string>& subtypes);

  private:

    static void append(QPDFPageObjectHelper page, QPDFObjectHandle annotation);
  };

  inline nlohmann::json pdf_annotation_ops::read(QPDF& qpdf, QPDFPageObjectHelper page)
  {
    nlohmann::json result = nlohmann::json::array();

    QPDFObjectHandle annotations = page.getObjectHandle().getKey("/Annots");
    if(not annotations.isArray())
      {
        return result;
      }

    std::map<QPDFObjGen, int> indices = pdf_object_json::page_indices(qpdf);
    const std::set<std::string> skipped = {"/P", "/Parent", "/Popup", "/AP", "/IRT"};

    for(QPDFObjectHandle& annotation : annotations.getArrayAsVector())
      {
        if(not annotation.isDictionary())
          {
            continue;
          }

        // /Dest and an action's /D start with a page object: show the page
        // index instead of walking into the page.
        std::set<std::string> skipped_here = skipped;
        skipped_here.insert("/Dest");

        nlohmann::json entry = pdf_object_json::to_json(annotation, 4, skipped_here);

        QPDFObjectHandle destination = annotation.getKey("/Dest");
        QPDFObjectHandle action = annotation.getKey("/A");
        if(not destination.isArray() and
           action.isDictionary() and
           action.getKey("/S").isNameAndEquals("/GoTo"))
          {
            destination = action.getKey("/D");
            if(entry.contains("/A") and entry["/A"].is_object())
              {
                entry["/A"].erase("/D");
              }
          }

        int target = pdf_object_json::destination_page(destination, indices);
        if(target >= 0)
          {
            entry["page"] = target;
          }

        result.push_back(entry);
      }

    return result;
  }

  inline void pdf_annotation_ops::append(QPDFPageObjectHelper page, QPDFObjectHandle annotation)
  {
    QPDFObjectHandle page_object = page.getObjectHandle();

    QPDFObjectHandle annotations = page_object.getKey("/Annots");
    if(not annotations.isArray())
      {
        annotations = QPDFObjectHandle::newArray();
        page_object.replaceKey("/Annots", annotations);
      }

    annotation.replaceKey("/P", page_object);
    annotations.appendItem(annotation);
  }

  inline void pdf_annotation_ops::add(QPDF& qpdf, QPDFPageObjectHelper page, const std::string& dictionary)
  {
    QPDFObjectHandle annotation = QPDFObjectHandle::parse(dictionary, "annotation");

    if(not annotation.isDictionary())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "an annotation must be a dictionary");
      }
    if(not annotation.getKey("/Subtype").isName())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "an annotation needs a /Subtype");
      }
    if(not annotation.getKey("/Rect").isRectangle())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "an annotation needs a /Rect of four numbers");
      }

    annotation.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));

    append(page, qpdf.makeIndirectObject(annotation));
  }

  inline void pdf_annotation_ops::add_page_link(QPDF& qpdf,
                                                QPDFPageObjectHelper page,
                                                const std::array<double, 4>& rectangle,
                                                int target)
  {
    if(not (std::isfinite(rectangle[0]) and std::isfinite(rectangle[1]) and
            std::isfinite(rectangle[2]) and std::isfinite(rectangle[3]) and
            rectangle[0] < rectangle[2] and rectangle[1] < rectangle[3]))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "a link rectangle must be finite and have a positive width and height");
      }

    QPDFObjectHandle link = QPDFObjectHandle::newDictionary();
    link.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    link.replaceKey("/Subtype", QPDFObjectHandle::newName("/Link"));
    link.replaceKey("/Rect",
                    QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(rectangle[0], rectangle[1],
                                                                           rectangle[2], rectangle[3])));
    // no visible border, as link annotations are usually written
    link.replaceKey("/Border", QPDFObjectHandle::parse("[0 0 0]"));
    link.replaceKey("/Dest", pdf_catalog_ops::fit_destination(qpdf, target));

    append(page, qpdf.makeIndirectObject(link));
  }

  inline int pdf_annotation_ops::remove(QPDF& qpdf,
                                        QPDFPageObjectHelper page,
                                        const std::set<std::string>& subtypes)
  {
    QPDFObjectHandle page_object = page.getObjectHandle();

    QPDFObjectHandle annotations = page_object.getKey("/Annots");
    if(not annotations.isArray())
      {
        return 0;
      }

    std::vector<QPDFObjectHandle> kept;
    std::vector<QPDFObjectHandle> removed_widgets;
    int removed = 0;

    for(QPDFObjectHandle& annotation : annotations.getArrayAsVector())
      {
        std::string subtype;
        if(annotation.isDictionary() and annotation.getKey("/Subtype").isName())
          {
            subtype = annotation.getKey("/Subtype").getName();
          }

        bool selected = subtypes.empty() ? (subtype != "/Widget") : (subtypes.count(subtype) > 0);
        if(not selected)
          {
            kept.push_back(annotation);
            continue;
          }

        if(subtype == "/Widget")
          {
            removed_widgets.push_back(annotation);
          }
        removed += 1;
      }

    if(removed == 0)
      {
        return 0;
      }

    // A widget is the visible part of a form field: without it the field
    // would stay in the form with nothing to show it.
    if(not removed_widgets.empty())
      {
        pdf_form_ops::detach_widgets(qpdf, removed_widgets);
      }

    if(kept.empty())
      {
        page_object.removeKey("/Annots");
      }
    else
      {
        page_object.replaceKey("/Annots", QPDFObjectHandle::newArray(kept));
      }

    return removed;
  }

}

#endif
