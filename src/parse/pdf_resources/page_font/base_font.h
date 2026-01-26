//-*-C++-*-

#ifndef PDF_PAGE_FONT_BASE_FONT_H
#define PDF_PAGE_FONT_BASE_FONT_H

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
    bool has(std::string c);

    double get_width(uint32_t numb);
    double get_width(std::string c);

    std::string get_string(uint32_t numb);

    std::string to_utf8(uint32_t numb);

    double get_ascend();
    double get_descend();

    std::array<double, 4> get_font_bbox();

    //private:

    void initialise();

  private:

    std::string filename;
    font_glyphs& glyphs;

    bool initialised;

    nlohmann::json properties;
    
    std::map<uint32_t, std::string> numb_to_name;
    std::map<uint32_t, std::string> numb_to_utf8;

    std::map<uint32_t   , double> numb_to_width;
    std::map<std::string, double> name_to_width;
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

  bool base_font::has(std::string c)
  {
    initialise();

    for(auto itr=numb_to_utf8.begin(); itr!=numb_to_utf8.end(); itr++)
      {
	if(c==itr->second)
	  {
	    return true;
	  }
      }

    return false;
  }

  double base_font::get_width(uint32_t numb)
  {
    initialise();

    return numb_to_width.at(numb);
  }

  double base_font::get_width(std::string c)
  {
    initialise();

    for(auto itr=numb_to_utf8.begin(); itr!=numb_to_utf8.end(); itr++)
      {
	if(c==itr->second and numb_to_width.count(itr->first))
	  {
	    return numb_to_width[itr->first];
	  }
      }

    LOG_S(ERROR) << "could not find width for '" << c << "'";

    return 500;
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

    std::regex  expr("C\\s(-?\\d+)\\s;\\s[A-Z]+\\s(\\d+)\\s;\\s[A-Z]+\\s([A-Za-z0-9]+).*");
    std::smatch match;

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
        else if(std::regex_match(line, match, expr))
          {
            int         numb = std::stoi(match[1]);
            double      wval = std::stod(match[2]);
            std::string name =           match[3] ;

            if(numb>=0 and name!="")
              {
                uint32_t uc = static_cast<uint32_t>(numb);

                numb_to_name[uc] = name;
                numb_to_utf8[uc] = glyphs[name];

                numb_to_width[uc] = wval;
                name_to_width[name] = wval;
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
                properties[elems[0]] = std::stod(elems[1]);
              }
            else if(elems.size()==2)
              {
                properties[elems[0]] = elems[1];
              }
            else if(elems.size()>0 and elems[0]=="FontBBox")
              {
                std::array<double, 4> bbox = { 
                  std::stod(elems[1]),
                  std::stod(elems[2]),
                  std::stod(elems[3]),
                  std::stod(elems[4])};

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
