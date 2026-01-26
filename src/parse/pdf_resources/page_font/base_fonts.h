//-*-C++-*-

#ifndef PDF_PAGE_FONT_BASE_FONTS_H
#define PDF_PAGE_FONT_BASE_FONTS_H

#include <set>
#include <map>

//#include <filesystem>

namespace pdflib
{

  class base_fonts
  {
    typedef base_font base_font_type;

  public:

    base_fonts();
    ~base_fonts();

    void print();

    bool is_core_14_font(std::string font_name);

    bool        has_corresponding_font(std::string font_name);
    std::string get_corresponding_font(std::string font_name);

    bool            has(std::string font_name);
    base_font_type& get(std::string font_name);

    base_font_type& operator[](std::string font_name);

    template<typename glyphs_type>
    void initialise(std::string filename, glyphs_type& glyphs);

  private:

    std::string normalise(std::string font_name);

    std::string read_fontname(std::string filename);

  private:

    bool initialized;
    
    std::set<std::string> core_14_fonts;

    std::map<std::string, base_font_type> name_to_basefont;
  };

  base_fonts::base_fonts():
    initialized(false)
  {}

  base_fonts::~base_fonts()
  {}

  void base_fonts::print()
  {
    for(auto itr=name_to_basefont.begin(); itr!=name_to_basefont.end(); itr++)
      {
        LOG_S(INFO) << itr->first;
      }
  }

  std::string base_fonts::normalise(std::string font_name)
  {
    std::string norm_name="";

    for(int l=0; l<font_name.size(); l++)
      {
	char c = font_name[l]; 

	if(c=='/' or c=='\r' or c=='\n' or c=='\\')
	  {
	    continue;
	  }
	else if('A'<=c and c<='Z')
	  {
	    char t = c-'Z'+'z';
	    norm_name.push_back(t);
	  }
	else
	  {
	    norm_name.push_back(c);
	  }
      }

    return norm_name;
  }

  bool base_fonts::is_core_14_font(std::string font_name)
  {
    std::string norm_name = normalise(font_name);
    return (core_14_fonts.count(norm_name)==1);
  }

  bool base_fonts::has_corresponding_font(std::string font_name)
  {
    std::string norm_name = normalise(font_name);
    for(auto itr=name_to_basefont.begin(); itr!=name_to_basefont.end(); itr++)
      {
        if(norm_name.find(itr->first)!=std::string::npos)
          {
	    //LOG_S(INFO) << "\t identified " << itr->first << " in " << font_name;	    
            return true;
          }
      }

    return false;
  }

  std::string base_fonts::get_corresponding_font(std::string font_name)
  {
    std::string norm_name = normalise(font_name);

    std::string result = "";
    for(auto itr=name_to_basefont.begin(); itr!=name_to_basefont.end(); itr++)
      {
        if(norm_name.find(itr->first)!=std::string::npos)
          {	    
	    // we have to be careful that "Helvetica" is not returned for Helvetice-Bold!
	    if(result.size()<(itr->first).size())
	      {
		result = itr->first;
	      }
          }
      }

    if(result.size()>0)
      {
	return result;
      }

    LOG_S(ERROR) << "unkown " << font_name << "[norm_name=" << norm_name << "]";
    
    return "Unknown";
  }

  bool base_fonts::has(std::string font_name)
  {
    std::string norm_name = normalise(font_name);
    return (name_to_basefont.count(norm_name)==1);
  }

  base_font& base_fonts::get(std::string font_name)
  {
    std::string norm_name = normalise(font_name);
    return name_to_basefont.at(norm_name);
  }

  base_font& base_fonts::operator[](std::string font_name)
  {
    std::string norm_name = normalise(font_name);
    return name_to_basefont.at(norm_name);
  }

  template<typename glyphs_type>
  void base_fonts::initialise(std::string dirname, glyphs_type& glyphs)
  {
    if(initialized)
      {
	LOG_S(WARNING) << "skipping base_fonts::initialise, already initialized ...";
	return;
      }
    
    std::vector<std::string> standard = utils::filesystem::list_files(dirname+"/standard");
    std::sort(standard.begin(), standard.end());

    LOG_S(INFO) << "standard-fonts: " << standard.size();
    for(auto path:standard)
      {
	LOG_S(INFO) << "\t file-name: " << path;

        std::string fontname = read_fontname(path);
	//LOG_S(INFO) << "\t font-name: " << fontname;

	base_font bf(path, glyphs);	
	name_to_basefont.emplace(std::pair<std::string, base_font>(fontname, bf));	

	core_14_fonts.insert(fontname);
      }    

    std::vector<std::string> microsoft = utils::filesystem::list_files(dirname+"/custom/Microsoft");
    std::sort(microsoft.begin(), microsoft.end());

    LOG_S(INFO) << "microsoft-fonts: " << microsoft.size();
    for(auto path:microsoft)
      {
        std::string fontname = read_fontname(path);

	if(name_to_basefont.count(fontname)==0)
	  {
	    LOG_S(INFO) << "\t reading font " << fontname << "at " << path;
	    
	    base_font bf(path, glyphs);
	    name_to_basefont.emplace(std::pair<std::string, base_font>(fontname, bf));	
	  }    
	else
	  {
	    //LOG_S(WARNING) << "\t font-name (=" << fontname << ") already read";
	  }
      }

    std::vector<std::string> tex = utils::filesystem::list_files(dirname+"/custom/TeX");
    std::sort(tex.begin(), tex.end());

    LOG_S(INFO) << "tex-fonts: " << tex.size();
    for(auto path:tex)
      {
        std::string fontname = read_fontname(path);
	//LOG_S(INFO) << "\t font-name: " << fontname;

	if(name_to_basefont.count(fontname)==0)
	  {
	    LOG_S(INFO) << "\t reading font " << fontname << "at " << path;

	    base_font bf(path, glyphs);
	    name_to_basefont.emplace(std::pair<std::string, base_font>(fontname, bf));	
	  }    
	else
	  {
	    //LOG_S(WARNING) << "\t font-name (=" << fontname << ") already read";
	  }
      }

    initialized = true;
  }

  std::string base_fonts::read_fontname(std::string filename)
  {
    std::string fontname = "unknown";

    std::ifstream file(filename.c_str());
    
    std::string line;
    while (std::getline(file, line))
      {
        if(line.size()==0 or (line.front()=='#'))
	  {
	    continue;
	  }
	
        size_t ind = line.find("FontName");
        if(ind!=std::string::npos)
          {
            std::vector<std::string> elems = utils::string::split(line, " ");

            fontname = "/"+utils::string::strip(elems.back());
	    fontname = normalise(fontname);
	    //fontname = utils::string::strip(elems.back());
	    /*
	    if(fontname.size()>0 and fontname.back()=='\n')
	      {
		fontname.pop_back();
	      }
	    
	    if(fontname.size()>0 and fontname.back()=='\r')
	      {
		fontname.pop_back();
	      }
	    */
            //LOG_S(INFO) << line << " -> " << fontname;                        
            break;
          }
      }

    if(fontname=="unknown")
      {
	std::stringstream ss;
	ss << "no FontName found in " << filename;
	
        LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());
      }

    return fontname;
  }

}

#endif
