//-*-C++-*-

#ifndef PDF_PAGE_FONT_CMAP_PARSER_H
#define PDF_PAGE_FONT_CMAP_PARSER_H

namespace pdflib
{

  class cmap_parser
  {

  public:

    cmap_parser();
    ~cmap_parser();

    cmap_value get();

    void print();

    void parse(std::vector<qpdf_instruction>& instructions,
               pdf_timings& timings,
               const std::string& key_root);

  private:

    static uint32_t    to_uint32(QPDFObjectHandle handle);

    static std::string to_utf8(QPDFObjectHandle handle,
                               size_t number_of_chars);

    std::string get_source(QPDFObjectHandle my_handle);
    std::string get_target(QPDFObjectHandle my_handle);

    void parse_cmap_name(std::vector<qpdf_instruction>& parameters);
    void parse_cmap_type(std::vector<qpdf_instruction>& parameters);

    void parse_begincodespacerange(std::vector<qpdf_instruction>& parameters);
    void parse_endcodespacerange(std::vector<qpdf_instruction>& parameters);

    void parse_beginbfrange(std::vector<qpdf_instruction>& parameters);
    void parse_endbfrange(std::vector<qpdf_instruction>& parameters);

    void parse_beginbfchar(std::vector<qpdf_instruction>& parameters);
    void parse_endbfchar(std::vector<qpdf_instruction>& parameters);

    void set_mapping(const std::string src,
                     const std::string tgt);

    void set_range(const std::string src_begin,
                   const std::string src_end,
                   const std::string tgt);

    void set_range(const std::string              src_begin,
                   const std::string              src_end,
                   const std::vector<std::string> tgt);

    // Helper to remove trailing null bytes from a string
    static void remove_trailing_nulls(std::string& str);

    // Helper to populate the map for a range of source codepoints.
    // Detects identity mapping when tgts.size()==1 && tgts[0]==begin (maps i -> i).
    // For non-identity, uses tgts and increments tgts.back() for each iteration.
    //static void populate_range_mapping(uint32_t begin, uint32_t end,
    //                                   std::vector<uint32_t>& tgts,
    //                                   const std::pair<uint32_t, uint32_t>& csr_range,
    //                                   std::unordered_map<uint32_t, std::string>& map,
    //				         bool cache=true);
    void populate_range_mapping(uint32_t begin, uint32_t end,
                                std::vector<uint32_t>& tgts);
    
    // Legacy implementation - kept for comparison, uses mapping=="" check instead of identity detection
    static void populate_range_mapping_legacy(uint32_t begin, uint32_t end,
                                              const std::string& mapping,
                                              std::vector<uint32_t>& tgts,
                                              const std::pair<uint32_t, uint32_t>& csr_range,
                                              std::unordered_map<uint32_t, std::string>& map);

  private:

    uint32_t                      char_count;

    uint32_t                      csr_cnt;
    std::pair<uint32_t, uint32_t> csr_range;

    uint32_t                      bf_cnt;
    std::pair<uint32_t, uint32_t> bf_range;

    std::unordered_map<uint32_t, std::string> _map;

    cmap_value _cmap;
  };

  cmap_parser::cmap_parser():
    char_count(0)
  {}

  cmap_parser::~cmap_parser()
  {}

  cmap_value cmap_parser::get()
  {
    return _cmap;
  }

  void cmap_parser::print()
  {
    for(auto itr=_map.begin(); itr!=_map.end(); itr++)
      {
        LOG_S(INFO) << itr->first << "\t" << itr->second;
      }
  }

