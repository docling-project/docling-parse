//-*-C++-*-

#ifndef PDF_UTILS_STRING_H
#define PDF_UTILS_STRING_H

namespace utils
{
  namespace string
  {
    bool is_valid_utf8(const std::string& val)
    {
      return utf8::is_valid(val.begin(), val.end());
    }

    std::string fix_into_valid_utf8(const std::string& val)
    {
      std::string res;
      if (!is_valid_utf8(val))
	{
	  utf8::replace_invalid(val.begin(), val.end(), std::back_inserter(res));
	}
      else
	{
	  res = val; // Copy the original string if it's already valid
	}
      
    return res;
    }

    int count_unicode_characters(const std::string& utf8_string)
    {
      int count = 0;

      std::size_t i = 0;
      while (i < utf8_string.size())
	{
	  unsigned char c = static_cast<unsigned char>(utf8_string[i]);

	  // Check the number of leading 1 bits in the byte
	  if ((c & 0x80) == 0) { 
	    // 1-byte character (ASCII)
            i += 1;
	  } else if ((c & 0xE0) == 0xC0) { 
            // 2-byte character
            i += 2;
	  } else if ((c & 0xF0) == 0xE0) { 
            // 3-byte character
            i += 3;
	  } else if ((c & 0xF8) == 0xF0) { 
            // 4-byte character
            i += 4;
	  } else {
            // Invalid UTF-8 byte
	    return -1;
	  }
	  
	  ++count; // Increment character count
	}

      return count;
    }
    
    bool is_integer(const std::string & s)
    {
      return std::regex_match(s, std::regex("(-)?[0-9]+"));
    }

    bool is_number(const std::string & s)
    {
      return std::regex_match(s, std::regex("(-)?[0-9]+(\\.[0-9]+)?"));
    }

    std::string to_lower(std::string text)
    {
      // Convert the string to lowercase
      std::transform(text.begin(), text.end(), text.begin(),
		     [](unsigned char c) { return std::tolower(c); });

      return text;
    }
    
    std::string strip(std::string& line)
    {
      std::string result = line;

      while(line.size()>0 and line.back()==' ')
        line.pop_back();

      while(line.size()>0 and line.front()==' ')
        line.erase(line.begin());

      return result;
    }

    std::vector<std::string> split(std::string line,
                                   std::string delim)
    {
      std::vector<std::string> elems;

      size_t pos = 0;
      std::string token;

      while((pos = line.find(delim)) != std::string::npos)
        {
          token = line.substr(0, pos);
          elems.push_back(token);

          //std::cout << token << std::endl;
          line.erase(0, pos + delim.length());
        }

      //std::cout << s << std::endl;
      elems.push_back(line);

      return elems;
    }

    uint32_t hex_to_numb(std::string hex)
    {
      return std::stoul(hex, NULL, 16);
    }

    std::vector<uint32_t> hex_to_vec(std::string hex_str, int num_char)
    {
      assert(hex_str.size()%num_char==0);

      std::vector<uint32_t> vec;
      for(int l=0; l<hex_str.size(); l+=num_char)
        {
          std::string hex = hex_str.substr(l, num_char);
          uint32_t    ind = hex_to_numb(hex);//std::stoul(hex, NULL, 16);

          vec.push_back(ind);
        }

      return vec;
    }

    std::string vec_to_utf8(std::vector<uint32_t>& vec)
    {
      size_t      len = 64+8*vec.size();
      std::string str(len, ' ');

      auto itr = utf8::utf16to8(vec.begin(), vec.end(), str.begin());
      str.erase(itr, str.end());

      return str;
    }

    std::string vec_to_utf8(std::vector<std::string>& vec)
    {
      std::vector<uint32_t> vec_={};

      for(auto item:vec)
	{
	  vec_.push_back(hex_to_numb(item));
	}

      return vec_to_utf8(vec_);
    }

    std::string hex_to_utf8(std::string hex_str, int num_char)
    {
      /*
        assert(hex_str.size()%4==0);

        std::vector<uint32_t> vec;
        for(int l=0; l<hex_str.size(); l+=4)
        {
        std::string hex = hex_str.substr(l, 4);
        uint32_t    ind = std::stoul(hex, NULL, 16);

        vec.push_back(ind);
        }
      */
      std::vector<uint32_t> vec = hex_to_vec(hex_str, num_char);

      return vec_to_utf8(vec);
    }

    std::string replace(std::string& text, const std::string& word_0, const std::string& word_1)
    {
      if(word_0==word_1)
	{
	  return text;
	}

      std::size_t pos=0;
      while(pos<text.size())
	{			          
	  pos = text.find(word_0, pos);
	  if(pos==std::string::npos)
	    {
	      break;
	    }

	  text.replace(pos, word_0.size(), word_1);
	  pos += word_1.size();
	}
      
      return text;
    }

