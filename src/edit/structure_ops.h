//-*-C++-*-

#ifndef PDF_STRUCTURE_OPS_H
#define PDF_STRUCTURE_OPS_H

#include <map>
#include <set>
#include <vector>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>
#include <qpdf/QPDFNumberTreeObjectHelper.hh>

namespace pdflib
{
  // Remove one page's entries from the tagged PDF structure tree.
  class pdf_structure_ops
  {
  public:
    static void detach_page(QPDF& qpdf, QPDFObjectHandle page);

  private:
    explicit pdf_structure_ops(QPDFObjectHandle page);

    bool prune_item(QPDFObjectHandle item, bool on_removed_page, int depth);
    bool prune_children(QPDFObjectHandle element, bool on_removed_page, int depth);
    bool was_pruned(const QPDFObjectHandle& node) const;

    QPDFObjectHandle page;
    std::set<QPDFObjGen> annotations;
    std::set<long long> parent_keys;
    std::map<QPDFObjGen, bool> visited;
  };

  inline pdf_structure_ops::pdf_structure_ops(QPDFObjectHandle page):
    page(page)
  {
    auto key = page.getKey("/StructParents");
    if(key.isInteger())
      {
        parent_keys.insert(key.getIntValue());
      }
    auto annots = page.getKey("/Annots");
    if(annots.isArray())
      {
        for(auto annotation : annots.getArrayAsVector())
          {
            if(annotation.isDictionary())
              {
                if(annotation.isIndirect())
                  {
                    annotations.insert(annotation.getObjGen());
                  }
                auto parent = annotation.getKey("/StructParent");
                if(parent.isInteger())
                  {
                    parent_keys.insert(parent.getIntValue());
                  }
              }
          }
      }
  }

  inline bool pdf_structure_ops::was_pruned(const QPDFObjectHandle& node) const
  {
    auto iter = visited.find(node.getObjGen());
    return node.isIndirect() and iter != visited.end() and iter->second;
  }

  inline bool pdf_structure_ops::prune_item(QPDFObjectHandle item, bool on_removed_page, int depth)
  {
    if(item.isInteger())
      {
        // A bare MCID inherits the enclosing element's page.
        return on_removed_page;
      }
    if(not item.isDictionary() or depth >= 256)
      {
        return false;
      }

    auto seen = visited.end();
    if(item.isIndirect())
      {
        auto [iter, inserted] = visited.emplace(item.getObjGen(), false);
        if(not inserted)
          {
            return iter->second;
          }
        seen = iter;
      }

    auto explicit_page = item.getKey("/Pg");
    bool explicit_removed_page = explicit_page.isSameObjectAs(page);
    if(not explicit_page.isNull())
      {
        on_removed_page = explicit_removed_page;
      }

    bool drop = false;
    auto type = item.getKey("/Type");
    if(type.isNameAndEquals("/MCR") or type.isNameAndEquals("/OBJR"))
      {
        auto object = item.getKey("/Obj");
        bool removed_object = object.isSameObjectAs(page) or
                              (object.isIndirect() and annotations.count(object.getObjGen()) > 0);
        drop = on_removed_page or removed_object;
        if(removed_object)
          {
            item.removeKey("/Obj");
          }
      }
    else
      {
        bool pruned_children = prune_children(item, on_removed_page, depth + 1);
        drop = (on_removed_page or pruned_children) and not item.hasKey("/K");
      }

    if(explicit_removed_page)
      {
        item.removeKey("/Pg");
      }
    if(seen != visited.end())
      {
        seen->second = drop;
      }
    return drop;
  }

  inline bool pdf_structure_ops::prune_children(QPDFObjectHandle element,
                                                bool on_removed_page,
                                                int depth)
  {
    auto kids = element.getKey("/K");
    if(not kids.isArray())
      {
        if(prune_item(kids, on_removed_page, depth))
          {
            element.removeKey("/K");
            return true;
          }
        return false;
      }

    std::vector<QPDFObjectHandle> kept;
    for(auto kid : kids.getArrayAsVector())
      {
        if(not prune_item(kid, on_removed_page, depth))
          {
            kept.push_back(kid);
          }
      }
    if(kept.size() == static_cast<std::size_t>(kids.getArrayNItems()))
      {
        return false;
      }
    if(kept.empty())
      {
        element.removeKey("/K");
      }
    else
      {
        element.replaceKey("/K", QPDFObjectHandle::newArray(kept));
      }
    return true;
  }

  inline void pdf_structure_ops::detach_page(QPDF& qpdf, QPDFObjectHandle page)
  {
    auto root = qpdf.getRoot().getKey("/StructTreeRoot");
    if(not root.isDictionary())
      {
        return;
      }

    pdf_structure_ops cleanup(page);
    cleanup.prune_children(root, false, 0);

    auto parent_tree = root.getKey("/ParentTree");
    if(parent_tree.isDictionary())
      {
        QPDFNumberTreeObjectHelper parents(parent_tree, qpdf);
        for(const auto& [key, entry] : parents.getAsMap())
          {
            if(cleanup.parent_keys.count(key) > 0 or cleanup.was_pruned(entry))
              {
                parents.remove(key);
                continue;
              }
            auto value = entry;
            if(value.isArray())
              {
                for(int index = 0; index < value.getArrayNItems(); index++)
                  {
                    if(cleanup.was_pruned(value.getArrayItem(index)))
                      {
                        // Parent-tree array positions are MCIDs.
                        value.setArrayItem(index, QPDFObjectHandle::newNull());
                      }
                  }
              }
          }
      }

    auto id_tree = root.getKey("/IDTree");
    if(id_tree.isDictionary())
      {
        QPDFNameTreeObjectHelper ids(id_tree, qpdf);
        for(const auto& [key, entry] : ids.getAsMap())
          {
            if(cleanup.was_pruned(entry))
              {
                ids.remove(key);
              }
          }
      }
  }
}

#endif