  void cmap_parser::parse(std::vector<qpdf_instruction>& instructions,
                          pdf_timings& timings,
                          const std::string& key_root)
  {
    utils::timer total_timer;

    std::vector<qpdf_instruction> parameters;

    for(auto& item:instructions)
      {
	//LOG_S(INFO) << item.key << ": " << item.val;

        if(item.key!="operator")
          {
            parameters.push_back(item);
          }
        else
          {
	    LOG_S(INFO) << item.key << ": " << item.val;

            if(item.val=="CMapName")
              {
                parse_cmap_name(parameters);
              }
            else if(item.val=="CMapType")
              {
                parse_cmap_type(parameters);
              }
            else if(item.val=="begincodespacerange")
              {
                parse_begincodespacerange(parameters);
              }
            else if(item.val=="endcodespacerange")
              {
                utils::timer op_timer;
                parse_endcodespacerange(parameters);
                timings.add_timing(key_root + pdf_timings::KEY_CMAP_PARSE_ENDCODESPACERANGE, op_timer.get_time());
              }
            else if(item.val=="beginbfrange")
              {
                parse_beginbfrange(parameters);
              }
            else if(item.val=="endbfrange")
              {
                utils::timer op_timer;
                parse_endbfrange(parameters);
                timings.add_timing(key_root + pdf_timings::KEY_CMAP_PARSE_ENDBFRANGE, op_timer.get_time());
              }
            else if(item.val=="beginbfchar")
              {
                parse_beginbfchar(parameters);
              }
            else if(item.val=="endbfchar")
              {
                utils::timer op_timer;
                parse_endbfchar(parameters);
                timings.add_timing(key_root + pdf_timings::KEY_CMAP_PARSE_ENDBFCHAR, op_timer.get_time());
              }
            else
              {
                LOG_S(WARNING) << "cmap ignoring " << item.val << " operator!";
              }

            parameters.clear();
          }
      }

    // If identity was not set during populate_range_mapping, construct from _map
    if(not _cmap.is_identity())
      {
        _cmap = cmap_value(std::move(_map));
      }

    timings.add_timing(key_root + pdf_timings::KEY_CMAP_PARSE_TOTAL, total_timer.get_time());
  }

  uint32_t cmap_parser::to_uint32(QPDFObjectHandle handle)
  {
    std::string unparsed = handle.unparse();

    // we have a hex-string ...
    if(unparsed.size()>0 and
       unparsed.front()=='<' and
       unparsed.back()=='>')
      {
        unparsed = unparsed.substr(1, unparsed.size()-2);

        // we go from hex to number
        uint32_t result = std::stoul(unparsed, NULL, 16);
        return result;
      }
    else
      {
        uint32_t result=0;

        std::string tmp = handle.getStringValue();
        for(size_t i=0; i<tmp.size(); i+=1)
          {
            result = (result << 8) + static_cast<unsigned char>(tmp.at(i));
          }

        return result;
      }
  }

  std::string cmap_parser::to_utf8(QPDFObjectHandle handle,
                                   size_t number_of_chars)
  {
    if(not handle.isString())
      {
        std::string message = "not handle.isString()";
        LOG_S(ERROR) << message;
        throw std::logic_error(message);
      }

    std::string unparsed = handle.unparse();
    LOG_S(INFO) << " unparsed: '" << unparsed << "'";

    // FIXME this might be too short
    std::string result(64, ' ');

    // we have a hex-string ...
    if(unparsed.size()>0     and
       unparsed.front()=='<' and
       unparsed.back() =='>'   )
      {
        //logging_lib::info("pdf-parser") << "we have a hex-string ...\n";
        unparsed = unparsed.substr(1, unparsed.size()-2);

        std::vector<uint32_t> utf16_vec;
        for(size_t i=0; i<unparsed.size(); i+=2*number_of_chars)
          {
            std::string tmp = unparsed.substr(i, 2 * number_of_chars);
            uint32_t i32    = std::stoul(tmp, NULL, 16);
            utf16_vec.push_back(i32);
          }

        try
          {
            auto itr = utf8::utf16to8(utf16_vec.begin(), utf16_vec.end(), result.begin());
            result.erase(itr, result.end());

            //logging_lib::success("pdf-parser") << "SUCCES: able to parse the unicode hex-string \""
            //<< unparsed << "\" --> " << result;
          }
        catch(...)
          {
            LOG_S(ERROR) << "Not able to parse the unicode hex-string \""
                         << unparsed << "\"";

            result = "GLYPH(cmap:" + unparsed + ")";
          }
      }
    else
      {
        std::string tmp = handle.getStringValue();

        auto itr = result.begin();
        for(size_t i=0; i<tmp.size(); i+=number_of_chars)
          {
            uint32_t i32=0;

            for(size_t j=0; j<number_of_chars; j+=1)
	      {
		i32 = (i32 << 8) + static_cast<unsigned char>(tmp.at(i+j));
	      }
	    
            try
              {
                itr = utf8::append(i32, itr);
              }
            catch(...)
              {
                LOG_S(ERROR) << "Not able to parse the unicode string \""
                             << tmp << "\" --> " << i32;
              }
          }

        result.erase(itr, result.end());
      }

    return result;
  }

