//-*-C++-*-

#ifndef PDF_OUTLINE_OPS_H
#define PDF_OUTLINE_OPS_H

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#define POINTERHOLDER_TRANSITION 0 // eliminate warnings from QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>
#include <qpdf/QPDFOutlineObjectHelper.hh>
#include <qpdf/QUtil.hh>

#include <edit/catalog_ops.h>
#include <edit/edit_error.h>
#include <edit/object_json.h>

namespace pdflib
{
  // The document outline (bookmarks; ISO 32000-1, 12.3.3). qpdf reads
  // outlines but has no way to create or change them, so the tree is built
  // and repaired here, on the outline dictionaries themselves.
  //
  // An outline item is identified to callers by the object number of its
  // dictionary; 0 stands for the outline root.
  class pdf_outline_ops
  {
  public:

    // [{"id", "title", "page": index or null, "url": string or null,
    //   "open": bool, "children": [...]}, ...]
    static nlohmann::json read(QPDF& qpdf);

    // Add an item as the last child of `parent` (0: top level) that opens
    // `page` fitted to the window; page -1 makes an item without a
    // destination. Returns the new item's id.
    static int add_item(QPDF& qpdf, const std::string& title, int page, int parent);

    // Add an item described by `spec`: "title" (required), "parent" (id, 0 by
    // default) and the entries update_item takes. Returns the new item's id.
    static int add_item(QPDF& qpdf, const nlohmann::json& spec);

    // Change an item. Entries of `changes`, all optional:
    //
    //   "title"              text
    //   "page"               index, or null for no target; with it "left",
    //                        "top" and "zoom" (numbers or null) choose a
    //                        position and magnification instead of fitting
    //                        the page to the window
    //   "url"                a URL to open instead of a page, or null
    //   "color"              [red, green, blue] in 0..1, or null for default
    //   "bold", "italic"     booleans
    //   "open"               whether the item's children are shown
    static void update_item(QPDF& qpdf, int id, const nlohmann::json& changes);

    // Remove an item and everything below it.
    static void remove_item(QPDF& qpdf, int id);

    // Make an item the child of `parent` (0: top level) at position `index`
    // among its siblings; a negative index puts it last.
    static void move_item(QPDF& qpdf, int id, int parent, int index);

    // Remove the whole outline.
    static void clear(QPDF& qpdf);

    // Recreate the outline of `source` in `destination` for the pages that
    // were copied across: `page_map` gives, for a page of the source, the
    // index it has in the destination. An item is kept when its own page or
    // the page of one of its descendants was copied. Returns the number of
    // items created under `parent`.
    static int import(QPDF& destination,
                      QPDF& source,
                      const std::map<QPDFObjGen, int>& page_map,
                      int parent);

    // Remove every outline destination, and the open action, that points at
    // this page: it is about to leave the page tree, and a destination that
    // refers to it would keep the page alive in the file and lead nowhere.
    static void drop_destinations_to(QPDF& qpdf, QPDFObjGen page);

  private:

    static const int MAX_DEPTH = 64;

    static nlohmann::json read_items(std::vector<QPDFOutlineObjectHelper> items,
                                     const std::map<QPDFObjGen, int>& indices,
                                     int depth);

    static QPDFObjectHandle outline_root(QPDF& qpdf);
    static QPDFObjectHandle resolve_parent(QPDF& qpdf, int parent);
    static QPDFObjectHandle resolve_item(QPDF& qpdf, int id);

    // The sibling list of `parent`: take `item` out of it, or put it in at
    // `index` (negative: last).
    static void unlink(QPDFObjectHandle item);
    static void link(QPDFObjectHandle parent, QPDFObjectHandle item, int index);

    // Rewrite every /Count of the outline from its structure. An item keeps
    // being open or closed; `closed` adds items to close, `opened` items to
    // open. Returns the number of descendants visible below `item`.
    static long long recount(QPDFObjectHandle item,
                             bool is_root,
                             const std::set<QPDFObjGen>& closed,
                             const std::set<QPDFObjGen>& opened,
                             std::set<QPDFObjGen>& visited,
                             int depth);
    static void recount(QPDF& qpdf);

    static void apply(QPDF& qpdf,
                      QPDFObjectHandle item,
                      const nlohmann::json& changes,
                      std::set<QPDFObjGen>& closed,
                      std::set<QPDFObjGen>& opened);

