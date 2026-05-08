//-*-C++-*-

#ifndef PDF_PAGE_FONT_BASE_FONT_H
#define PDF_PAGE_FONT_BASE_FONT_H

#include <unordered_map>

namespace pdflib
{

  class base_font
  {

  public:

    base_font(std::string filename_,
	      font_glyphs& glyphs_);

    base_font(const base_font& other);

    ~base_font();

    base_font& operator=(const base_font& other);

    bool has(uint32_t numb);
    bool has(const std::string& c);

    double get_width(uint32_t numb);
    double get_width(const std::string& c);
    bool has_char_bbox(const uint32_t& numb);
    bool has_char_bbox(const std::string& c);
    std::array<double, 4> get_char_bbox(const uint32_t& numb);
    std::array<double, 4> get_char_bbox(const std::string& c);

    std::string get_string(uint32_t numb);

    std::string to_utf8(uint32_t numb);

    double get_ascend();
    double get_descend();
    double get_capheight();
    double get_xheight();

    std::array<double, 4> get_font_bbox();

    //private:

    void initialise();

  private:

    std::string filename;
    font_glyphs& glyphs;

    bool initialised;

    nlohmann::json properties;
    
    std::unordered_map<uint32_t, std::string> numb_to_name;
    std::unordered_map<uint32_t, std::string> numb_to_utf8;
    std::unordered_map<std::string, uint32_t> utf8_to_numb;

    std::unordered_map<uint32_t   , double> numb_to_width;
    std::unordered_map<std::string, double> name_to_width;
    std::unordered_map<uint32_t, std::array<double, 4> > numb_to_bbox;
    std::unordered_map<std::string, std::array<double, 4> > name_to_bbox;
  };

  base_font::base_font(std::string filename_,
		       font_glyphs& glyphs_):
    filename(filename_),
    glyphs(glyphs_),
    initialised(false)
  {}

  base_font::base_font(const base_font& other):
    filename(other.filename),
    glyphs(other.glyphs),
    initialised(false)
  {}

  base_font::~base_font()
  {}

  base_font& base_font::operator=(const base_font& other)
  {
    this->filename = other.filename;
    this->glyphs = other.glyphs;

    initialised = false;

    return *this;
  }

  bool base_font::has(uint32_t numb)
  {
    initialise();

    return (numb_to_utf8.count(numb)==1);
  }

  bool base_font::has(const std::string& c)
  {
    initialise();
    return (utf8_to_numb.count(c)==1);
  }

  double base_font::get_width(uint32_t numb)
  {
    initialise();

    return numb_to_width.at(numb);
  }

  double base_font::get_width(const std::string& c)
  {
    initialise();

    if(utf8_to_numb.count(c)==1)
      {
        uint32_t numb = utf8_to_numb.at(c);
        if(numb_to_width.count(numb))
          {
            return numb_to_width.at(numb);
          }
      }

    LOG_S(ERROR) << "could not find width for '" << c << "'";

    return 500;
  }

  bool base_font::has_char_bbox(const uint32_t& numb)
  {
    initialise();
    return (numb_to_bbox.count(numb)==1);
  }

  bool base_font::has_char_bbox(const std::string& c)
  {
    initialise();
    if(utf8_to_numb.count(c)!=1)
      {
        return false;
      }
    return (numb_to_bbox.count(utf8_to_numb.at(c))==1);
  }

  std::array<double, 4> base_font::get_char_bbox(const uint32_t& numb)
  {
    initialise();
    return numb_to_bbox.at(numb);
  }

  std::array<double, 4> base_font::get_char_bbox(const std::string& c)
  {
    initialise();
    if(utf8_to_numb.count(c)==1)
      {
        uint32_t numb = utf8_to_numb.at(c);
        if(numb_to_bbox.count(numb)==1)
          {
            return numb_to_bbox.at(numb);
          }
      }

    std::stringstream ss;
    ss << "could not find char bbox for '" << c << "'";
    LOG_S(ERROR) << ss.str();
    throw std::logic_error(ss.str());
  }

  std::string base_font::get_string(uint32_t numb)
  {
    initialise();

    return numb_to_utf8.at(numb);
  }

  std::string base_font::to_utf8(uint32_t numb)
  {
    initialise();

    return numb_to_utf8.at(numb);
  }

  double base_font::get_ascend()
  {
    initialise();

    if(properties.count("Ascender")==1)
      {
        return properties["Ascender"].get<double>();
      }
    else if(properties.count("FontBBox")==1)
      {
	LOG_S(ERROR) << "properties does not have key 'Ascender': falling back on the 'FontBBox'";

	std::array<double, 4> bbox = properties["FontBBox"].get<std::array<double, 4> >();
	return bbox[3];
      }

    {
      std::stringstream ss;
      ss << "properties does not have key 'Ascender': " 
	 << properties.dump(2);
      
      LOG_S(ERROR) << ss.str();
      throw std::logic_error(ss.str());
    }
    
    return -1.;
  }