  void cmap_parser::remove_trailing_nulls(std::string& str)
  {
    /* Legacy */
    // str.erase(std::remove_if(str.begin(), str.end(), [] (char x) { return x==0; }), str.end());

    // Remove only trailing null bytes (not all nulls)
    while(not str.empty() && str.back() == '\0')
      {
        str.pop_back();
      }
    // If string became empty, it was all nulls - preserve as single null
    if(str.empty())
      {
        str = std::string(1, '\0');
      }
  }

  // Legacy: static version with caching
  //void cmap_parser::populate_range_mapping(uint32_t begin, uint32_t end,
  //                                         std::vector<uint32_t>& tgts,
  //                                         const std::pair<uint32_t, uint32_t>& csr_range,
  //                                         std::unordered_map<uint32_t, std::string>& map,
  //                                         bool cache)

  void cmap_parser::populate_range_mapping(uint32_t begin, uint32_t end,
                                           std::vector<uint32_t>& tgts)
  {
    if(begin==0 and
       end==65535 and
       csr_range.first==0 and
       csr_range.second==65535 and
       tgts.size()==1 and tgts.at(0)==0)
      {
        // Identity mapping detected: cmap_value will compute UTF-8 on the fly
        LOG_S(INFO) << "identity mapping detected, using cmap_value identity mode";
        _cmap = cmap_value(true, csr_range, {});
        return;
      }

    // Non-identity: populate _map entry by entry
    bool is_identity = (tgts.size() == 1 && tgts[0] == begin);

    LOG_S(INFO) << "populate_range_mapping: begin=" << begin << ", end=" << end
                << ", tgts.size()=" << tgts.size()
                << ", is_identity=" << is_identity;

    for(uint32_t i = 0; i < end - begin + 1; i++)
      {
        uint32_t src_codepoint = begin + i;

        if(not (csr_range.first <= src_codepoint and src_codepoint <= csr_range.second))
          {
            if(is_identity)
              {
                LOG_S(WARNING) << "index " << src_codepoint << " is out of bounds ["
                               << csr_range.first << ", " << csr_range.second << "]";
              }
            else
              {
                LOG_S(ERROR) << "index " << src_codepoint << " is out of bounds ["
                             << csr_range.first << ", " << csr_range.second << "]";
              }

            if(not is_identity)
              {
                tgts.back() += 1;
              }
            continue;
          }

        try
          {
            std::string tmp(128, 0);
            {
              auto itr = tmp.begin();
              if(is_identity)
                {
                  itr = utf8::append(src_codepoint, itr);
                }
              else
                {
                  for(auto tgt_uint : tgts)
                    {
                      itr = utf8::append(tgt_uint, itr);
                    }
                }
              tmp.erase(itr, tmp.end());
            }

            if(_map.count(src_codepoint) == 1)
              {
                LOG_S(WARNING) << "overwriting number c=" << src_codepoint;
              }

            if(utf8::is_valid(tmp.begin(), tmp.end()))
              {
                _map[src_codepoint] = tmp;
              }
            else
              {
                LOG_S(WARNING) << "invalid utf8 string -> iteration: " << src_codepoint;
                _map[src_codepoint] = "UNICODE<" + std::to_string(src_codepoint) + ">";
              }
          }
        catch(const std::exception& exc)
          {
            LOG_S(WARNING) << "invalid utf8 string: " << exc.what() << " -> iteration: " << src_codepoint;
            _map[src_codepoint] = "UNICODE<" + std::to_string(src_codepoint) + ">";
          }

        if(not is_identity)
          {
            tgts.back() += 1;
          }
      }
  }

