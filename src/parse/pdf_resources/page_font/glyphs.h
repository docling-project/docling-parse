//-*-C++-*-

#ifndef PDF_PAGE_FONT_GLYPHS_H
#define PDF_PAGE_FONT_GLYPHS_H

#include <fstream>

#include <unordered_set>
#include <unordered_map>

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
    
    std::unordered_set<std::string> unknown_glyphs;

    std::unordered_map<std::string, std::string> name_to_code;
    std::unordered_map<std::string, std::string> name_to_utf8;
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

    // `.notdef` and friends are not unknown glyphs: they are the standard
    // names for "nothing is drawn here". Subsetted fonts routinely leave every
    // entry named `.notdef` while carrying a perfectly good /ToUnicode, so a
    // marker here does not report a lookup failure -- it injects the literal
    // text "glyph[.notdef]" into the page, once per character, which is then
    // both extracted and drawn.
    if(key==".notdef" or key==".null" or key=="nonmarkingreturn")
      {
        return "";
      }

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

      "/custom/MathematicalPi/MathematicalPi.hex.dat",
      "/custom/TeX/glyphs_01.dat",
      "/custom/TeX/glyphs_03.dat"
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

            // The Adobe Glyph List places the Symbol font's bracket, brace and
            // parenthesis PIECES -- the parts a typesetter stacks to build a
            // tall delimiter around a matrix -- in the private-use area, where
            // they were assigned before Unicode had them. Nothing but the
            // original Symbol font maps that range, so a fallback face draws
            // .notdef boxes down the sides of every matrix. Unicode has had
            // these since 3.2 (Miscellaneous Technical, U+239B..U+23AD), and
            // ordinary text faces carry them; rewrite the legacy codes to the
            // real ones, which fixes both the render and the extracted text.
            {
              static const std::map<std::string, std::string> legacy_pua = {
                {"F8EB", "239B"}, {"F8EC", "239C"}, {"F8ED", "239D"},  // ( pieces
                {"F8F6", "239E"}, {"F8F7", "239F"}, {"F8F8", "23A0"},  // ) pieces
                {"F8EE", "23A1"}, {"F8EF", "23A2"}, {"F8F0", "23A3"},  // [ pieces
                {"F8F9", "23A4"}, {"F8FA", "23A5"}, {"F8FB", "23A6"},  // ] pieces
                {"F8F1", "23A7"}, {"F8F2", "23A8"}, {"F8F3", "23A9"},  // { pieces
                {"F8F4", "23AA"},                                       // brace ext
                {"F8FC", "23AB"}, {"F8FD", "23AC"}, {"F8FE", "23AD"},  // } pieces
                {"F8F5", "23AE"},                                       // integral ext
                {"F8E7", "23AF"},                                       // horizontal ext
                {"F8E6", "23D0"},                                       // vertical ext
              };
              auto it = legacy_pua.find(val_);
              if(it != legacy_pua.end()) { val_ = it->second; }
            }

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
