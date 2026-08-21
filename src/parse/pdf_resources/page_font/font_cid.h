//-*-C++-*-

#ifndef PDF_PAGE_FONT_CID_H
#define PDF_PAGE_FONT_CID_H

#include <fstream>

#include <limits>
#include <set>
#include <map>
#include <unordered_map>
#include <vector>

namespace pdflib
{

  // One `begincodespacerange` entry of a CMap: the byte strings between `low`
  // and `high`, both `n_bytes` long, are codes of this CMap. The ranges are
  // what tells a decoder how many bytes the next code occupies (ISO 32000-1,
  // 9.7.6.2) -- a Shift-JIS CMap mixes one- and two-byte codes in one string,
  // and only the codespace says which is which.
  struct cmap_codespace_range
  {
    uint32_t low     = 0;
    uint32_t high    = 0;
    int      n_bytes = 1;

    // Adobe matches a code byte by byte against the range bounds rather than
    // comparing the codes as integers: <8140> <9FFC> admits 0x8140..0x9FFC
    // only when the *second* byte is also within 0x40..0xFC.
    bool contains(const unsigned char* bytes) const
    {
      for(int i=0; i<n_bytes; i++)
        {
          const int shift = 8*(n_bytes-1-i);

          const unsigned char lo = (low  >> shift) & 0xFF;
          const unsigned char hi = (high >> shift) & 0xFF;

          if(bytes[i]<lo or hi<bytes[i]) { return false; }
        }

      return true;
    }
  };

  class font_cid
  {

  public:

    font_cid();
    ~font_cid();

    std::unordered_map<uint32_t, std::string>& get();

    // The CMap's codespace ranges, inherited ones included. Empty when the
    // resource declared none.
    const std::vector<cmap_codespace_range>& get_codespaces() const;

    void decode_cmap_resource(std::string filename,
                              std::string cid2code,
                              std::vector<std::string> columns);

    void decode_widths(std::unordered_map<uint32_t, double>& numb_to_widths);

  private:

    std::vector<std::string> split(std::string line, char delim='\t');

    // Of several code points listed for one CID, the first that is not a
    // radical from the Kangxi (U+2F00..U+2FDF) or CJK Radicals Supplement
    // (U+2E80..U+2EFF) blocks; those are compatibility forms that look like
    // the ideograph and do not read as it.
    static const std::string& prefer_character(const std::vector<std::string>& hex_values);

    void read_cmap2cid(std::string filename);

    // `visited` breaks the (illegal but cheap to guard) cycle of two CMaps
    // that `usecmap` each other.
    void read_cmap2cid(std::string             filename,
                       std::set<std::string>&  visited,
                       bool                    required);

    // Resolves the operand of `usecmap` to a file next to `filename`: the
    // referenced CMap always lives in the same character-collection
    // directory.
    static std::string sibling_cmap_path(const std::string& filename,
                                         const std::string& cmap_name);

    // `<00a5>` -> value 0x00a5 over 2 bytes. Returns false for a token that is
    // not a hex string.
    static bool parse_hex_token(const std::string& token,
                                uint32_t&          value,
                                int&               n_bytes);

    void read_cid2code(std::string              filename,
                       std::vector<std::string> columns);

  private:

    std::unordered_map<uint32_t, uint32_t>    cmap2cid;
    std::unordered_map<uint32_t, std::string> cid2utf8;

    std::unordered_map<uint32_t, std::string> cmap2str;

    std::vector<cmap_codespace_range> codespaces;
  };

  font_cid::font_cid()
  {}

  font_cid::~font_cid()
  {}

  std::unordered_map<uint32_t, std::string>& font_cid::get()
  {
    return cmap2str;
  }