  // FIXME: not used code, just reference still ...
  void cmap_parser::populate_range_mapping_legacy(uint32_t begin, uint32_t end,
                                                  const std::string& mapping,
                                                  std::vector<uint32_t>& tgts,
                                                  const std::pair<uint32_t, uint32_t>& csr_range,
                                                  std::unordered_map<uint32_t, std::string>& map)
  {
    // Legacy implementation using mapping=="" check (likely dead code path)
    // Kept for comparison with populate_range_mapping which uses identity detection

    if(mapping == "")
      {
        for(uint32_t i = 0; i < end - begin + 1; i++)
          {
            if(csr_range.first <= begin + i and begin + i <= csr_range.second)
              {
                try
                  {
                    std::string tmp(128, 0);
                    {
                      auto itr = tmp.begin();
                      itr = utf8::append(begin + i, itr);

                      tmp.erase(itr, tmp.end());
                    }

                    if(map.count(begin + i) == 1)
                      {
                        LOG_S(WARNING) << "overwriting number c=" << begin + i;
                      }

                    if(utf8::is_valid(tmp.begin(), tmp.end()))
                      {
                        //LOG_S(INFO) << "cmap-ind:" << (begin+i) << " -> target: " << tmp;
                        map[begin + i] = tmp;
                      }
                    else
                      {
                        LOG_S(WARNING) << "invalid utf8 string -> iteration: " << (begin + i);
                        map[begin + i] = "UNICODE<" + std::to_string(begin + i) + ">";
                      }
                  }
                catch(const std::exception& exc)
                  {
                    LOG_S(WARNING) << "invalid utf8 string: " << exc.what() << " -> iteration: " << (begin + i);

                    map[begin + i] = "UNICODE<" + std::to_string(begin + i) + ">";
                  }
              }
            else
              {
                LOG_S(WARNING) << "index " << begin + i << " is out of bounds ["
                               << csr_range.first << ", " << csr_range.second << "]";
              }
          }
      }
    else
      {
        LOG_S(ERROR) << begin << ", "
                     << end << ", "
                     << csr_range.first << ", "
                     << csr_range.second << ", "
                     << tgts.at(0) << ", " << tgts.size();

        for(uint32_t i = 0; i < end - begin + 1; i++)
          {
            if(csr_range.first <= begin + i and begin + i <= csr_range.second)
              {
                try
                  {
                    std::string tmp(128, 0);
                    {
                      auto itr = tmp.begin();
                      for(auto tgt_uint : tgts)
                        {
                          itr = utf8::append(tgt_uint, itr);
                        }
                      tmp.erase(itr, tmp.end());
                    }

                    if(map.count(begin + i) == 1)
                      {
                        LOG_S(WARNING) << "overwriting number c=" << begin + i;
                      }

                    //map[begin + i] = tmp;
                    if(utf8::is_valid(tmp.begin(), tmp.end()))
                      {
                        // LOG_S(INFO) << "cmap-ind:" << (begin+i) << " -> target: " << tmp;
                        map[begin + i] = tmp;
                      }
                    else
                      {
                        LOG_S(WARNING) << "invalid utf8 string -> iteration: " << (begin + i);
                        map[begin + i] = "UNICODE<" + std::to_string(begin + i) + ">";
                      }
                  }
                catch(const std::exception& exc)
                  {
                    LOG_S(WARNING) << "invalid utf8 string: " << exc.what();

                    map[begin + i] = "UNICODE<" + std::to_string(begin + i) + ">";
                  }
              }
            else
              {
                LOG_S(ERROR) << "index " << begin + i << " is out of bounds ["
                             << csr_range.first << ", " << csr_range.second << "]";
              }

            tgts.back() += 1;
          }

        LOG_S(ERROR) << begin << ", "
                     << end << ", "
                     << csr_range.first << ", "
                     << csr_range.second << ", "
                     << tgts.at(0) << ", " << tgts.size() << "\t => Done!";
      }
  }

  void cmap_parser::parse_cmap_name(std::vector<qpdf_instruction>& parameters)
  {
    LOG_S(WARNING) << __FUNCTION__ << ": skipping ...";
  }

