//-*-C++-*-

#ifndef PDF_OBJECT_JSON_H
#define PDF_OBJECT_JSON_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#define POINTERHOLDER_TRANSITION 0 // eliminate warnings from QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjGen.hh>
#include <qpdf/QPDFObjectHandle.hh>

namespace pdflib
{
  // A detached, bounded JSON view of a PDF object, for inspection results
  // whose shape the file decides (annotations, viewer preferences). Names
  // keep their leading slash, strings are UTF-8, a stream is shown as its
  // dictionary. The walk stops at `depth` levels and never follows the same
  // indirect object twice on one path, so cyclic graphs terminate.
  class pdf_object_json
  {
  public:

    static nlohmann::json to_json(QPDFObjectHandle object, int depth);

    static nlohmann::json to_json(QPDFObjectHandle object,
                                  int depth,
                                  const std::set<std::string>& skipped_keys);

    // Zero-based index of every page of `qpdf`, by the page object's id.
    static std::map<QPDFObjGen, int> page_indices(QPDF& qpdf);

    // The page a destination array [page /Kind ...] points at, or -1.
    static int destination_page(QPDFObjectHandle destination,
                                const std::map<QPDFObjGen, int>& indices);

    // {"page": index or null, "kind": "/XYZ", "arguments": [...]}, or null
    // when `destination` is not a destination array.
    static nlohmann::json destination_to_json(QPDFObjectHandle destination,
                                              const std::map<QPDFObjGen, int>& indices);

  private:

    static nlohmann::json walk(QPDFObjectHandle object,
                               int depth,
                               const std::set<std::string>& skipped_keys,
                               std::set<QPDFObjGen>& path);
  };

  inline nlohmann::json pdf_object_json::to_json(QPDFObjectHandle object, int depth)
  {
    std::set<std::string> skipped_keys;
    return to_json(object, depth, skipped_keys);
  }

  inline nlohmann::json pdf_object_json::to_json(QPDFObjectHandle object,
                                                 int depth,
                                                 const std::set<std::string>& skipped_keys)
  {
    std::set<QPDFObjGen> path;
    return walk(object, depth, skipped_keys, path);
  }

  inline nlohmann::json pdf_object_json::walk(QPDFObjectHandle object,
                                              int depth,
                                              const std::set<std::string>& skipped_keys,
                                              std::set<QPDFObjGen>& path)
  {
    if(object.isNull())    { return nullptr; }
    if(object.isBool())    { return object.getBoolValue(); }
    if(object.isInteger()) { return object.getIntValue(); }
    if(object.isReal())    { return object.getNumericValue(); }
    if(object.isName())    { return object.getName(); }
    if(object.isString())  { return object.getUTF8Value(); }

    bool is_container = (object.isArray() or object.isDictionary() or object.isStream());
    if(not is_container)
      {
        return object.unparse();
      }

    // An indirect container already on the path is a cycle (/Parent, /P ...).
    QPDFObjGen identity = object.getObjGen();
    bool tracked = (object.isIndirect() and identity.getObj() != 0);
    if(depth <= 0 or (tracked and path.count(identity) > 0))
      {
        return object.isArray() ? nlohmann::json("[...]") : nlohmann::json("<<...>>");
      }

    if(tracked)
      {
        path.insert(identity);
      }

    nlohmann::json result;
    if(object.isArray())
      {
        result = nlohmann::json::array();
        for(QPDFObjectHandle& item : object.getArrayAsVector())
          {
            result.push_back(walk(item, depth - 1, skipped_keys, path));
          }
      }
    else
      {
        QPDFObjectHandle dictionary = object.isStream() ? object.getDict() : object;

        result = nlohmann::json::object();
        for(const std::string& key : dictionary.getKeys())
          {
            if(skipped_keys.count(key) > 0)
              {
                continue;
              }
            result[key] = walk(dictionary.getKey(key), depth - 1, skipped_keys, path);
          }
      }

    if(tracked)
      {
        path.erase(identity);
      }

    return result;
  }

  inline std::map<QPDFObjGen, int> pdf_object_json::page_indices(QPDF& qpdf)
  {
    std::map<QPDFObjGen, int> indices;

    int index = 0;
    for(const QPDFObjectHandle& page : qpdf.getAllPages())
      {
        indices[page.getObjGen()] = index;
        index += 1;
      }

    return indices;
  }

  inline int pdf_object_json::destination_page(QPDFObjectHandle destination,
                                               const std::map<QPDFObjGen, int>& indices)
  {
    if(not destination.isArray() or destination.getArrayNItems() == 0)
      {
        return -1;
      }

    QPDFObjectHandle page = destination.getArrayItem(0);
    if(not page.isIndirect())
      {
        return -1;
      }

    auto itr = indices.find(page.getObjGen());
    return itr == indices.end() ? -1 : itr->second;
  }

  inline nlohmann::json pdf_object_json::destination_to_json(QPDFObjectHandle destination,
                                                             const std::map<QPDFObjGen, int>& indices)
  {
    // A named destination's value is the array itself or a dictionary
    // holding it under /D (ISO 32000-1, 12.3.2.3).
    if(destination.isDictionary() and destination.hasKey("/D"))
      {
        destination = destination.getKey("/D");
      }

    if(not destination.isArray() or destination.getArrayNItems() == 0)
      {
        return nullptr;
      }

    nlohmann::json result = nlohmann::json::object();

    int page = destination_page(destination, indices);
    result["page"] = (page < 0) ? nlohmann::json(nullptr) : nlohmann::json(page);

    result["kind"] = nullptr;
    result["arguments"] = nlohmann::json::array();

    int number_of_items = destination.getArrayNItems();
    if(number_of_items > 1 and destination.getArrayItem(1).isName())
      {
        result["kind"] = destination.getArrayItem(1).getName();
      }
    for(int index = 2; index < number_of_items; index++)
      {
        result["arguments"].push_back(to_json(destination.getArrayItem(index), 1));
      }

    return result;
  }

}

#endif
