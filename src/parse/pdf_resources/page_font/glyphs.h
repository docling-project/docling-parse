//-*-C++-*-

#ifndef PDF_PAGE_FONT_GLYPHS_H
#define PDF_PAGE_FONT_GLYPHS_H

#include <fstream>

#include <set>
#include <map>

namespace pdflib
{

  class font_glyphs
  {

  public:

    font_glyphs();
    ~font_glyphs();

    void print();

    void print_unknown_glyphs();

    bool has(std::string key);

    std::string operator[](std::string key);

    void initialise(std::string dirname);
    
  private:

    void read_file_hex(std::string filename);

    void read_file_uni(std::string filename);

    std::string preprocess(std::string val);
    
  private:

    bool initialized;
    
    std::set<std::string> unknown_glyphs;

    std::map<std::string, std::string> name_to_code;
    std::map<std::string, std::string> name_to_utf8;
  };

  font_glyphs::font_glyphs():
    initialized(false)
  {}

  font_glyphs::~font_glyphs()
  {}

  void font_glyphs::print()
  {
    for(auto itr=name_to_utf8.begin(); itr!=name_to_utf8.end(); itr++)
      {
        std::string key = itr->first;

        std::string val_0 = name_to_code[key];
        std::string val_1 = name_to_utf8[key];

	/*
        if(key[0]=='H')
          {            
            LOG_S(INFO) << key << "\t" << val_0 << "\t" << val_1;
          }
	*/
      }
  }

  void font_glyphs::print_unknown_glyphs()
  {
    LOG_S(ERROR) << "unknown glyphs: " << unknown_glyphs.size();
    for(auto itr=unknown_glyphs.begin(); itr!=unknown_glyphs.end(); itr++)
      {
	LOG_S(ERROR) << "\tglyph-name: " << *itr;
      }    

    std::stringstream ss;
    for(auto itr=unknown_glyphs.begin(); itr!=unknown_glyphs.end(); itr++)
      {
	ss << "\n" << "#" << *itr << ";";
      }    
    LOG_S(INFO) << ss.str();
  }
                 
  bool font_glyphs::has(std::string key)
  {
    return (name_to_utf8.count(key)>0);
  }

  std::string font_glyphs::operator[](std::string key)
  {
    if(name_to_utf8.count(key)==1)
      return name_to_utf8[key];

    LOG_S(ERROR) << "could not find a glyph with name=" << key;
    unknown_glyphs.insert(key);

    return "glyph["+key+"]";
  }

  void font_glyphs::initialise(std::string dirname)
  {
    if(initialized)
      {
	LOG_S(WARNING) << "skipping font_glyphs::initialise, already initialized ...";
	return;
      }
    
    LOG_S(INFO) << "font-glyphs initialise from directory: " 
		<< dirname;

    std::vector<std::string> paths_hex = {
      "/standard/additional.dat",
      "/standard/glyphlist.dat",
      "/standard/zapfdingbats.dat",
      "/standard/scripts.dat",
      "/standard/missing.dat",

      "/custom/MathematicalPi/MathematicalPi.hex.dat"
    };

    for(auto path:paths_hex)
      {
        std::string fpath = dirname + path; 
        read_file_hex(fpath);
      }

    std::vector<std::string> paths_uni = {
      "/custom/MathematicalPi/MathematicalPi.uni.dat"
    };

    for(auto path:paths_uni)
      {
        std::string fpath = dirname + path; 
        read_file_uni(fpath);
      }

    initialized = true;
  }

  void font_glyphs::read_file_hex(std::string filename)
  {
    LOG_S(INFO) << __FUNCTION__ << ": " << filename;

    std::ifstream file(filename.c_str());

    if(file.fail())
      {
	std::stringstream ss;
	ss << "filename does not exists: " << filename;	
	
	LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());
      }

    std::string line;
    while (std::getline(file, line))
      {
        if(line.size()==0 or (line.front()=='#'))
          continue;

        size_t ind=line.find(";");
        if(ind!=std::string::npos)
          {
            std::string key = line.substr(0    , ind);
            std::string val = line.substr(ind+1, line.size()-(ind+1));

            name_to_code[key] = val;

            std::string val_ = preprocess(val);

            if(val_.size()%4==0 and name_to_utf8.count(key)==0)
              {
                name_to_utf8[key] = utils::string::hex_to_utf8(val_, 4);
              }
            else if(name_to_utf8.count(key)==1) // already present
              {
                LOG_S(ERROR) << "key [" << key << "] is defined twice";               
              }
            else
              {
                LOG_S(ERROR) << key << "  -->  " << val;               
                name_to_utf8[key] = "glyph["+val_+"]";
              }
          }
        else
          {
            LOG_S(ERROR) << "ignoring " << line;
          }
      }
  }

  std::string font_glyphs::preprocess(std::string val)
  {
    if(val.size()%4!=0)
      {
        auto itr = std::remove(val.begin(), val.end(), ' ');
        val.erase(itr, val.end());
        
        size_t ind=val.find(",");
        if(ind!=std::string::npos)
          {
            val = val.substr(0, ind);
          }        
      }

    return val;
  }

  void font_glyphs::read_file_uni(std::string filename)
  {
    LOG_S(WARNING) << __FUNCTION__ << ": " << filename;

    std::ifstream file(filename.c_str());

    if(file.fail())
      {
	std::stringstream ss;
	ss << "filename does not exists: " << filename;	
	
	LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());
      }
    
    std::string line;
    while (std::getline(file, line))
      {
        if(line.size()==0 or (line.front()=='#'))
          continue;

        size_t ind_0=line.find(";", 0);
        size_t ind_1=line.find(";", ind_0+1);

        if(ind_0!=std::string::npos)
          {
            std::string key  = line.substr(0      , ind_0);
            std::string val  = line.substr(ind_0+1, ind_1-ind_0-1);
            std::string code = line.substr(ind_1+1, line.size()-(ind_1+1));

            /*
              LOG_S(WARNING) << "------------";
              LOG_S(WARNING) << key;
              LOG_S(WARNING) << val; 
              LOG_S(WARNING) << code;
            */

            if(name_to_code.count(key)==0 and 
               name_to_utf8.count(key)==0   )
              {
                name_to_code[key] = code;
                name_to_utf8[key] = val;
              }
            else
              {
                LOG_S(ERROR) << "key [" << key << "] is defined twice";               
              }
          }
      }
  }

}

#endif