  void cmap_parser::parse_cmap_type(std::vector<qpdf_instruction>& parameters)
  {
    LOG_S(WARNING) << __FUNCTION__ << ": skipping ...";
  }

  void cmap_parser::parse_begincodespacerange(std::vector<qpdf_instruction>& parameters)
  {
    LOG_S(INFO) << __FUNCTION__;

    const int num_params = 1;
    if(parameters.size()<num_params)
      {
        std::string message = "parameters.size() < " + std::to_string(num_params);
        LOG_S(ERROR) << message;

        throw std::logic_error(message);
      }
    else if(parameters.size()>num_params)
      {
        LOG_S(WARNING) << "parameters.size() > " << num_params;
      }
    
    csr_cnt = parameters[0].to_int();

    LOG_S(INFO) << __FUNCTION__ << " csr_cnt: " << csr_cnt;
  }

  void cmap_parser::parse_endcodespacerange(std::vector<qpdf_instruction>& parameters)
  {
    LOG_S(INFO) << __FUNCTION__;

    const int num_params = 2;
    if(parameters.size()<num_params)
      {
        std::string message = "parameters.size() < " + std::to_string(num_params);
        LOG_S(ERROR) << message;
        throw std::logic_error(message);
      }
    else if(parameters.size()>num_params)
      {
        LOG_S(WARNING) << "parameters.size() > " << num_params;
      }

    csr_range.first  = cmap_parser::to_uint32(parameters[0].obj);
    csr_range.second = cmap_parser::to_uint32(parameters[1].obj);

    LOG_S(INFO) << parameters[0].obj.unparse() << "\t" << csr_range.first;
    LOG_S(INFO) << parameters[1].obj.unparse() << "\t" << csr_range.second;
  }

  void cmap_parser::parse_beginbfrange(std::vector<qpdf_instruction>& parameters)
  {
    LOG_S(INFO) << __FUNCTION__;

    const int num_params = 1;
    if(parameters.size()<num_params)
      {
        std::string message = "parameters.size() < " + std::to_string(num_params);
        LOG_S(ERROR) << message;

        throw std::logic_error(message);
      }
    else if(parameters.size()>num_params)
      {
        LOG_S(WARNING) << "parameters.size() > " << num_params;
      }

    bf_cnt = parameters[0].to_int();
    LOG_S(INFO) << __FUNCTION__ << " bf_cnt: " << bf_cnt;
  }
  
  // the source can be 1 or 2 byte
  std::string cmap_parser::get_source(QPDFObjectHandle my_handle)
  {
    std::string source = my_handle.getStringValue();

    std::string result="";
    {
      std::string tmp = my_handle.getStringValue();
      result = to_utf8(my_handle, tmp.size());
    }
    //LOG_S(INFO) << __FUNCTION__ << my_handle.unparse() << "\t" << source << "\t'" << result << "'";

    return result;
  }

  // the target is always 2 byte
  std::string cmap_parser::get_target(QPDFObjectHandle my_handle)
  {
    //for(int i=0; i<4; i++)
    //{
    //LOG_S(INFO) << __FUNCTION__ << "\t" << i << ": " << int(my_handle[i]);
    //}
    
    std::string target = to_utf8(my_handle, 2);

    //{
    //std::string _ = my_handle.getStringValue();    
    //LOG_S(INFO) << __FUNCTION__ << "\t" << _ << "\t" << _.size() << "\t" << target;
    //}
    
    return target;
  }

