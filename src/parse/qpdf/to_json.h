//-*-C++-*-

#ifndef QPDF_UTILS_H
#define QPDF_UTILS_H

#include <sstream>
#include <iostream>
#include <iomanip>

#include <nlohmann/json.hpp>
#include <qpdf/QPDF.hh>

namespace pdflib
{
  // FIXME: add a begin time to cap the max time spent in this routine
  nlohmann::json to_json(QPDFObjectHandle obj, std::set<std::string> prev_objs={},
                         int level=0, int max_level=32)
  {
    nlohmann::json result;

    if(obj.isDictionary() or obj.isArray())
      {
        std::string this_obj = obj.unparse();

        if(not utf8::is_valid(this_obj.begin(), this_obj.end()))
          {
            utf8::replace_invalid(this_obj.begin(), this_obj.end(),
                                  std::back_inserter(this_obj));
            LOG_S(WARNING) << "fixed already encountered object: " << this_obj;
          }

        if(prev_objs.count(this_obj)==1)
          {
            result = "[ref to previous object: '"+this_obj+"']";
            return result;
          }
        else
          {
            prev_objs.insert(this_obj);
          }
      }

    if(level<max_level)
      {
        const static std::set<std::string> keys_to_be_skipped = {"/Parent", "/P", "/Annots", "/B"};

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
                //LOG_S(INFO) << "key: " << key;

                if(keys_to_be_skipped.count(key)==1)
                  {
                    result[key] = "[skipping " + key + "]";
                  }
                else
                  {
                    result[key] = to_json(obj.getKey(key), prev_objs, level+1);
                  }
              }
          }
        else if(obj.isArray())
          {
            //LOG_S(INFO) << "array: " << obj.getArrayNItems();
            for(int l=0; l<obj.getArrayNItems(); l++)
              {
                QPDFObjectHandle new_obj = obj.getArrayItem(l);

                auto item = to_json(new_obj, prev_objs, level+1);
                result.push_back(item);
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
            double val = obj.getNumericValue();
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

    return result;
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
