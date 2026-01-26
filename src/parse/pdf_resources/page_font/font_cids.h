//-*-C++-*-

#ifndef PDF_PAGE_FONT_CIDS_H
#define PDF_PAGE_FONT_CIDS_H

#include <fstream>

#include <set>
#include <map>

namespace pdflib
{

  class font_cids
  {

  public:

    font_cids();
    ~font_cids();

    void initialise(std::string dirname);

    bool      has(std::string font_name);
    font_cid& get(std::string encoding_name);

    int get_supplement(std::string registry, 
		       std::string ordering);

    bool decode_cmap_resource(std::string font_name);

    void decode_all();

  private:

    bool initialized;
    std::string directory;

    std::map<std::string, int>                       ro_2_sup;
    std::map<std::string, std::vector<std::string> > ros_2_cols;

    std::map<std::string, std::string> cmap_2_filename;
    std::map<std::string, std::string> cmap_2_cid2code;
    std::map<std::string, std::vector<std::string> > cmap_2_columns;

    std::map<std::string, font_cid> cids;
  };

  font_cids::font_cids():
    initialized(false)
  {}

  font_cids::~font_cids()
  {}

  bool font_cids::has(std::string font_name)
  {
    //LOG_S(INFO) << __FUNCTION__ << ": " << font_name << "\t" 
    //<< cmap_2_filename.count(font_name);

    return (cmap_2_filename.count(font_name)==1);
  }

  font_cid& font_cids::get(std::string encoding_name)
  {
    return cids[encoding_name];
  }

  int font_cids::get_supplement(std::string registry, 
				std::string ordering)
  {
    LOG_S(INFO) << __FUNCTION__;

    std::string key = registry+"-"+ordering;

    for(auto itr=ro_2_sup.begin(); itr!=ro_2_sup.end(); itr++)
      {
	if(itr->first==key)
	  {
	    return itr->second;
	  }
      }

    return -1;
  }

  void font_cids::initialise(std::string dirname)
  {
    if(initialized)
      {
	LOG_S(WARNING) << "skipping font_cids::initialise, already initialized ...";
	return;
      }
    
    LOG_S(INFO) << "initialise font_cids";

    directory = dirname;
    directory += (directory.back()=='/'? "" : "/");

    ro_2_sup = {
      { "Adobe-CNS1"  , 7},
      { "Adobe-KR"    , 9},
      { "Adobe-Japan1", 7},
      { "Adobe-GB1"   , 5},
      { "Adobe-Korea1", 2}
    };

    ros_2_cols = {
      { "Adobe-CNS1-7"  , {"UniCNS-UTF16"} },
      { "Adobe-KR-9"    , {"UniAKR-UTF16"} },
      { "Adobe-Japan1-7", {"UniJIS-UTF16", "Roman"} },
      { "Adobe-GB1-5"   , {"UniGB-UTF16"} },
      { "Adobe-Korea1-2", {"UniKS-UTF16"} }
    };

    for(auto itr=ros_2_cols.begin(); itr!=ros_2_cols.end(); itr++)
      {
	std::string cdir = directory+(itr->first)+"/CMap";
	
	std::vector<std::string> files = utils::filesystem::list_files(cdir, true);

	for(auto file:files)
	  {
	    std::string key = "/"+file;
	    LOG_S(INFO) << __FUNCTION__ << "\t" << file;
	    
	    cmap_2_columns[key]  = (itr->second);
	    cmap_2_columns[file] = (itr->second);

	    cmap_2_cid2code[key]  = directory+(itr->first)+"/cid2code.txt";
	    cmap_2_cid2code[file] = directory+(itr->first)+"/cid2code.txt";
	    
	    cmap_2_filename[key]  = cdir+"/"+file;
	    cmap_2_filename[file] = cdir+"/"+file;
	  }
      }

    initialized = true;
  }

  bool font_cids::decode_cmap_resource(std::string cmap_name)
  {
    LOG_S(INFO) << __FUNCTION__;    

    // fetch pre-cached cid-fonts
    if(cids.count(cmap_name)==1)
      {
	//cid = cids[cmap_name];
	return true;
      }
    else if(cmap_2_cid2code.count(cmap_name)==1)
      {
	std::vector<std::string> columns = cmap_2_columns[cmap_name];
	std::string cid2code = cmap_2_cid2code[cmap_name];
	std::string filename = cmap_2_filename[cmap_name];
	
	font_cid& cid = cids[cmap_name];
	cid.decode_cmap_resource(filename, cid2code, columns);

	//cids[cmap_name] = cid;
	return true;
      }
    else
      {
	LOG_S(INFO) << "unknown cmap-name: " << cmap_name;    	
	return false;
      }
  }

  void font_cids::decode_all()
  {
    LOG_S(INFO) << __FUNCTION__;

    for(auto itr=cmap_2_filename.begin(); itr!=cmap_2_filename.end(); itr++)
      {
	std::string cmap_name = itr->first; 

	if(cmap_name.front()=='/')
	  {
	    font_cid cid;
	    
	    std::vector<std::string> columns = cmap_2_columns[cmap_name];
	    std::string cid2code = cmap_2_cid2code[cmap_name];
	    std::string filename = cmap_2_filename[cmap_name];
	    
	    cid.decode_cmap_resource(filename, cid2code, columns);
	  }
      }
  }

}

#endif