  void cmap_parser::parse_endbfrange(std::vector<qpdf_instruction>& parameters)
  {
    LOG_S(INFO) << __FUNCTION__;
    
    const int num_params = 3*bf_cnt;
    if(parameters.size()==0)
      {
	LOG_S(ERROR) << "skipping " << __FUNCTION__;
	return;
      }
    else if(parameters.size()<num_params)
      {
	std::stringstream ss;
	ss << "bf_cnt: " << bf_cnt << ", len(parameters): " << parameters.size();	
        //std::string message = "parameters.size() < " + std::to_string(num_params);
	
        LOG_S(ERROR) << ss.str();
        throw std::logic_error(ss.str());
      }
    else if(parameters.size()>num_params)
      {
        LOG_S(WARNING) << "parameters.size(): " << parameters.size() << " > " << num_params;
      }

    std::string source_start = "";
    std::string source_end = "";

    QPDFObjectHandle target;

    // LOG_S(ERROR) << __FUNCTION__ << "\t total cnt: " << bf_cnt;
    
    for(size_t i=0; i<bf_cnt; i+=1)
      {
        source_start = get_source(parameters[3*i+0].obj);
        source_end   = get_source(parameters[3*i+1].obj);
        target       =            parameters[3*i+2].obj;

        if(target.isString())
          {
	    //LOG_S(ERROR) << "source_beg: " << source_start << ", source_end: " << source_end << ": " << target;
	    
            // FIXME we probably need to fix the 2 in the to_utf8(..)
            //std::string tgt = target.getUTF8Value();
            std::string tgt = get_target(target);//to_utf8(target, 2);

	    //LOG_S(INFO) << "source_beg: " << source_start.size() << ", source_end: " << source_end.size()
	    //<< " tgt: " << tgt.size()
	    //<< " source_start==tgt: " << (source_start==tgt);

            remove_trailing_nulls(tgt);

	    //LOG_S(INFO) << "source_beg: " << source_start.size() << ", source_end: " << source_end.size()
	    //<< " tgt: " << tgt.size()
	    //<< " source_start==tgt: " << (source_start==tgt);

            set_range(source_start, source_end, tgt);
          }
        else if(target.isArray())
          {
            std::vector<QPDFObjectHandle> tgts = target.getArrayAsVector();

            std::vector<std::string> target_strs;

            for(QPDFObjectHandle tgt_: tgts)
              {
                // FIXME we probably need to fix the 2 in the to_utf8(..)
                //std::string tgt = tgt.getUTF8Value();
                std::string tgt = get_target(tgt_);

                remove_trailing_nulls(tgt);

                target_strs.push_back(tgt);
              }

	    //LOG_S(INFO) << " -> source_beg: " << source_start << ", source_end: " << source_end << ": ";
	    //for(auto _:target_strs)
	    //{
	    //LOG_S(INFO) << " => " << _;
	    //}
	    
            set_range(source_start, source_end, target_strs);
          }
        else
          {
            LOG_S(ERROR) << "could not interprete the target";
          }
      }
  }

  void cmap_parser::set_mapping(const std::string src,
                                const std::string tgt)
  {
    //LOG_S(INFO) << __FUNCTION__ << ": " << src << " -> " << tgt;

    uint32_t c = 0;
    {
      auto itr = src.begin();
      c = utf8::next(itr, src.end());
    }

    if(not (csr_range.first<=c and c<=csr_range.second))
      {
        LOG_S(ERROR) << c << " is going out of bounds: " << csr_range.first << "," << csr_range.second;
      }

    if(_map.count(c)==1)
      {
        LOG_S(ERROR) << "overwriting number cmap[" << c << "]: " << _map.at(c) << " with " << tgt;
      }

    LOG_S(INFO) << "orig: " << src << " -> cmap-ind:" << c << " -> target: " << tgt;
    _map[c] = tgt;
  }