    static std::optional<double> number_or_null(const nlohmann::json& changes, const char* key);

    static int import_items(QPDF& destination,
                            std::vector<QPDFOutlineObjectHelper> items,
                            const std::map<QPDFObjGen, int>& page_map,
                            int parent,
                            int depth);

    static bool has_copied_page(QPDFOutlineObjectHelper& item,
                                const std::map<QPDFObjGen, int>& page_map,
                                int depth);

    static void drop_in_items(QPDFObjectHandle first,
                              QPDFObjGen page,
                              std::set<QPDFObjGen>& visited,
                              int depth);

    static bool points_at(QPDFObjectHandle destination, QPDFObjGen page);
  };

  inline nlohmann::json pdf_outline_ops::read(QPDF& qpdf)
  {
    QPDFOutlineDocumentHelper helper(qpdf);
    if(not helper.hasOutlines())
      {
        return nlohmann::json::array();
      }

    return read_items(helper.getTopLevelOutlines(), pdf_object_json::page_indices(qpdf), 0);
  }

  inline nlohmann::json pdf_outline_ops::read_items(std::vector<QPDFOutlineObjectHelper> items,
                                                    const std::map<QPDFObjGen, int>& indices,
                                                    int depth)
  {
    nlohmann::json result = nlohmann::json::array();

    for(QPDFOutlineObjectHelper& item : items)
      {
        nlohmann::json entry = nlohmann::json::object();

        entry["id"] = item.getObjectHandle().getObjectID();
        entry["title"] = item.getTitle();

        // getDestPage resolves /Dest, a /GoTo action and named destinations.
        entry["page"] = nullptr;
        QPDFObjectHandle page = item.getDestPage();
        if(page.isIndirect())
          {
            auto itr = indices.find(page.getObjGen());
            if(itr != indices.end())
              {
                entry["page"] = itr->second;
              }
          }

        QPDFObjectHandle dictionary = item.getObjectHandle();

        entry["url"] = nullptr;
        QPDFObjectHandle action = dictionary.getKey("/A");
        if(action.isDictionary() and
           action.getKey("/S").isNameAndEquals("/URI") and
           action.getKey("/URI").isString())
          {
            entry["url"] = action.getKey("/URI").getUTF8Value();
          }

        // a negative /Count is an item whose children are hidden
        QPDFObjectHandle count = dictionary.getKey("/Count");
        entry["open"] = not (count.isInteger() and count.getIntValue() < 0);

        entry["children"] = (depth < MAX_DEPTH) ?
          read_items(item.getKids(), indices, depth + 1) : nlohmann::json::array();

        result.push_back(entry);
      }

    return result;
  }