    bool is_space(char32_t c)
    {
      static const std::unordered_set<char32_t> unicode_spaces = {
        0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x0020, 0x0085, 0x00A0,
        0x1680, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006,
        0x2007, 0x2008, 0x2009, 0x200A, 0x2028, 0x2029, 0x202F, 0x205F, 0x3000
      };
      return unicode_spaces.count(c) > 0;
    }
    
    bool is_space(const std::string& val)
    {
      if(val.size()==0)
	{
	  return false;
	}
      
      if(utf8::is_valid(val.begin(), val.end()))
	{
	  std::vector<uint32_t> utf32_chars={};
	  utf8::utf8to32(val.begin(), val.end(), std::back_inserter(utf32_chars));
	  
	  if(utf32_chars.size()==1)
	    {
	      return is_space(utf32_chars.at(0));
	    }
	  else
	    {
	      return false;
	    }
	}
      
      return false;      
    }
    
    // Function to check if a Unicode character is punctuation or whitespace
    bool is_punctuation_or_space(char32_t ch)
    {
      static const std::unordered_set<char32_t> punctuationAndSpaces = {
        U' ', U'\t', U'\n', U'\r', U'\f', U'\v',  // Whitespace characters
        U'.', U',', U';', U':', U'!', U'?',       // Basic punctuation
        U'(', U')', U'[', U']', U'{', U'}',       // Brackets
        U'\'', U'\"', U'`', U'‘', U'’', U'“', U'”', // Quotes
        U'-', U'–', U'—', U'_',                  // Hyphens and dashes
        U'/', U'\\', U'|', U'@', U'#', U'%', U'&', U'*', U'+', U'=', U'<', U'>'
      };
      
      // Check if character is in the punctuation set
      if(punctuationAndSpaces.count(ch))
	{
	  return true;
	}
      
      // Additional Unicode punctuation ranges
      return (ch >= 0x2000 && ch <= 0x206F) ||  // General Punctuation block
	(ch >= 0x3000 && ch <= 0x303F) ||  // CJK Symbols and Punctuation
	(ch >= 0xFE50 && ch <= 0xFE6F) ||  // Small Form Variants
	(ch >= 0xFF00 && ch <= 0xFF0F) ||  // Fullwidth punctuation
	(ch >= 0xFF1A && ch <= 0xFF1F) ||  // More fullwidth punctuation
	(ch >= 0xFF3B && ch <= 0xFF5E);    // Fullwidth brackets and symbols
    }

    bool is_punctuation_or_space(const std::string& val)
    {
      if(val.size()==0)
	{
	  return false;
	}
      
      if(utf8::is_valid(val.begin(), val.end()))
	{
	  std::vector<uint32_t> utf32_chars={};
	  utf8::utf8to32(val.begin(), val.end(), std::back_inserter(utf32_chars));
	  
	  if(utf32_chars.size()==1)
	    {
	      return is_punctuation_or_space(utf32_chars.at(0));
	    }
	  else
	    {
	      return false;
	    }
	}
      
      return false;
    }
    
    // Function to check if a Unicode character belongs to an RTL script
    bool is_rtl_char(uint32_t ch)
    {
      return 
        // Arabic script
        (ch >= 0x0600 && ch <= 0x06FF) ||  // Arabic
        (ch >= 0x0750 && ch <= 0x077F) ||  // Arabic Supplement
        (ch >= 0x08A0 && ch <= 0x08FF) ||  // Arabic Extended-A
        (ch >= 0xFB50 && ch <= 0xFDFF) ||  // Arabic Presentation Forms-A
        (ch >= 0xFE70 && ch <= 0xFEFF) ||  // Arabic Presentation Forms-B

        // Hebrew script
        (ch >= 0x0590 && ch <= 0x05FF) ||  // Hebrew
        (ch >= 0xFB1D && ch <= 0xFB4F) ||  // Hebrew Presentation Forms

        // Syriac script
        (ch >= 0x0700 && ch <= 0x074F) ||  // Syriac

        // Thaana script
        (ch >= 0x0780 && ch <= 0x07BF) ||  // Thaana

        // N'Ko script
        (ch >= 0x07C0 && ch <= 0x07FF);    // N’Ko
    }
    
    bool is_right_to_left(const std::string& val)
    {
      if(val.size()==0)
	{
	  return false;
	}
      
      if(utf8::is_valid(val.begin(), val.end()))
	{
	  std::vector<uint32_t> utf32_chars = {};
	  utf8::utf8to32(val.begin(), val.end(), std::back_inserter(utf32_chars));

	  bool result = true;
	  for(auto _:utf32_chars)
	    {
	      result = (result and is_rtl_char(_));
	    }

	  return result;
	}

      return false;
    }
    
  }

}

#endif