  void cmap_parser::set_range(const std::string src_begin,
                              const std::string src_end,
                              const std::string tgt)
  {
    //LOG_S(INFO) << __FUNCTION__;

    auto itr_beg = src_begin.begin();
    uint32_t begin = utf8::next(itr_beg, src_begin.end());

    if(itr_beg != src_begin.end())
      {
        LOG_S(WARNING) << "itr_beg!=src_begin.end() --> errors might occur in the cmap: "
                       << "'" << src_begin << "' -> " << begin;
      }

    auto itr_end = src_end.begin();
    uint32_t end = utf8::next(itr_end, src_end.end());

    if(itr_end != src_end.end())
      {
        LOG_S(WARNING) << "itr_end!=src_end.end() --> errors might occur in the cmap: "
                       << "'" << src_end << "' -> " << end;;
      }

    //LOG_S(INFO) << __FUNCTION__ << "\t"
    //<< "beg: " << begin << ", "
    //<< "end: " << end << "\t tgt: `" << tgt << "` with size: " << tgt.size();

    // Parse target string into codepoints
    std::string mapping(tgt);
    std::vector<uint32_t> tgts;

    auto itr_tgt = tgt.begin();
    while(itr_tgt != tgt.end())
      {
        uint32_t tmp = utf8::next(itr_tgt, tgt.end());
        tgts.push_back(tmp);
      }

    //LOG_S(INFO) << __FUNCTION__ << "\t"
    //<< "len(tgts): " << tgts.size() << "\t end - begin + 1: " << (end - begin + 1);

    // Pre-reserve capacity to avoid rehashing during bulk insertions
    _map.reserve(_map.size() + (end - begin + 1));

    // New implementation with cmap_value identity detection
    populate_range_mapping(begin, end, tgts);

    // Legacy implementations:
    //populate_range_mapping(begin, end, tgts, csr_range, _map);
    //populate_range_mapping_legacy(begin, end, mapping, tgts, csr_range, _map);
  }

  void cmap_parser::set_range(const std::string src_begin,
                              const std::string src_end,
                              const std::vector<std::string> tgt)
  {
    //LOG_S(INFO) << __FUNCTION__;

    auto itr_begin = src_begin.begin();
    uint32_t begin = utf8::next(itr_begin, src_begin.end());

    auto itr_end = src_end.begin();
    uint32_t end = utf8::next(itr_end, src_end.end());

    // Pre-reserve capacity to avoid rehashing during bulk insertions
    _map.reserve(_map.size() + (end - begin + 1));

    for(uint32_t i = 0; i < end - begin + 1; i++)
      {
        //assert(csr_range.first<=begin+i and begin+i<=csr_range.second);

        if(_map.count(begin+i)==1)
          {
            LOG_S(WARNING) << "overwriting number c=" << begin+i;
          }

	if(i<tgt.size())
	  {
	    // LOG_S(INFO) << "cmap-ind:" << (begin+i) << " -> target: " << tgt.at(i);
	    _map[begin + i] = tgt.at(i);
	  }
	else
	  {
	    std::stringstream ss;
	    ss << "out of bounds: " << i << " >= " << tgt.size()
	       << ", beg: " << begin
	       << ", end: " << end;  
	    
	    LOG_S(ERROR) << ss.str();
	    break;
	  }
      }
  }

  void cmap_parser::parse_beginbfchar(std::vector<qpdf_instruction>& parameters)
  {
    LOG_S(INFO) << __FUNCTION__;

    if(parameters.size()==1)
      {
        char_count = parameters[0].to_int();
      }
    else if(parameters.size()>0)
      {
        LOG_S(WARNING) << "parameters.size()>0 for parse_beginbfchar";
        char_count = parameters[0].to_int();
      }
    else
      {
        LOG_S(ERROR) << "parameters.size()!=1 for parse_beginbfchar";
      }
  }

  void cmap_parser::parse_endbfchar(std::vector<qpdf_instruction>& parameters)
  {
    LOG_S(INFO) << __FUNCTION__ << ": starting ...";

    if(parameters.size()!=2*char_count)
      {
        LOG_S(WARNING) << "parameters.size()!=2*char_count -> "
                       << "parameters: " << parameters.size() << ", "
                       << "char_count: " << char_count;
      }
    //assert(parameters.size()==2*char_count);

    for(size_t i=0; i<char_count; i++)
      {
        if(2*i>=parameters.size())
          {
            LOG_S(ERROR) << "going out of bounds: skipping parse_endbfchar";
            continue;
          }

        QPDFObjectHandle source_ = parameters[2*i+0].obj;
        QPDFObjectHandle target_ = parameters[2*i+1].obj;

        std::string source = get_source(source_);
        std::string target = get_target(target_);

        target.erase(std::remove_if(target.begin(), target.end(), [] (char x) { return x == 0; }),
                     target.end());

        set_mapping(source, target);
      }
  }

}

#endif
