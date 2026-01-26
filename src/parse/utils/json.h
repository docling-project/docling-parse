//-*-C++-*-

#ifndef PDF_UTILS_JSON_H
#define PDF_UTILS_JSON_H

#include <nlohmann/json.hpp>

namespace utils
{
  namespace json
  {
    bool has(std::vector<std::string> keys,
             nlohmann::json obj)
    {
      if(keys.size()==0)
        {
          return false;
        }
      else if(keys.size()==1 and obj.is_object())
        {
          return obj.count(keys.front());
        }
      else if(keys.size()>1 and obj.is_object() and obj.count(keys[0])==1)
        {
          std::string key = keys.front();
          keys.erase(keys.begin());

          return has(keys, obj[key]);
        }
      else
        {
          return false;
        }
    }

    nlohmann::json get(std::vector<std::string> keys,
                       nlohmann::json obj)
    {
      nlohmann::json result;

      if(keys.size()==0)
        {
          return result;
        }
      else if(keys.size()==1 and obj.is_object())
        {
          return obj[keys.front()];
        }
      else if(keys.size()>1 and obj.is_object() and obj.count(keys[0])==1)
        {
          std::string key = keys.front();
          keys.erase(keys.begin());

          return get(keys, obj[key]);
        }
      else
        {
          return result;
        }
    }
  }
}

#endif
