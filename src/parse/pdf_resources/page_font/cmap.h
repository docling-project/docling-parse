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

    std::map<uint32_t, std::string> get() { return _map; }

    void print();

    void parse(std::vector<qpdf_instruction>& instructions);

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

  private:

    uint32_t                      char_count;

    uint32_t                      csr_cnt;
    std::pair<uint32_t, uint32_t> csr_range;

    uint32_t                      bf_cnt;
    std::pair<uint32_t, uint32_t> bf_range;

    std::map<uint32_t, std::string> _map;
  };

  cmap_parser::cmap_parser():
    char_count(0)
  {}

  cmap_parser::~cmap_parser()
  {}

  void cmap_parser::print()
  {
    for(auto itr=_map.begin(); itr!=_map.end(); itr++)
      {
        LOG_S(INFO) << itr->first << "\t" << itr->second;
      }
  }

  void cmap_parser::parse(std::vector<qpdf_instruction>& instructions)
  {
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
                parse_endcodespacerange(parameters);
              }
            else if(item.val=="beginbfrange")
              {
                parse_beginbfrange(parameters);
              }
            else if(item.val=="endbfrange")
              {
                parse_endbfrange(parameters);
              }
            else if(item.val=="beginbfchar")
              {
                parse_beginbfchar(parameters);
              }
            else if(item.val=="endbfchar")
              {
                parse_endbfchar(parameters);
              }
            else
              {
                LOG_S(WARNING) << "cmap ignoring " << item.val << " operator!";
              }

            parameters.clear();
          }
      }
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
    //logging_lib::info("pdf-parser") << __FUNCTION__ << "\n";
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
    //assert(parameters.size()==2*csr_cnt);

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

    for(size_t i=0; i<bf_cnt; i+=1)
      {
        source_start = get_source(parameters[3*i+0].obj);
        source_end   = get_source(parameters[3*i+1].obj);
        target       =            parameters[3*i+2].obj;

        if(target.isString())
          {
	    //LOG_S(INFO) << "source_beg: " << source_start << ", source_end: " << source_end << ": " << target;
	    
            // FIXME we probably need to fix the 2 in the to_utf8(..)
            //std::string tmp = target.getUTF8Value();
            std::string tmp = get_target(target);//to_utf8(target, 2);
	    LOG_S(INFO) << "source_beg: " << source_start << ", source_end: " << source_end << ": " << tmp;

            tmp.erase(std::remove_if(tmp.begin(), tmp.end(), [] (char x) { return x==0;} ), tmp.end());
	    LOG_S(INFO) << "source_beg: " << source_start << ", source_end: " << source_end << ": " << tmp;
	    
            set_range(source_start, source_end, tmp);
          }
        else if(target.isArray())
          {
            std::vector<QPDFObjectHandle> tmps = target.getArrayAsVector();

            std::vector<std::string> target_strs;

            for(QPDFObjectHandle tmp: tmps)
              {
                // FIXME we probably need to fix the 2 in the to_utf8(..)
                //std::string tgt = tmp.getUTF8Value();
                std::string tgt = get_target(tmp);

                tgt.erase(std::remove_if(tgt.begin(), tgt.end(), [] (char x) { return x==0; }), tgt.end());

                target_strs.push_back(tgt);
              }

	    LOG_S(INFO) << "source_beg: " << source_start << ", source_end: " << source_end << ": ";
	    for(auto _:target_strs)
	      {
		LOG_S(INFO) << " => " << _;
	      }
	    
            set_range(source_start, source_end, target_strs);
          }
        else
          {
            LOG_S(WARNING) << "could not interprete the target";
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

    if(itr_beg!=src_begin.end())
      {
        LOG_S(WARNING) << "itr_beg!=src_begin.end() --> errors might occur in the cmap: "
                       << "'" << src_begin << "' -> " << begin;
      }

    auto itr_end = src_end.begin();
    uint32_t end = utf8::next(itr_end, src_end.end());

    if(itr_end!=src_end.end())
      {
        LOG_S(WARNING) << "itr_end!=src_end.end() --> errors might occur in the cmap: "
                       << "'" << src_end << "' -> " << end;;
      }

    LOG_S(INFO) << __FUNCTION__ << "\t"
		<< "beg: " << begin << ", "
		<< "end: " << end;
    
    std::string mapping(tgt);
    std::vector<uint32_t> tgts;
    
    auto itr_tgt = tgt.begin();
    while(itr_tgt!=tgt.end())
      {
        uint32_t tmp = utf8::next(itr_tgt, tgt.end());
        tgts.push_back(tmp);
      }

    if(mapping=="")
      {
        for(uint32_t i = 0; i < end - begin + 1; i++)
          {
            //assert(csr_range.first<=begin+i and begin+i<=csr_range.second);

            if(csr_range.first<=begin+i and begin+i<=csr_range.second)
              {
                try
                  {
                    std::string tmp(128, 0);
                    {
                      auto itr = tmp.begin();
                      itr = utf8::append(begin+i, itr);

                      tmp.erase(itr, tmp.end());
                    }

                    if(_map.count(begin+i)==1)
                      {
                        LOG_S(WARNING) << "overwriting number c=" << begin+i;
                      }

		    if(utf8::is_valid(tmp.begin(), tmp.end()))
		      {
			LOG_S(INFO) << "cmap-ind:" << (begin+i) << " -> target: " << tmp;
			_map[begin + i] = tmp;
		      }
		    else
		      {
			LOG_S(WARNING) << "invalid utf8 string -> iteration: " << (begin+i);
			_map[begin + i] = "UNICODE<"+std::to_string(begin+i)+">";
		      }
                  }
                catch(const std::exception& exc)
                  {
                    LOG_S(WARNING) << "invalid utf8 string: " << exc.what() << " -> iteration: " << (begin+i);

                    _map[begin + i] = "UNICODE<"+std::to_string(begin+i)+">";
                  }
              }
            else
              {
                LOG_S(WARNING) << "index " << begin+i << " is out of bounds ["
                               << csr_range.first << ", " << csr_range.second << "]";
              }
          }
      }
    else
      {
        for(uint32_t i = 0; i < end - begin + 1; i++)
          {
            //assert(csr_range.first<=begin+i and begin+i<=csr_range.second);

            if(csr_range.first<=begin+i and begin+i<=csr_range.second)
              {
                try
                  {
                    std::string tmp(128, 0);
                    {
                      auto itr = tmp.begin();
                      for(auto tgt_uint: tgts)
                        {
                          itr = utf8::append(tgt_uint, itr);
                        }
                      tmp.erase(itr, tmp.end());
                    }

                    if(_map.count(begin+i)==1)
                      {
                        LOG_S(WARNING) << "overwriting number c=" << begin+i;
                      }

                    //_map[begin + i] = tmp;
		    if(utf8::is_valid(tmp.begin(), tmp.end()))
		      {
			LOG_S(INFO) << "cmap-ind:" << (begin+i) << " -> target: " << tmp;
			_map[begin + i] = tmp;
		      }
		    else
		      {
			LOG_S(WARNING) << "invalid utf8 string -> iteration: " << (begin+i);
			_map[begin + i] = "UNICODE<"+std::to_string(begin+i)+">";
		      }		    
                  }
                catch(const std::exception& exc)
                  {
                    LOG_S(WARNING) << "invalid utf8 string: " << exc.what();

                    _map[begin + i] = "UNICODE<"+std::to_string(begin+i)+">";
                  }
              }
            else
              {
		LOG_S(ERROR) << "index " << begin+i << " is out of bounds ["
			     << csr_range.first << ", " << csr_range.second << "]";
              }

            tgts.back() += 1;
          }
      }

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

    for(uint32_t i = 0; i < end - begin + 1; i++)
      {
        //assert(csr_range.first<=begin+i and begin+i<=csr_range.second);

        if(_map.count(begin+i)==1)
          {
            LOG_S(WARNING) << "overwriting number c=" << begin+i;
          }

	if(i<tgt.size())
	  {
	    LOG_S(INFO) << "cmap-ind:" << (begin+i) << " -> target: " << tgt.at(i);
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
