//-*-C++-*-

#ifndef PDF_PAGE_FONT_CID_H
#define PDF_PAGE_FONT_CID_H

#include <fstream>

#include <set>
#include <map>

namespace pdflib
{

  class font_cid
  {

  public:

    font_cid();
    ~font_cid();

    std::map<uint32_t, std::string>& get();

    void decode_cmap_resource(std::string filename,
                              std::string cid2code,
                              std::vector<std::string> columns);

    void decode_widths(std::map<uint32_t, double>& numb_to_widths);

  private:

    std::vector<std::string> split(std::string line, char delim='\t');

    void read_cmap2cid(std::string filename);

    void read_cid2code(std::string              filename,
                       std::vector<std::string> columns);

  private:

    std::map<uint32_t, uint32_t>    cmap2cid;
    std::map<uint32_t, std::string> cid2utf8;

    std::map<uint32_t, std::string> cmap2str;
  };

  font_cid::font_cid()
  {}

  font_cid::~font_cid()
  {}

  std::map<uint32_t, std::string>& font_cid::get()
  {
    return cmap2str;
  }

  std::vector<std::string> font_cid::split(std::string line,
                                           char        delim)
  {
    size_t ind0 = 0;
    size_t ind1 = line.find(delim, ind0);

    std::vector<std::string> parts = {};
    while(ind1!=std::string::npos)
      {
        parts.push_back(line.substr(ind0, ind1-ind0));

        ind0 = ind1+1;
        ind1 = line.find(delim, ind0);
      }

    if(ind0<line.size())
      {
        parts.push_back(line.substr(ind0, line.size()-ind0));
      }

    return parts;
  }

  void font_cid::read_cmap2cid(std::string filename)
  {
    LOG_S(INFO) << __FUNCTION__ << "\t filename: " << filename;

    std::ifstream file(filename.c_str());

    if(file.fail())
      {
	//LOG_S(ERROR) << "filename does not exists: " << filename;
	std::stringstream ss;
	ss << "filename does not exists: " << filename;
	
	LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());
      }

