//-*-C++-*-

#ifndef QPDF_UTILS_H
#define QPDF_UTILS_H

#include <sstream>
#include <iostream>
#include <iomanip>
#include <set>
#include <limits>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <qpdf/QPDF.hh>
#include <qpdf/QUtil.hh>

namespace pdflib
{
  // A name object comes out of qpdf with its #xx escapes resolved, as raw
  // bytes (ISO 32000-1, 7.3.5). Names meant for display should be UTF-8,
  // but forms written by older tools spell non-ASCII option names in
  // PDFDocEncoding, as in /zur#fcck for "zurück". Keep valid UTF-8 as it is
  // and transcode the rest the way getUTF8Value() transcodes text strings,
  // so that every name handed to Python decodes instead of raising.
  std::string name_to_utf8(QPDFObjectHandle name)
  {
    std::string raw = name.getName();
    if(utf8::is_valid(raw.begin(), raw.end()))
      {
        return raw;
      }
    return QUtil::pdf_doc_to_utf8(raw);
  }

  std::pair<bool, std::string> to_string(QPDFObjectHandle obj,
					 const std::string& key)
  {
    std::pair<bool, std::string> result(false, "");

    if(obj.hasKey(key) and obj.getKey(key).isString())
      {
	result.first = true;
	result.second = obj.getKey(key).getUTF8Value();
      }
    else if(obj.hasKey(key) and obj.getKey(key).isName())
      {
	result.first = true;
	result.second = name_to_utf8(obj.getKey(key));
      }

    return result;
  }

  // Field attributes such as /FT, /V and /T are inheritable through the
  // /Parent chain of AcroForm field dictionaries (PDF 32000-1, 12.7.3.1):
  // widget annotations of a radio-button group or of split fields carry
  // these keys on their parent field, not on the annotation itself.
  std::pair<bool, std::string> to_inherited_string(QPDFObjectHandle obj,
						   const std::string& key,
						   int max_depth=32)
  {
    for(int depth=0; depth<max_depth; depth++)
      {
	auto result = to_string(obj, key);
	if(result.first)
	  {
	    return result;
	  }

	if(not obj.hasKey("/Parent"))
	  {
	    break;
	  }

	obj = obj.getKey("/Parent");
	if(not obj.isDictionary())
	  {
	    break;
	  }
      }

    return {false, ""};
  }

  nlohmann::json to_json_impl(QPDFObjectHandle obj,
                              std::set<QPDFObjGen>& path,
                              int level, int max_level, std::size_t& remaining)
  {
    nlohmann::json result;
    if(remaining==0) { return "[exceeding node budget]"; }
    --remaining;

    // Attribute/annotation values may refer back into the entire document.
    if(level>0 and obj.isDictionary())
      {
        auto type = obj.getKey("/Type");
        static const std::unordered_set<std::string> document_types = {
          "/Catalog", "/Pages", "/Page", "/StructTreeRoot", "/StructElem"};
        if(type.isName() and document_types.count(type.getName()))
          {
            auto ref = obj.isIndirect() ? " '"+obj.unparse()+"'" : "";
            return "[skipping "+type.getName()+" object"+ref+"]";
          }
      }
    bool on_path = obj.isIndirect() and (obj.isDictionary() or obj.isArray());
    if(on_path)
      {
        if(not path.insert(obj.getObjGen()).second)
          {
            return "[ref to previous object: '"+obj.unparse()+"']";
          }
      }

    if(level<max_level)
      {
        const static std::unordered_set<std::string> keys_to_be_skipped = {"/Parent", "/P", "/Annots", "/B"};

        if(obj.isDictionary())
          {
            /*
              LOG_S(INFO) << "detected dict: ";
              for(auto key : obj.getKeys())
              {
              LOG_S(INFO) << " -> key: " << key;
              }
            */

            for(auto key : obj.getKeys())
              {
                if(remaining==0)
                  {
                    result[key] = "[exceeding node budget]";
                    break;
                  }
                //LOG_S(INFO) << "key: " << key;

                if(keys_to_be_skipped.count(key)==1)
                  {
                    result[key] = "[skipping " + key + "]";
                  }
                else
                  {
                    result[key] = to_json_impl(obj.getKey(key), path, level+1, max_level, remaining);
                  }
              }
          }
        else if(obj.isArray())
          {
            //LOG_S(INFO) << "array: " << obj.getArrayNItems();
            for(int l=0; l<obj.getArrayNItems(); l++)
              {
                QPDFObjectHandle new_obj = obj.getArrayItem(l);

                result.push_back(to_json_impl(new_obj, path, level+1, max_level, remaining));
                if(remaining==0)
                  {
                    if(l+1<obj.getArrayNItems()) { result.push_back("[exceeding node budget]"); }
                    break;
                  }
              }
          }
        else if(obj.isStream())
          {
	    /*
	    std::string val = "";
	    
	    try
	      {
		// Convert raw data to std::string
		std::shared_ptr<Buffer> ptr = obj.getStreamData(qpdf_dl_all);
		val = std::string(reinterpret_cast<const char*>(ptr->getBuffer()), ptr->getSize());
	      }
	    catch(const std::exception& exc)
	      {
		LOG_S(ERROR) << "could not stream data, " << exc.what();
		val = obj.unparse()+" [stream]";
	      }
	    */
	    
	    std::string val = obj.unparse()+" [stream]";
	    
            if(utf8::is_valid(val.begin(), val.end()))
              {
                result = val;
              }
            else
              {
                LOG_S(WARNING) << "val is not utf8: " << val;
              }
          }
        else if(obj.isName())
          {
            std::string val = obj.getName();

            if(utf8::is_valid(val.begin(), val.end()))
              {
                result = val;
              }
            else
              {
                LOG_S(WARNING) << "val is not utf8: " << val;

                std::string tmp;
                utf8::replace_invalid(val.begin(), val.end(),
                                      std::back_inserter(tmp));

                LOG_S(WARNING) << " --> " << tmp;

                result = tmp;
              }
          }
        else if(obj.isString())
          {
            std::string val = obj.getUTF8Value();

            if(utf8::is_valid(val.begin(), val.end()))
              {
                result = val;
              }
            else
              {
                LOG_S(WARNING) << "val is not utf8: " << val;

                utf8::replace_invalid(val.begin(), val.end(),
                                      std::back_inserter(val));

                LOG_S(WARNING) << " --> " << val;

                result = val;
              }
          }
        else if(obj.isInteger())
          {
            int val = obj.getIntValue();
            result = val;
          }
        else if(obj.isReal())
          {
            double val = utils::numeric::locale_safe_numeric_value(obj);
            result = val;
          }
        else if(obj.isBool())
          {
            bool val = obj.getBoolValue();
            result = val;
          }
        else
          {
            std::string val = obj.unparse() + " ["+obj.getTypeName()+"]";
            LOG_S(INFO) << "unidentified value: " << val;

            if(utf8::is_valid(val.begin(), val.end()))
              {
                result = val;
              }
            else
              {
                LOG_S(WARNING) << "val is not utf8: " << val;

                utf8::replace_invalid(val.begin(), val.end(),
                                      std::back_inserter(val));

                LOG_S(WARNING) << " --> " << val;

                result = val;
              }
          }
      }
    else
      {
        LOG_S(WARNING) << __FUNCTION__ << "\t level=" << level << "\t" << obj.unparse();
        result = "[exceeding recursion]";
      }

    if(on_path) { path.erase(obj.getObjGen()); }
    return result;
  }