  inline QPDFObjectHandle pdf_outline_ops::outline_root(QPDF& qpdf)
  {
    QPDFObjectHandle root = qpdf.getRoot();

    QPDFObjectHandle outlines = root.getKey("/Outlines");
    if(not outlines.isDictionary())
      {
        outlines = QPDFObjectHandle::newDictionary();
        outlines.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));
        outlines = qpdf.makeIndirectObject(outlines);
        root.replaceKey("/Outlines", outlines);
      }
    else if(not outlines.isIndirect())
      {
        // items refer to their parent, so the root has to be referable
        outlines = qpdf.makeIndirectObject(outlines);
        root.replaceKey("/Outlines", outlines);
      }

    return outlines;
  }

  inline QPDFObjectHandle pdf_outline_ops::resolve_parent(QPDF& qpdf, int parent)
  {
    if(parent == 0)
      {
        return outline_root(qpdf);
      }

    QPDFObjectHandle item = qpdf.getObjectByID(parent, 0);
    if(not (item.isDictionary() and item.hasKey("/Title")))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "outline item " + std::to_string(parent) + " does not exist");
      }

    return item;
  }

  inline QPDFObjectHandle pdf_outline_ops::resolve_item(QPDF& qpdf, int id)
  {
    QPDFObjectHandle item = (id > 0) ? qpdf.getObjectByID(id, 0) : QPDFObjectHandle::newNull();

    // an outline item has a title and hangs below a parent
    if(not (item.isDictionary() and item.hasKey("/Title") and item.getKey("/Parent").isDictionary()))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "outline item " + std::to_string(id) + " does not exist");
      }

    return item;
  }

  inline void pdf_outline_ops::unlink(QPDFObjectHandle item)
  {
    QPDFObjectHandle parent = item.getKey("/Parent");
    QPDFObjectHandle previous = item.getKey("/Prev");
    QPDFObjectHandle next = item.getKey("/Next");

    // Siblings are a doubly linked list between the parent's /First and /Last.
    if(previous.isDictionary())
      {
        if(next.isDictionary()) { previous.replaceKey("/Next", next); }
        else                    { previous.removeKey("/Next"); }
      }
    else if(parent.isDictionary())
      {
        if(next.isDictionary()) { parent.replaceKey("/First", next); }
        else                    { parent.removeKey("/First"); }
      }

    if(next.isDictionary())
      {
        if(previous.isDictionary()) { next.replaceKey("/Prev", previous); }
        else                        { next.removeKey("/Prev"); }
      }
    else if(parent.isDictionary())
      {
        if(previous.isDictionary()) { parent.replaceKey("/Last", previous); }
        else                        { parent.removeKey("/Last"); }
      }

    item.removeKey("/Prev");
    item.removeKey("/Next");
    item.removeKey("/Parent");
  }

  inline void pdf_outline_ops::link(QPDFObjectHandle parent, QPDFObjectHandle item, int index)
  {
    // The sibling the item goes in front of: the index-th, or none (last).
    QPDFObjectHandle before = QPDFObjectHandle::newNull();
    if(index >= 0)
      {
        std::set<QPDFObjGen> visited;
        before = parent.getKey("/First");
        for(int position = 0; position < index and before.isDictionary(); position++)
          {
            if(before.isIndirect() and not visited.insert(before.getObjGen()).second)
              {
                before = QPDFObjectHandle::newNull();
                break;
              }
            before = before.getKey("/Next");
          }
      }

    item.replaceKey("/Parent", parent);

    if(before.isDictionary())
      {
        QPDFObjectHandle previous = before.getKey("/Prev");

        item.replaceKey("/Next", before);
        before.replaceKey("/Prev", item);

        if(previous.isDictionary())
          {
            previous.replaceKey("/Next", item);
            item.replaceKey("/Prev", previous);
          }
        else
          {
            parent.replaceKey("/First", item);
          }
        return;
      }

    QPDFObjectHandle last = parent.getKey("/Last");
    if(last.isDictionary())
      {
        last.replaceKey("/Next", item);
        item.replaceKey("/Prev", last);
      }
    else
      {
        parent.replaceKey("/First", item);
      }
    parent.replaceKey("/Last", item);
  }

  inline long long pdf_outline_ops::recount(QPDFObjectHandle item,
                                            bool is_root,
                                            const std::set<QPDFObjGen>& closed,
                                            const std::set<QPDFObjGen>& opened,
                                            std::set<QPDFObjGen>& visited,
                                            int depth)
  {
    // /Count (ISO 32000-1, tables 152 and 153): for an open item, the number
    // of descendants a viewer shows below it; for a closed one, minus the
    // number it would show if opened; absent without children.
    long long shown = 0;

    QPDFObjectHandle kid = item.getKey("/First");
    while(kid.isDictionary() and depth < MAX_DEPTH)
      {
        if(kid.isIndirect() and not visited.insert(kid.getObjGen()).second)
          {
            break;
          }

        shown += 1 + recount(kid, false, closed, opened, visited, depth + 1);
        kid = kid.getKey("/Next");
      }

    bool is_closed = false;
    if(not is_root)
      {
        QPDFObjectHandle count = item.getKey("/Count");
        is_closed = (count.isInteger() and count.getIntValue() < 0);

        if(item.isIndirect() and closed.count(item.getObjGen()) > 0) { is_closed = true; }
        if(item.isIndirect() and opened.count(item.getObjGen()) > 0) { is_closed = false; }
      }

    if(shown == 0 and not is_root)
      {
        item.removeKey("/Count");
        return 0;
      }

    item.replaceKey("/Count", QPDFObjectHandle::newInteger(is_closed ? -shown : shown));

    // what a closed item holds is not visible further up
    return is_closed ? 0 : shown;
  }

  inline void pdf_outline_ops::recount(QPDF& qpdf)
  {
    QPDFObjectHandle outlines = qpdf.getRoot().getKey("/Outlines");
    if(not outlines.isDictionary())
      {
        return;
      }

    std::set<QPDFObjGen> none;
    std::set<QPDFObjGen> visited;
    recount(outlines, true, none, none, visited, 0);
  }

  inline std::optional<double> pdf_outline_ops::number_or_null(const nlohmann::json& changes,
                                                               const char* key)
  {
    auto itr = changes.find(key);
    if(itr == changes.end() or itr->is_null())
      {
        return std::nullopt;
      }
    if(not itr->is_number())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             std::string("outline item '") + key + "' must be a number");
      }

    return itr->get<double>();
  }

  inline void pdf_outline_ops::apply(QPDF& qpdf,
                                     QPDFObjectHandle item,
                                     const nlohmann::json& changes,
                                     std::set<QPDFObjGen>& closed,
                                     std::set<QPDFObjGen>& opened)
  {
    if(not changes.is_object())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "outline item changes must be a dictionary");
      }

    if(changes.contains("title"))
      {
        if(not changes["title"].is_string())
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT, "an outline item title must be text");
          }
        item.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(changes["title"].get<std::string>()));
      }

    bool has_url = (changes.contains("url") and not changes["url"].is_null());
    bool has_page = (changes.contains("page") and not changes["page"].is_null());

    if(has_url and has_page)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "an outline item opens a page or a URL, not both");
      }

    // Naming either one, even as null, replaces the item's target.
    if(changes.contains("url") or changes.contains("page"))
      {
        item.removeKey("/Dest");
        item.removeKey("/A");
      }

    if(has_url)
      {
        if(not changes["url"].is_string())
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT, "an outline item URL must be text");
          }

        QPDFObjectHandle action = QPDFObjectHandle::newDictionary();
        action.replaceKey("/Type", QPDFObjectHandle::newName("/Action"));
        action.replaceKey("/S", QPDFObjectHandle::newName("/URI"));
        action.replaceKey("/URI", QPDFObjectHandle::newString(changes["url"].get<std::string>()));

        item.replaceKey("/A", action);
      }
    else if(has_page)
      {
        if(not changes["page"].is_number_integer())
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT, "an outline item page must be a page index");
          }

        QPDFObjectHandle destination =
          pdf_catalog_ops::fit_destination(qpdf, changes["page"].get<int>());

        // With a position or a magnification the destination is /XYZ, whose
        // null entries mean "keep what the viewer has" (ISO 32000-1, table 151).
        bool positioned = (changes.contains("left") or changes.contains("top") or changes.contains("zoom"));
        if(positioned)
          {
            destination = QPDFObjectHandle::newArray();
            destination.appendItem(pdf_catalog_ops::fit_destination(qpdf, changes["page"].get<int>()).getArrayItem(0));
            destination.appendItem(QPDFObjectHandle::newName("/XYZ"));

            for(const char* key : {"left", "top", "zoom"})
              {
                std::optional<double> value = number_or_null(changes, key);
                destination.appendItem(value.has_value() ?
                                       QPDFObjectHandle::newReal(QUtil::double_to_string(value.value(), 4, true)) :
                                       QPDFObjectHandle::newNull());
              }
          }

        item.replaceKey("/Dest", destination);
      }

    if(changes.contains("color"))
      {
        const nlohmann::json& color = changes["color"];
        if(color.is_null())
          {
            item.removeKey("/C");
          }
        else
          {
            if(not (color.is_array() and color.size() == 3))
              {
                throw pdf_edit_error(edit_error::INVALID_EDIT,
                                     "an outline item color is three numbers between 0 and 1");
              }

            QPDFObjectHandle components = QPDFObjectHandle::newArray();
            for(const nlohmann::json& component : color)
              {
                if(not component.is_number() or component.get<double>() < 0.0 or component.get<double>() > 1.0)
                  {
                    throw pdf_edit_error(edit_error::INVALID_EDIT,
                                         "an outline item color is three numbers between 0 and 1");
                  }
                components.appendItem(QPDFObjectHandle::newReal(QUtil::double_to_string(component.get<double>(), 4, true)));
              }
            item.replaceKey("/C", components);
          }
      }

    if(changes.contains("bold") or changes.contains("italic"))
      {
        // /F: bit 1 italic, bit 2 bold (ISO 32000-1, table 154)
        long long flags = 0;
        QPDFObjectHandle existing = item.getKey("/F");
        if(existing.isInteger())
          {
            flags = existing.getIntValue();
          }

        for(const auto& style : {std::make_pair("italic", 1LL), std::make_pair("bold", 2LL)})
          {
            if(not changes.contains(style.first))
              {
                continue;
              }
            if(not changes[style.first].is_boolean())
              {
                throw pdf_edit_error(edit_error::INVALID_EDIT,
                                     std::string("outline item '") + style.first + "' must be true or false");
              }

            flags = changes[style.first].get<bool>() ? (flags | style.second) : (flags & ~style.second);
          }

        if(flags == 0) { item.removeKey("/F"); }
        else           { item.replaceKey("/F", QPDFObjectHandle::newInteger(flags)); }
      }

    if(changes.contains("open"))
      {
        if(not changes["open"].is_boolean())
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT, "outline item 'open' must be true or false");
          }

        (changes["open"].get<bool>() ? opened : closed).insert(item.getObjGen());
      }
  }

  inline int pdf_outline_ops::add_item(QPDF& qpdf, const nlohmann::json& spec)
  {
    if(not (spec.is_object() and spec.contains("title") and spec["title"].is_string()))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "an outline item needs a title");
      }

    int parent = 0;
    if(spec.contains("parent") and not spec["parent"].is_null())
      {
        if(not spec["parent"].is_number_integer())
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT, "an outline item parent is an item id");
          }
        parent = spec["parent"].get<int>();
      }

    QPDFObjectHandle parent_object = resolve_parent(qpdf, parent);

    QPDFObjectHandle item = qpdf.makeIndirectObject(QPDFObjectHandle::newDictionary());

    // Everything that can be refused is checked before the item is linked in.
    std::set<QPDFObjGen> closed;
    std::set<QPDFObjGen> opened;
    apply(qpdf, item, spec, closed, opened);

    link(parent_object, item, -1);

    std::set<QPDFObjGen> visited;
    recount(qpdf.getRoot().getKey("/Outlines"), true, closed, opened, visited, 0);

    return item.getObjectID();
  }

  inline int pdf_outline_ops::add_item(QPDF& qpdf, const std::string& title, int page, int parent)
  {
    nlohmann::json spec = nlohmann::json::object();
    spec["title"] = title;
    spec["parent"] = parent;
    if(page >= 0)
      {
        spec["page"] = page;
      }

    return add_item(qpdf, spec);
  }

  inline void pdf_outline_ops::update_item(QPDF& qpdf, int id, const nlohmann::json& changes)
  {
    QPDFObjectHandle item = resolve_item(qpdf, id);

    std::set<QPDFObjGen> closed;
    std::set<QPDFObjGen> opened;
    apply(qpdf, item, changes, closed, opened);

    std::set<QPDFObjGen> visited;
    recount(qpdf.getRoot().getKey("/Outlines"), true, closed, opened, visited, 0);
  }

  inline void pdf_outline_ops::remove_item(QPDF& qpdf, int id)
  {
    QPDFObjectHandle item = resolve_item(qpdf, id);

    // Unlinked, the item and what hangs below it are unreachable, and the
    // writer leaves unreachable objects out.
    unlink(item);
    recount(qpdf);
  }

  inline void pdf_outline_ops::move_item(QPDF& qpdf, int id, int parent, int index)
  {
    QPDFObjectHandle item = resolve_item(qpdf, id);
    QPDFObjectHandle parent_object = resolve_parent(qpdf, parent);

    // An item cannot go below itself: walk up from the new parent.
    std::set<QPDFObjGen> visited;
    QPDFObjectHandle ancestor = parent_object;
    for(int depth = 0; depth < MAX_DEPTH and ancestor.isDictionary(); depth++)
      {
        if(ancestor.isIndirect() and ancestor.getObjGen() == item.getObjGen())
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT,
                                 "an outline item cannot be moved below itself");
          }
        if(ancestor.isIndirect() and not visited.insert(ancestor.getObjGen()).second)
          {
            break;
          }
        ancestor = ancestor.getKey("/Parent");
      }

    unlink(item);
    link(parent_object, item, index);
    recount(qpdf);
  }

  inline void pdf_outline_ops::clear(QPDF& qpdf)
  {
    qpdf.getRoot().removeKey("/Outlines");
  }

  inline bool pdf_outline_ops::has_copied_page(QPDFOutlineObjectHelper& item,
                                               const std::map<QPDFObjGen, int>& page_map,
                                               int depth)
  {
    QPDFObjectHandle page = item.getDestPage();
    if(page.isIndirect() and page_map.count(page.getObjGen()) > 0)
      {
        return true;
      }

    if(depth >= MAX_DEPTH)
      {
        return false;
      }

    for(QPDFOutlineObjectHelper& kid : item.getKids())
      {
        if(has_copied_page(kid, page_map, depth + 1))
          {
            return true;
          }
      }

    return false;
  }

  inline int pdf_outline_ops::import_items(QPDF& destination,
                                           std::vector<QPDFOutlineObjectHelper> items,
                                           const std::map<QPDFObjGen, int>& page_map,
                                           int parent,
                                           int depth)
  {
    int created = 0;

    for(QPDFOutlineObjectHelper& item : items)
      {
        if(not has_copied_page(item, page_map, depth))
          {
            continue;
          }

        int page = -1;
        QPDFObjectHandle source_page = item.getDestPage();
        if(source_page.isIndirect())
          {
            auto itr = page_map.find(source_page.getObjGen());
            if(itr != page_map.end())
              {
                page = itr->second;
              }
          }

        int id = add_item(destination, item.getTitle(), page, parent);
        created += 1;

        if(depth < MAX_DEPTH)
          {
            import_items(destination, item.getKids(), page_map, id, depth + 1);
          }
      }

    return created;
  }

  inline int pdf_outline_ops::import(QPDF& destination,
                                     QPDF& source,
                                     const std::map<QPDFObjGen, int>& page_map,
                                     int parent)
  {
    QPDFOutlineDocumentHelper helper(source);
    if(not helper.hasOutlines())
      {
        return 0;
      }

    return import_items(destination, helper.getTopLevelOutlines(), page_map, parent, 0);
  }

  inline bool pdf_outline_ops::points_at(QPDFObjectHandle destination, QPDFObjGen page)
  {
    if(destination.isDictionary() and destination.hasKey("/D"))
      {
        destination = destination.getKey("/D");
      }

    if(not destination.isArray() or destination.getArrayNItems() == 0)
      {
        return false;
      }

    QPDFObjectHandle target = destination.getArrayItem(0);
    return (target.isIndirect() and target.getObjGen() == page);
  }

  inline void pdf_outline_ops::drop_in_items(QPDFObjectHandle first,
                                             QPDFObjGen page,
                                             std::set<QPDFObjGen>& visited,
                                             int depth)
  {
    QPDFObjectHandle item = first;

    while(item.isDictionary())
      {
        // a damaged list may loop back on itself
        if(item.isIndirect() and not visited.insert(item.getObjGen()).second)
          {
            break;
          }

        if(points_at(item.getKey("/Dest"), page))
          {
            item.removeKey("/Dest");
          }

        QPDFObjectHandle action = item.getKey("/A");
        if(action.isDictionary() and
           action.getKey("/S").isNameAndEquals("/GoTo") and
           points_at(action.getKey("/D"), page))
          {
            item.removeKey("/A");
          }

        if(depth < MAX_DEPTH)
          {
            drop_in_items(item.getKey("/First"), page, visited, depth + 1);
          }

        item = item.getKey("/Next");
      }
  }

  inline void pdf_outline_ops::drop_destinations_to(QPDF& qpdf, QPDFObjGen page)
  {
    QPDFObjectHandle root = qpdf.getRoot();

    QPDFObjectHandle outlines = root.getKey("/Outlines");
    if(outlines.isDictionary())
      {
        std::set<QPDFObjGen> visited;
        drop_in_items(outlines.getKey("/First"), page, visited, 0);
      }

    QPDFObjectHandle open_action = root.getKey("/OpenAction");
    if(points_at(open_action, page))
      {
        root.removeKey("/OpenAction");
      }
    else if(open_action.isDictionary() and
            open_action.getKey("/S").isNameAndEquals("/GoTo") and
            points_at(open_action.getKey("/D"), page))
      {
        root.removeKey("/OpenAction");
      }
  }

}

#endif