    bool        cmap=false;
    std::string line;
    while(std::getline(file, line))
      {
        if(line.find("begincidrange")!=std::string::npos)
          {
            cmap=true;
          }
        else if(line.find("endcidrange")!=std::string::npos)
          {
            cmap=false;
          }
        else if(cmap)
          {
            std::vector<std::string> items = split(line, ' ');

            auto itr = std::remove(items.begin(), items.end(), "");
            items.erase(itr, items.end());

            //assert(items.size()==3);
	    if(items.size()==3)
	      {
		std::string hex_beg = items[0].substr(1, items[0].size()-2);
		std::string hex_end = items[1].substr(1, items[1].size()-2);
		std::string val     = items[2];

		uint32_t beg = stoul(hex_beg, NULL, 16);
		uint32_t end = stoul(hex_end, NULL, 16);
		uint32_t ind = stoul(val    , NULL, 10);

		//LOG_S(INFO) << "\t"
		//<< hex_beg << " (=" << beg << ")\t"
		//<< hex_end << " (=" << end << ")\t"
		//<< val     << " (=" << ind << ")";
		
		for(uint32_t i=beg; i<=end; i++)
		  {
		    cmap2cid[i] = ind;
		    ind += 1;
		  }
	      }
	    else
	      {
		LOG_S(ERROR) << "items does not have the right size";
	      }
          }
        else // ignore
          {
	    LOG_S(WARNING) << "ignoring";
	  }
      }
  }

  void font_cid::read_cid2code(std::string              filename,
                               std::vector<std::string> columns)
  {
    LOG_S(INFO) << __FUNCTION__ << "\t filename: " << filename;

    std::ifstream file(filename.c_str());

    if(file.fail())
      {
	std::stringstream ss;
	ss << "filename does not exists: " << filename;

	LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());
      }

    std::vector<int> col_inds = {};

    std::string line;
    while(std::getline(file, line))
      {
        if(line.size()==0 or line.front()=='#') // ignore
          {
            continue;
          }

        std::vector<std::string> parts = split(line);

        if(col_inds.size()==0)
          {
            for(auto column:columns)
              {
                for(int l=0; l<parts.size(); l++)
                  {
                    if(parts[l]==column)
                      {
                        col_inds.push_back(l);
                      }
                  }
              }
            //assert(col_inds.size()>0);

            //for(auto col_ind:col_inds)
            //{
            //LOG_S(WARNING) << column << "\t" << parts[col_ind] << "\t" << col_ind;
            //}
          }
        else if(col_inds.size()>0)
          {
            uint32_t cid = stoul(parts[0], NULL, 10);

            for(auto col_ind:col_inds)
              {
                if(col_ind>=parts.size())
                  {
                    LOG_S(ERROR) << "parts.size() (=" << parts.size() << ") < "
                                 << "col-ind (=" << col_ind << ")";
                    continue;
                  }

                std::string name = parts[col_ind];

                {
                  auto itr = std::remove(name.begin(), name.end(), ' ');
                  name.erase(itr, name.end());

                  itr = std::remove(name.begin(), name.end(), 'v');
                  name.erase(itr, name.end());
                }

                if(name=="*")
                  {}
                else if(name.find(",")!=std::string::npos)
                  {
                    std::vector<std::string> vec = split(name, ',');

                    /*
                      std::string utf8="";
                      for(auto item:vec)
                      {
                      utf8 += utils::string::hex_to_utf8(item, 4);
                      }

                      cid2utf8[cid] = utf8;
                    */

                    if(vec.size()>0)
                      {
                        std::string utf8 = utils::string::hex_to_utf8(vec.front(), 4);
                        cid2utf8[cid] = utf8;
                      }
                    else
                      {
                        LOG_S(WARNING) << "name '" << name << "' has no components!";
                      }
                  }
                else if((name.size()%4)==0)
                  {
                    std::string utf8 = utils::string::hex_to_utf8(name, 4);
                    cid2utf8[cid] = utf8;
                  }
                else if(name.size()==2)
                  {
                    std::string utf8 = utils::string::hex_to_utf8(name, 2);
                    cid2utf8[cid] = utf8;
                  }
                else
                  {
                    LOG_S(ERROR) << "ignoring cid: " << cid << "\tname: " << name;
                    cid2utf8[cid] = "GLYPH<cid:"+std::to_string(cid)+">";
                  }
              }
          }
        else
          {
            LOG_S(ERROR) << "all options exhausted for " << __FUNCTION__;
          }
      }

  }

  void font_cid::decode_cmap_resource(std::string              filename,
                                      std::string              cid2code,
                                      std::vector<std::string> columns)
  {
    LOG_S(INFO) << __FUNCTION__;

    {
      LOG_S(INFO) << "filename: " << filename;
      LOG_S(INFO) << "cid2code: " << cid2code;
      for(auto column:columns)
        {
          LOG_S(INFO) << "\tcolumn  : " << column;
        }
    }

    read_cmap2cid(filename);

    read_cid2code(cid2code, columns);

    for(auto itr=cmap2cid.begin(); itr!=cmap2cid.end(); itr++)
      {
        if(cid2utf8.count(itr->second)==1)
          {
            cmap2str[itr->first] = cid2utf8[itr->second];

            //if(/*itr->first<512 or*/ itr->first==3301)
            //{
            //LOG_S(INFO) << "cmap2str[" << itr->first << "]: " << cmap2str[itr->first];
            //}
          }
        else
          {
            //LOG_S(ERROR) << "cid (=" << itr->second << ") is not in cid2utf8 "
	    //<< "for cmap (=" << itr->first << ")" ;
          }
      }
  }

  void font_cid::decode_widths(std::map<uint32_t, double>& numb_to_widths)
  {
    LOG_S(INFO) << __FUNCTION__;

    std::map<uint32_t, double> numb_to_widths_ = numb_to_widths;
    numb_to_widths.clear();

    for(auto itr=cmap2cid.begin(); itr!=cmap2cid.end(); itr++)
      {
	if(numb_to_widths_.count(itr->second)==1)
	  {
	    numb_to_widths[itr->first] = numb_to_widths_[itr->second];
	  }
	else
	  {
	    //LOG_S(ERROR) << "cmap-id (=" << itr->first << ") is not in numb_to_widths "
	    //<< "for cid (=" << itr->second << ")";
	  }
      }
  }

}

#endif
