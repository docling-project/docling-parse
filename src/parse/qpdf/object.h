//-*-C++-*-

#ifndef QPDF_OBJECT_ACCESS_H
#define QPDF_OBJECT_ACCESS_H

#include <array>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <utf8.h>
#include <qpdf/QPDFObjectHandle.hh>

#include <parse/utils/numeric.h>

namespace pdflib
{
  namespace qpdf_object
  {

    inline QPDFObjectHandle get_path(QPDFObjectHandle obj,
                                     const std::vector<std::string>& keys)
    {
      for(const auto& key : keys)
        {
          if(obj.isStream())
            {
              obj = obj.getDict();
            }

          if(not obj.isDictionary() or not obj.hasKey(key))
            {
              return QPDFObjectHandle::newNull();
            }

          obj = obj.getKey(key);
        }

      return obj;
    }

    inline bool has_path(QPDFObjectHandle obj,
                         const std::vector<std::string>& keys)
    {
      return not get_path(obj, keys).isNull();
    }

    inline std::string debug(QPDFObjectHandle obj)
    {
      if(obj.isNull())
        {
          return "null";
        }

      try
        {
          std::string result = obj.unparse();
          constexpr std::size_t max_size = 512;
          if(result.size() > max_size)
            {
              result.resize(max_size);
              result += "...";
            }
          return result;
        }
      catch(const std::exception& exc)
        {
          return std::string("<") + obj.getTypeName() + ": " + exc.what() + ">";
        }
    }

    inline std::string sanitize_utf8(const std::string& val)
    {
      if(utf8::is_valid(val.begin(), val.end()))
        {
          return val;
        }

      std::string result;
      utf8::replace_invalid(val.begin(), val.end(), std::back_inserter(result));
      return result;
    }

    inline bool get_name_or_string(QPDFObjectHandle obj, std::string& result)
    {
      if(obj.isName())
        {
          result = sanitize_utf8(obj.getName());
          return true;
        }
      if(obj.isString())
        {
          result = sanitize_utf8(obj.getUTF8Value());
          return true;
        }

      return false;
    }

    inline bool get_name_or_string(QPDFObjectHandle obj,
                                   const std::vector<std::string>& keys,
                                   std::string& result)
    {
      return get_name_or_string(get_path(obj, keys), result);
    }

    inline bool get_int(QPDFObjectHandle obj, int& result)
    {
      if(obj.isInteger())
        {
          result = obj.getIntValue();
          return true;
        }

      return false;
    }

    inline bool get_int(QPDFObjectHandle obj,
                        const std::vector<std::string>& keys,
                        int& result)
    {
      return get_int(get_path(obj, keys), result);
    }

    inline bool get_number(QPDFObjectHandle obj, double& result)
    {
      if(obj.isNumber())
        {
          result = utils::numeric::locale_safe_numeric_value(obj);
          return true;
        }

      return false;
    }

    inline bool get_number(QPDFObjectHandle obj,
                           const std::vector<std::string>& keys,
                           double& result)
    {
      return get_number(get_path(obj, keys), result);
    }

    inline std::vector<double> get_number_array(QPDFObjectHandle obj)
    {
      std::vector<double> result;
      if(not obj.isArray())
        {
          return result;
        }

      for(int i=0; i<obj.getArrayNItems(); i++)
        {
          double value = 0.0;
          QPDFObjectHandle item = obj.getArrayItem(i);
          if(get_number(item, value))
            {
              result.push_back(value);
            }
        }

      return result;
    }

    inline std::vector<double> get_number_array(QPDFObjectHandle obj,
                                                const std::vector<std::string>& keys)
    {
      return get_number_array(get_path(obj, keys));
    }

    template<std::size_t N>
    inline bool get_number_array(QPDFObjectHandle obj,
                                 std::array<double, N>& result)
    {
      if(not obj.isArray() or obj.getArrayNItems() != static_cast<int>(N))
        {
          return false;
        }

      for(std::size_t i=0; i<N; i++)
        {
          QPDFObjectHandle item = obj.getArrayItem(static_cast<int>(i));
          if(not get_number(item, result[i]))
            {
              return false;
            }
        }

      return true;
    }

    template<std::size_t N>
    inline bool get_number_array(QPDFObjectHandle obj,
                                 const std::vector<std::string>& keys,
                                 std::array<double, N>& result)
    {
      return get_number_array(get_path(obj, keys), result);
    }

  }
}

#endif