  double base_font::get_descend()
  {
    initialise();

    if(properties.count("Descender")==1)
      {
        return properties["Descender"].get<double>();
      }
    else if(properties.count("FontBBox")==1)
      {
	LOG_S(ERROR) << "properties does not have key 'Descender': falling back on the 'FontBBox'"; 

	std::array<double, 4> bbox = properties["FontBBox"].get<std::array<double, 4> >();
	return bbox[1];
      }

    {
      std::stringstream ss;
      ss << "properties does not have key 'Descender': " 
	 << properties.dump(2);

      LOG_S(ERROR) << ss.str();
      throw std::logic_error(ss.str());
    }
    
    return -1.;
  }

  double base_font::get_capheight()
  {
    initialise();

    if(properties.count("CapHeight")==1)
      {
        return properties["CapHeight"].get<double>();
      }

    return get_ascend();
  }

  double base_font::get_xheight()
  {
    initialise();

    if(properties.count("XHeight")==1)
      {
        return properties["XHeight"].get<double>();
      }

    return 0.;
  }

  std::array<double, 4> base_font::get_font_bbox()
  {
    initialise();

    if(properties.count("FontBBox")==1)
      {
        return properties["FontBBox"].get<std::array<double, 4> >();
      }

    {
      std::stringstream ss;
      ss << "properties does not have key 'FontBBox': " 
	 << properties.dump(2);
      
      LOG_S(ERROR) << ss.str();
      throw std::logic_error(ss.str());
    }
    
    return {0.0, 0.0, 0.0, 0.0};
  }

  void base_font::initialise()
  {
    if(initialised)
      {
	return;
      }
    initialised = true;

    LOG_S(WARNING) << "initialising base-font: " << filename;
    
    std::ifstream file(filename.c_str());

    std::string line;
    while (std::getline(file, line))
      {
	if(line.size()>0 and line.back()=='\n')
	  {
	    line.pop_back();
	  }

	if(line.size()>0 and line.back()=='\r')
	  {
	    line.pop_back();
	  }

        if(line.size()==0 or (line.front()=='#'))
          {
            continue;
          }
        else if(line.size() > 2 and line[0] == 'C' and line[1] == ' ')
          {
            std::vector<std::string> fields = utils::string::split(line, ";");
            int numb = -1;
            double wval = 0.0;
            std::string name = "";
            bool have_bbox = false;
            std::array<double, 4> bbox = {0.0, 0.0, 0.0, 0.0};

            for(auto& field : fields)
              {
                field = utils::string::strip(field);
                if(field.empty())
                  {
                    continue;
                  }

                std::vector<std::string> elems = utils::string::split(field, " ");
                std::vector<std::string> toks;
                for(auto& elem : elems)
                  {
                    elem = utils::string::strip(elem);
                    if(not elem.empty())
                      {
                        toks.push_back(elem);
                      }
                  }

                if(toks.size() >= 2 and toks[0] == "C")
                  {
                    numb = std::stoi(toks[1]);
                  }
                else if(toks.size() >= 2 and toks[0] == "WX")
                  {
                    wval = utils::numeric::locale_safe_stod(toks[1]);
                  }
                else if(toks.size() >= 2 and toks[0] == "N")
                  {
                    name = toks[1];
                  }
                else if(toks.size() >= 5 and toks[0] == "B")
                  {
                    bbox[0] = std::stod(toks[1]);
                    bbox[1] = std::stod(toks[2]);
                    bbox[2] = std::stod(toks[3]);
                    bbox[3] = std::stod(toks[4]);
                    have_bbox = true;
                  }
              }

            if(numb>=0 and name!="")
              {
                uint32_t uc = static_cast<uint32_t>(numb);

                numb_to_name[uc] = name;
                numb_to_utf8[uc] = glyphs[name];
                utf8_to_numb[numb_to_utf8[uc]] = uc;

                numb_to_width[uc] = wval;
                name_to_width[name] = wval;
                if(have_bbox)
                  {
                    numb_to_bbox[uc] = bbox;
                    name_to_bbox[name] = bbox;
                    LOG_S(INFO) << "char-bbox: "
                                << uc << "\t" << name << "\t["
                                << bbox[0] << ", " << bbox[1] << ", "
                                << bbox[2] << ", " << bbox[3] << "]";
                  }
              }
          }
        else
          {
            std::vector<std::string> elems = utils::string::split(line, " ");

            for(int l=0; l<elems.size(); l++)
              {
                elems[l] = utils::string::strip(elems[l]);
              }

            if(elems.size()==2 and utils::string::is_number(elems[1]))
              {
                properties[elems[0]] = utils::numeric::locale_safe_stod(elems[1]);
              }
            else if(elems.size()==2)
              {
                properties[elems[0]] = elems[1];
              }
            else if(elems.size()>0 and elems[0]=="FontBBox")
              {
                std::array<double, 4> bbox = {
                  utils::numeric::locale_safe_stod(elems[1]),
                  utils::numeric::locale_safe_stod(elems[2]),
                  utils::numeric::locale_safe_stod(elems[3]),
                  utils::numeric::locale_safe_stod(elems[4])};

                properties[elems[0]] = bbox;
              }
          }
      }

    {
      LOG_S(INFO) << "initialised base-font: " << properties.dump(2);

      for(auto itr=numb_to_name.begin(); itr!=numb_to_name.end(); itr++)
        {
          LOG_S(INFO) << itr->first << "\t"
		      << itr->second << "\t"
		      << numb_to_utf8[itr->first] << "\t"
		      << numb_to_width[itr->first];
        }
    }
  }

}

#endif