  const std::vector<cmap_codespace_range>& font_cid::get_codespaces() const
  {
    return codespaces;
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

  const std::string& font_cid::prefer_character(const std::vector<std::string>& hex_values)
  {
    for(const std::string& hex : hex_values)
      {
        uint32_t codepoint = 0;
        try
          {
            codepoint = static_cast<uint32_t>(std::stoul(hex, NULL, 16));
          }
        catch(const std::exception& exc)
          {
            continue;
          }

        const bool is_radical =
          (0x2E80 <= codepoint and codepoint <= 0x2EFF) or
          (0x2F00 <= codepoint and codepoint <= 0x2FDF);

        if(not is_radical)
          {
            return hex;
          }
      }

    return hex_values.front();
  }

  std::string font_cid::sibling_cmap_path(const std::string& filename,
                                          const std::string& cmap_name)
  {
    std::size_t pos = filename.find_last_of('/');

    std::string dirname = (pos==std::string::npos)? std::string(".")
                                                  : filename.substr(0, pos);

    std::string name = cmap_name;
    if(name.size()>0 and name.front()=='/')
      {
        name = name.substr(1);
      }

    return dirname+"/"+name;
  }

  bool font_cid::parse_hex_token(const std::string& token,
                                 uint32_t&          value,
                                 int&               n_bytes)
  {
    if(token.size()<3 or token.front()!='<' or token.back()!='>')
      {
        return false;
      }

    std::string hex = token.substr(1, token.size()-2);

    // A code is a whole number of bytes, and at most the four this decoder
    // (and every CMap Adobe ships) works with.
    if(hex.size()==0 or (hex.size()%2)!=0 or hex.size()>8)
      {
        return false;
      }

    try
      {
        value = static_cast<uint32_t>(std::stoul(hex, NULL, 16));
      }
    catch(const std::exception& exc)
      {
        return false;
      }

    n_bytes = static_cast<int>(hex.size()/2);
    return true;
  }

  void font_cid::read_cmap2cid(std::string filename)
  {
    std::set<std::string> visited;
    read_cmap2cid(filename, visited, true);
  }

  void font_cid::read_cmap2cid(std::string            filename,
                               std::set<std::string>& visited,
                               bool                   required)
  {
    LOG_S(INFO) << __FUNCTION__ << "\t filename: " << filename;

    if(visited.count(filename)==1)
      {
        LOG_S(WARNING) << "skipping already visited cmap-resource: " << filename;
        return;
      }
    visited.insert(filename);

    std::ifstream file(filename.c_str());

    if(file.fail())
      {
	std::stringstream ss;
	ss << "filename does not exists: " << filename;

	LOG_S(ERROR) << ss.str();

        // A missing `usecmap` parent costs us its inherited entries, but the
        // entries this file does define are still worth having.
        if(not required) { return; }

	throw std::logic_error(ss.str());
      }

    // The blocks this decoder reads. `notdefrange` is deliberately absent: it
    // maps unmapped codes onto a substitute glyph, which is a rendering
    // decision and never text.
    enum cmap_block { NO_BLOCK, CODESPACE_BLOCK, CIDRANGE_BLOCK, CIDCHAR_BLOCK };

    cmap_block  block = NO_BLOCK;
    std::string line;
    while(std::getline(file, line))
      {
        if(line.find("begincodespacerange")!=std::string::npos)
          {
            block = CODESPACE_BLOCK;
            continue;
          }
        else if(line.find("begincidrange")!=std::string::npos)
          {
            block = CIDRANGE_BLOCK;
            continue;
          }
        else if(line.find("begincidchar")!=std::string::npos)
          {
            block = CIDCHAR_BLOCK;
            continue;
          }
        else if(line.find("endcodespacerange")!=std::string::npos or
                line.find("endcidrange")     !=std::string::npos or
                line.find("endcidchar")      !=std::string::npos)
          {
            block = NO_BLOCK;
            continue;
          }

        std::vector<std::string> items = split(line, ' ');

        auto itr = std::remove(items.begin(), items.end(), "");
        items.erase(itr, items.end());

        if(block==NO_BLOCK)
          {
            // `/Parent usecmap` makes this CMap an override of another one
            // (ISO 32000-1, 9.7.5.2): the parent supplies the codespace and
            // every mapping this file does not restate. Reading the parent
            // here -- the operator always precedes the blocks -- gives the
            // entries below the last word, which is exactly the precedence
            // the operator defines.
            if(items.size()==2 and items[1]=="usecmap")
              {
                read_cmap2cid(sibling_cmap_path(filename, items[0]),
                              visited, false);
              }

            continue;
          }

        if(block==CODESPACE_BLOCK)
          {
            cmap_codespace_range range;
            int n_bytes_high = 0;

            if(items.size()==2 and
               parse_hex_token(items[0], range.low , range.n_bytes) and
               parse_hex_token(items[1], range.high, n_bytes_high)  and
               range.n_bytes==n_bytes_high)
              {
                codespaces.push_back(range);
              }
            else
              {
                LOG_S(ERROR) << "ignoring malformed codespace-range: " << line;
              }

            continue;
          }

        if(block==CIDRANGE_BLOCK)
          {
            uint32_t beg = 0, end = 0;
            int      n_beg = 0, n_end = 0;

            if(items.size()==3 and
               parse_hex_token(items[0], beg, n_beg) and
               parse_hex_token(items[1], end, n_end))
              {
                uint32_t ind = 0;
                try
                  {
                    ind = static_cast<uint32_t>(std::stoul(items[2], NULL, 10));
                  }
                catch(const std::exception& exc)
                  {
                    LOG_S(ERROR) << "ignoring malformed cid-range: " << line;
                    continue;
                  }

                for(uint32_t i=beg; i<=end; i++)
                  {
                    cmap2cid[i] = ind;
                    ind += 1;

                    if(i==std::numeric_limits<uint32_t>::max()) { break; }
                  }
              }
            else
              {
                LOG_S(ERROR) << "ignoring malformed cid-range: " << line;
              }

            continue;
          }

        // CIDCHAR_BLOCK: `<code> cid`, the single-code form of a cid-range
        if(items.size()==2)
          {
            uint32_t code = 0;
            int      n_code = 0;

            if(parse_hex_token(items[0], code, n_code))
              {
                try
                  {
                    cmap2cid[code] = static_cast<uint32_t>(std::stoul(items[1], NULL, 10));
                  }
                catch(const std::exception& exc)
                  {
                    LOG_S(ERROR) << "ignoring malformed cid-char: " << line;
                  }

                continue;
              }
          }

        LOG_S(ERROR) << "ignoring malformed cid-char: " << line;
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

                    if(vec.size()>0)
                      {
                        // A CID that maps to several code points lists the
                        // compatibility form first for a number of characters:
                        // CID 3821 is `2f46,65e0`, and U+2F46 is KANGXI RADICAL
                        // WITHOUT, not the ideograph 无 (U+65E0). They draw
                        // alike, so taking the first went unnoticed on the
                        // page while the extracted text carried a radical that
                        // no search matches. Prefer a real character.
                        const std::string& pick = prefer_character(vec);

                        cid2utf8[cid] = utils::string::hex_to_utf8(pick, 4);
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

  void font_cid::decode_widths(std::unordered_map<uint32_t, double>& numb_to_widths)
  {
    LOG_S(INFO) << __FUNCTION__;

    std::unordered_map<uint32_t, double> numb_to_widths_ = numb_to_widths;
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