  nlohmann::json to_json(QPDFObjectHandle obj, int level=0, int max_level=32,
                        std::size_t max_nodes=std::numeric_limits<std::size_t>::max())
  {
    std::set<QPDFObjGen> path;
    return to_json_impl(obj, path, level, max_level, max_nodes);
  }

  void print_obj(QPDFObjectHandle obj, int level = 0)
  {
    if(obj.isDictionary())
      {
        for(auto key : obj.getKeys())
          {
            std::cout << std::setw(level * 4) << " " << key << ": ";
            if(key=="/Parent")
              {
                std::cout << " [skipping] \n";
                continue;
              }

            if(obj.getKey(key).isStream())
              {
                std::cout << "Stream" << std::endl;
                print_obj(obj.getKey(key).getDict(), level + 1);
              }
            else if(obj.getKey(key).isDictionary())
              {
                std::cout << std::endl;
                print_obj(obj.getKey(key), level + 1);
              }
            else
              {
                print_obj(obj.getKey(key), 0);
              }
            /*
              else if(obj.getKey(key).isInteger())
              {
              obj.getKey(key).getObj(), level + 1);
              }
              else
              {
              std::cout << "[" << obj.getKey(key).getTypeName() << "] --> "
              << obj.getKey(key).unparse() << std::endl;
              }
            */
          }
      }
    else if(obj.isArray())
      {
        std::cout << std::setw(level * 4) << "[\n";
        for(int l=0; l<obj.getArrayNItems(); l++)
          {
            QPDFObjectHandle new_obj = obj.getArrayItem(l);
            //std::cout << std::setw(level * 4) << "[" << new_obj.getTypeName() << "]\t";

            print_obj(new_obj, level+1);
          }
        std::cout << std::setw(level * 4) << "]\n";
      }
    else
      {
        //std::cout << std::setw(level * 4);
        for(int l=0; l<level*4; l++)
          std::cout << " ";

        if(obj.isName())
          {
            std::cout << obj.getName() << std::endl;
          }
        else if(obj.isString())
          {
            std::cout << obj.getUTF8Value() << std::endl;
          }
        else if(obj.isInteger())
          {
            std::cout << obj.getIntValue() << std::endl;
          }
        else
          {
            std::cout << "[" << obj.getTypeName() << "] --> "
                      << obj.unparse() << std::endl;
          }
      }

  }

}

#endif
