//-*-C++-*-

#ifndef QPDF_PAGE_INTEGRITY_H
#define QPDF_PAGE_INTEGRITY_H

#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include <qpdf/QPDFObjectHandle.hh>

namespace pdflib
{
  // Thrown for a page whose dictionary survived in the file while an object
  // it needs for its content did not (docling-parse#374).
  class missing_page_object_error : public std::runtime_error
  {
  public:
    using std::runtime_error::runtime_error;
  };

  namespace qpdf_page_integrity
  {
    // A reference `N G R` to an object that the file does not contain
    // resolves to null, as ISO 32000-1 section 7.3.10 requires, and qpdf
    // does so without a warning. The handle keeps its object number, so a
    // dangling reference is an *indirect* null, unlike a literal `null`
    // value or an absent key.
    //
    // Note that QPDFObjectHandle::hasKey() reports false for a key whose
    // value resolves to null, so the callers below read keys with getKey()
    // and inspect the handle they get back.
    inline bool is_dangling(QPDFObjectHandle obj)
    {
      return obj.isIndirect() and obj.isNull();
    }

    inline std::string reference(QPDFObjectHandle obj)
    {
      return std::to_string(obj.getObjectID()) + " "
        + std::to_string(obj.getGeneration()) + " R";
    }

    // The /Resources dictionary a /Pages ancestor hands down to the page
    // (ISO 32000-1 table 30), or null when no ancestor carries one.
    inline QPDFObjectHandle inherited_resources(QPDFObjectHandle page)
    {
      std::set<std::pair<int, int> > seen;

      QPDFObjectHandle node = page;
      while(node.isDictionary())
        {
          node = node.getKey("/Parent");
          if(not node.isDictionary())
            {
              break;
            }

          std::pair<int, int> id(node.getObjectID(), node.getGeneration());
          if(not seen.insert(id).second)
            {
              break; // cycle
            }

          QPDFObjectHandle resources = node.getKey("/Resources");
          if(resources.isDictionary())
            {
              return resources;
            }
        }

      return QPDFObjectHandle::newNull();
    }

    // A dangling entry in one resource category (/Font or /XObject): the
    // category dictionary itself, or one of the objects it names.
    inline std::string find_dangling_resource(QPDFObjectHandle resources,
                                              const std::string& category)
    {
      if(not resources.isDictionary())
        {
          return "";
        }

      QPDFObjectHandle group = resources.getKey(category);
      if(is_dangling(group))
        {
          return "/Resources" + category + " " + reference(group);
        }

      if(not group.isDictionary())
        {
          return "";
        }

      // getKeys() leaves out every key whose value resolves to null, which
      // is exactly the entry looked for here; the raw map keeps it.
      for(const auto& entry : group.getDictAsMap())
        {
          if(is_dangling(entry.second))
            {
              return "/Resources" + category + entry.first + " " + reference(entry.second);
            }
        }

      return "";
    }

    // Describes the first object the page needs for its content but the
    // file does not contain, or returns an empty string when the page is
    // complete.
    //
    // A file cut short by a failed transfer keeps the page dictionaries
    // written before the cut while their content streams are gone. qpdf
    // reconstructs the cross-reference table and recovers a document whose
    // pages all exist and are blank, so the loss is invisible downstream
    // unless the references are checked here, against the original
    // document: once the page is copied into a standalone buffer, qpdf
    // writes a dangling reference as a plain `null`.
    //
    // The page's /Contents, its /Resources dictionary and the /Font and
    // /XObject entries of that dictionary are inspected: those carry the
    // text and images of the page, and the decoder skips text set in a
    // font it cannot find rather than failing. An absent /Contents, a
    // literal `null` and an empty array are valid blank pages and stay
    // successful. A dangling page-level /Resources is accepted when an
    // ancestor supplies the resources instead.
    //
    // qpdf replaces a reference it can already tell is dangling while it
    // parses the page -- an object number outside an intact cross-reference
    // table -- by a plain null, which is indistinguishable from an
    // intentional one. A truncated file has lost its cross-reference table,
    // so there the object numbers survive and the check sees them.
    inline std::string find_missing_content(QPDFObjectHandle page)
    {
      if(not page.isDictionary())
        {
          return "";
        }

      QPDFObjectHandle contents = page.getKey("/Contents");
      if(is_dangling(contents))
        {
          return "/Contents " + reference(contents);
        }

      if(contents.isArray())
        {
          int n_items = contents.getArrayNItems();
          for(int i = 0; i < n_items; i++)
            {
              QPDFObjectHandle item = contents.getArrayItem(i);
              if(is_dangling(item))
                {
                  return "/Contents[" + std::to_string(i) + "] " + reference(item);
                }
            }
        }

      QPDFObjectHandle resources = page.getKey("/Resources");
      if(is_dangling(resources))
        {
          QPDFObjectHandle inherited = inherited_resources(page);
          if(not inherited.isDictionary())
            {
              return "/Resources " + reference(resources);
            }
          resources = inherited;
        }
      else if(not resources.isDictionary())
        {
          resources = inherited_resources(page);
        }

      for(const char* category : {"/Font", "/XObject"})
        {
          std::string missing = find_dangling_resource(resources, category);
          if(not missing.empty())
            {
              return missing;
            }
        }

      return "";
    }
  }
}

#endif
