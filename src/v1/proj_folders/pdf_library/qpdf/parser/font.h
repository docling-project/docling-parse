//-*-C++-*-

#ifndef PDF_LIBRARY_QPDF_PARSER_FONT_H
#define PDF_LIBRARY_QPDF_PARSER_FONT_H

#include <assert.h>
#include <map>
#include <string>

namespace pdf_lib
{
  namespace qpdf
  {
    template<>
    class parser<core::FONT> :
      public core::parser<core::FONT>
    {
    public:

      parser(QPDFObjectHandle          handle,
             core::object<core::FONT>& font);

      virtual void parse();

    private:

      std::string parse_name   (QPDFObjectHandle _handle);
      std::string parse_subtype(QPDFObjectHandle _handle);

      void parse_cmap(QPDFObjectHandle _handle);

      void parse_cmap_resources(QPDFObjectHandle _handle);

      void parse_differences(QPDFObjectHandle _handle);

      void parse_encoding(QPDFObjectHandle _handle);

      void parse_metrics();

      void parse_metrics_(std::string cmap_name);

      void parse_metrics_(std::string cmap_name,
			  core::object<core::FONT>::font_metric_type& fm);

      void parse_metrics_widths(std::vector<QPDFObjectHandle>& W,
                                std::map<uint32_t, int>&       widths);

      std::string normalize_font_name(std::string const& name);

    private:

      //bool verbose = true;

      QPDFObjectHandle _handle;
    };

    parser<core::FONT>::parser(QPDFObjectHandle          handle,
                               core::object<core::FONT>& font) :
      core::parser<core::FONT>(font),
      _handle(handle)
    {
    }

    void parser<core::FONT>::parse()
    {
      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t" << __FUNCTION__;


      if(false)
        {
	  logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__
					  << "\t printing dict of the font";
	  //print_dict(_handle);
        }


      // find the name of the font
      {
        object().get_name()    = parse_name   (_handle);
        object().get_subtype() = parse_subtype(_handle);
      }

      if(not _handle.hasKey("/ToUnicode") and
         not _handle.hasKey("/Encoding") and
         not (_handle.getKey("/Subtype").getName() == "/CIDFontType0" or
              _handle.getKey("/Subtype").getName() == "/CIDFontType2"))
        {
          //print_dict(_handle);
          logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__
                                          << "\tFont has no encoding: using STD encoding";
          object().set_base_encoding(core::STANDARD);
        }

      // parse the character-mapping or CMAP
      {
        parse_cmap(_handle);
      }

      //
      {
        parse_cmap_resources(_handle);
      }

      // parse the encoding
      {
        parse_encoding(_handle);
      }

      // parse font-metrics
      {
        parse_metrics();

        if(not object().font_metric_is_initialized())
          {
            auto& font_dictionary = core::object<core::FONT>::my_fm_dictionary;

            if(font_dictionary.has_corresponding_font(object().get_name()))
              {
                std::string font_name = font_dictionary.get_corresponding_font(object().get_name());

                logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__
                                                << "\tWARNING: did not initialize the font_metrics for font \""
                                                << object().get_name() << "\""
                                                << " --> using fontmetrics of \"" << font_name << "\" instead";

                object().set_font_metrics(font_name);
              }
            else
              {
                logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__
						<< "\tERROR: did not initialize the font_metrics for font \""
						<< object().get_name() << "\""
						<< " --> using fontmetrics of \"Times-Roman\" instead";

                object().set_font_metrics("/Times-Roman");
              }
          }
      }

      if(false)
        {
          std::stringstream ss;
          object().print(ss);

          logging_lib::success("pdf-parser") << __FILE__ << ":" << __LINE__
                                             << "\n----------- PRINTING THE FONT ---------------\n"
                                             << ss.str()
                                             << "\n---------------------------------------------\n";
        }
    }

    std::string parser<core::FONT>::parse_name(QPDFObjectHandle _handle)
    {
      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t" << __FUNCTION__;

      std::string name="not-defined";

      if(_handle.hasKey("/FontDescriptor"))
        {
          if(_handle.getKey("/FontDescriptor").hasKey("/FontName"))
            {
              name = _handle.getKey("/FontDescriptor").getKey("/FontName").getName();
	      logging_lib::info("pdf-parser")  << __FILE__ << ":" << __LINE__ << "\t"
					       << "registered name: " << name;
            }
          else
            {
              logging_lib::warn("pdf-parser")  << __FILE__ << ":" << __LINE__ << "\t"
                                                << "no fontname";
            }
        }
      else if(_handle.getKey("/Subtype").getName()=="/Type0")
        {
          // reference on page 275, subtitle `9.7 Composite Fonts`

          if(_handle.getKey("/DescendantFonts").getArrayNItems()>0)
            {
              //for(int l=0; l<_handle.getKey("/DescendantFonts").getArrayNItems(); l++)
              //print_dict(_handle.getKey("/DescendantFonts").getArrayItem(l));

              auto new_handle = _handle.getKey("/DescendantFonts").getArrayItem(0);

              if(new_handle.hasKey("/FontDescriptor"))
                {
                  if(new_handle.getKey("/FontDescriptor").hasKey("/FontName"))
                    {
                      name = new_handle.getKey("/FontDescriptor").getKey("/FontName").getName();
                    }
                  else
                    {
                      logging_lib::warn("pdf-parser")  << __FILE__ << ":" << __LINE__ << "\t"
                                                        << "no /FontName in /FontDescriptor for Type0 font";
                    }
                }
              else
                {
                  logging_lib::warn("pdf-parser")  << __FILE__ << ":" << __LINE__ << "\t"
                                                    << "no /FontDescriptor/FontName for Type0 font";
                }
            }
          else
            {
              logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
					      << "qpdf::parser<core::FONT>::parse()";
            }
        }
      else if(_handle.getKey("/Subtype").getName()=="/Type1")
        {
          if(_handle.hasKey("/BaseFont"))
            {
              name = _handle.getKey("/BaseFont").getName();
	      logging_lib::info("pdf-parser")  << __FILE__ << ":" << __LINE__ << "\t"
					       << "registered name: " << name;	      
            }
          else
            {
              logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                               << "no /BaseFont for Type1 font";
            }
        }
      else if(_handle.getKey("/Subtype").getName()=="/Type3")
        {
          // FIXME check on page 253!
          //logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t" << "found no name for Type3 font";
          if(_handle.hasKey("/Name"))
            {
              name = _handle.getKey("/Name").getName();
	      logging_lib::info("pdf-parser")  << __FILE__ << ":" << __LINE__ << "\t"
					       << "registered name: " << name;
            }
          else
            {
              logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                               << " no /BaseFont for Type3 font";
            }
        }
      else if(_handle.getKey("/Subtype").getName()=="/TrueType")
        {
          if(_handle.hasKey("/BaseFont"))
            {
              name = _handle.getKey("/BaseFont").getName();
	      logging_lib::info("pdf-parser")  << __FILE__ << ":" << __LINE__ << "\t"
					       << "registered name: " << name;
            }
          else
            {
              logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                               << "found no name for TrueType font";
            }
        }
      else
        {
          logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                           << " font has no name!";
        }

      name = normalize_font_name(name);

      logging_lib::info("pdf-parser")  << __FILE__ << ":" << __LINE__ << "\t"
				       << "normalised name: " << name;
      
      return name;
    }

    std::string parser<core::FONT>::normalize_font_name(std::string const& name)
    {
      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t" << __FUNCTION__;

      if(utf8::is_valid(name.begin(), name.end()))
        {
          return name;
        }
      else if(name.empty())
        {
          logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                          << "empty font name";
          return "empty-font-name";
        }
      else
        {
          auto itr_0 = name.begin();
          auto itr_1 = utf8::find_invalid(name.begin(), name.end());

          /*
            logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ <<"\tname: " << name;
            for(auto itr_=name.begin(); itr_!=name.end(); itr_++)
            {
            std::cout << "\t" << int(uint8_t(*itr_)) << "\t" << *itr_ << "\t" << (itr_==itr_1) << "\n";
            }
            std::cout << "\n";
          */

          std::vector<uint32_t> utf16_vec;

          int cnt=0;
          while(itr_1!=name.end())
            {
              for(auto itr_=itr_0; itr_!=itr_1; )
                {
                  utf16_vec.push_back(utf8::next(itr_, itr_1));
                  itr_0 = itr_;

                  cnt += 1;
                }


              {
                uint32_t i0 = int(uint8_t(*(itr_0++)));
                uint32_t i1 = int(uint8_t(*(itr_0++)));

                utf16_vec.push_back(255*i0+i1);

                cnt += 2;
              }

              itr_1 = utf8::find_invalid(itr_0, name.end());

              // ensure you do not go out of bounds
              if(cnt>=name.size())
                {
                  break;
                }
            }

          try
            {
              std::string result(256, ' ');

              auto itr = utf8::utf16to8(utf16_vec.begin(), utf16_vec.end(), result.begin());
              result.erase(itr, result.end());

              logging_lib::success("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                                 << "able to parse the unicode hex-string \""
                                                 << name << "\" --> " << result;

              return result;
            }
          catch(...)
            {
              logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                               << "Not able to parse the unicode hex-string \""
                                               << name << "\"";
            }

          // last resort if everything else failed ...
          std::stringstream buffer;
          for (unsigned int i = 0; i < name.length(); ++i)
            {
              unsigned char ch = name.at(i);
              if (ch >= 127)
                {
                  buffer << "#" << std::hex << static_cast<int>(ch);
                }
              else
                {
                  buffer << ch;
                }
            }

          return buffer.str();
        }
    }

    std::string parser<core::FONT>::parse_subtype(QPDFObjectHandle _handle)
    {
      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t" << __FUNCTION__;

      std::string name="not-defined";

      if(_handle.hasKey("/Subtype"))
        {
          name = _handle.getKey("/Subtype").getName();
        }

      return name;
    }

    void parser<core::FONT>::parse_differences(QPDFObjectHandle diff_array)
    {
      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t" << __FUNCTION__;

      std::vector<QPDFObjectHandle> differences = diff_array.getArrayAsVector();

      uint32_t c=0;
      for(size_t i = 0; i<differences.size(); i++)
        {
          QPDFObjectHandle item = differences.at(i);

          if(item.isInteger())
            {
              //std::cout << item.getIntValue() << "\n";
              c = item.getIntValue();
            }
          else if(item.isName())
            {
              //std::cout << "\t" << c << "\t" << item.getName() << "\t" << item.getName().substr(1) << "\n";
	      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
					      << c << "\t" << item.getName() << "\t" << item.getName().substr(1);
	      
              object().set_difference(c++, item.getName().substr(1));
            }
          else
            {
              logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
					      << "ignoring item in _parse_differences";
            }
        }
    }

    void parser<core::FONT>::parse_cmap(QPDFObjectHandle _handle)
    {
      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                      << __FUNCTION__;

      core::object<core::CMAP>& font_cmap = object().get_cmap();

      std::string name = object().get_name();
      //std::cout << "name: " << name << "\n";

      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
				      << "name: " << name;
      
      std::string subtype = object().get_subtype();
      //std::cout << "subtype: " << subtype << "\n";

      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
				      << "subtype: " << subtype;
      
      
      if(_handle.hasKey("/ToUnicode") and
         _handle.getKey("/ToUnicode").isStream())
        {
	  //print_dict(_handle);
	  
          parser<core::CMAP> p(_handle.getKey("/ToUnicode"), font_cmap);
          p.parse();

          //font_cmap.print(std::cout);
        }
    }

    void parser<core::FONT>::parse_cmap_resources(QPDFObjectHandle _handle)
    {
      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                      << __FUNCTION__;

      // only for Type0 fonts
      /*
        if(_handle.hasKey("/Subtype") and
        _handle.getKey("/Subtype").getName() != "/Type0")
        {
        return;
        }
      */

      // if we already found a non-zero cmap, no need to everwrite!
      if(object().get_cmap().size()>0)
	{
	  return;
	}
      
      //print_dict(_handle);

      std::string encoding = "unknown";
      if(_handle.hasKey("/Encoding") and
	 _handle.getKey("/Encoding").isName())
        {
          encoding = _handle.getKey("/Encoding").getName();
        }

      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t";
      
      cid_cmap cids;

      std::string cmap_name = "unknown";

      if(cids.has(encoding)==1)
        {
          cmap_name = encoding;

	  logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
					  << "found cmap_name: " << cmap_name;
        }
      else if(_handle.hasKey("/CIDSystemInfo"))
        {
          logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                          << "parsing CIDSystemInfo";

          std::string ordering, registry;
          int supplement = -1;

          {
            QPDFObjectHandle cid_system_info = _handle.getKey("/CIDSystemInfo");
            //print_dict(cid_system_info);

            registry = cid_system_info.getKey("/Registry").getUTF8Value();
            ordering = cid_system_info.getKey("/Ordering").getUTF8Value();


            supplement = cid_system_info.getKey("/Supplement").getIntValue();
          }

          cmap_name = registry+"-"+ordering+"-"+std::to_string(supplement);
        }
      else
        {
          cmap_name = "unknown";
        }

      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__
				      << "\t cmap_name: " << cmap_name;
      
      if(cmap_name!="unknown")
        {
          std::map<uint32_t, uint32_t> cmap_to_cid;

          cmap_to_cid = cids.load(cmap_name);

          logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                          << "cmap_to_cid: " << cmap_to_cid.size();

          std::string key = cids.get_key(cmap_name);

          logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                          << "key: " << key;

          cid_to_utf8 obj_1;
          std::map<uint32_t, std::string> cid_to_utf8 = obj_1.load(key);

          logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                          << "cid_to_utf8: " << cid_to_utf8.size();

          std::map<uint32_t, std::string> cmap_to_utf8;
          for(auto itr=cmap_to_cid.begin(); itr!=cmap_to_cid.end(); itr++)
            {
              if(cid_to_utf8.count(itr->second)==1)
                {
                  cmap_to_utf8[itr->first] = cid_to_utf8[itr->second];

		  //if(itr->first<64)
		  //{
		  //logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t cmap -> cid -> utf8: "
		  //<< itr->first << "\t" << itr->second << "\t" << cid_to_utf8[itr->second];
		  //}
                }
              else
                {
                  //logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                  //<< "cid: " << itr->second << " is not in cid_to_utf8";
                }
            }

          //for(auto itr=cmap_to_utf8.begin(); itr!=cmap_to_utf8.end(); itr++)
          //{
          //logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
          //<< itr->first << "\t" << itr->second;
          //}

          {
            core::object<core::FONT>::font_cmap_type font_cmap;

            for(auto itr=cmap_to_utf8.begin(); itr!=cmap_to_utf8.end(); itr++)
              {
                font_cmap.set(itr->first, itr->second);
              }

            object().set_cmap(font_cmap);
          }
        }
    }

    void parser<core::FONT>::parse_encoding(QPDFObjectHandle _handle)
    {
      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t" << __FUNCTION__;

      //print_dict(_handle);

      cid_cmap cids;

      if(_handle.hasKey("/Encoding"))
        {
          QPDFObjectHandle encoding = _handle.getKey("/Encoding");

          if(encoding.isName())
            {
              std::string name = encoding.getName();

              if(name == "/MacRomanEncoding")
                {
                  object().set_base_encoding(core::MACROMAN);
                }
              else if(name == "/MacExpertEncoding")
                {
                  object().set_base_encoding(core::MACEXPERT);
                }
              else if(name == "/WinAnsiEncoding")
                {
                  object().set_base_encoding(core::WINANSI);
                }
              else if(name == "/Identity-H")
                {
                  object().set_base_encoding(core::IDENTITY_H);
                }
              else if(cids.has(name))
                {
                  object().set_base_encoding(core::CMAP_RESOURCES);
                }
              else
                {
                  logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__
                                                  << "\tFont has no encoding: using STD encoding instead of "
                                                  << name;

                  object().set_base_encoding(core::STANDARD);
                }
            }
          else if(encoding.isDictionary())
            {
              if(encoding.hasKey("/BaseEncoding"))
                {
                  std::string name = encoding.getKey("/BaseEncoding").getName();
                  if(name == "/MacRomanEncoding")
                    {
                      object().set_base_encoding(core::MACROMAN);
                    }
                  else if(name == "/MacExpertEncoding")
                    {
                      object().set_base_encoding(core::MACEXPERT);
                    }
                  else if(name == "/WinAnsiEncoding")
                    {
                      object().set_base_encoding(core::WINANSI);
                    }
                  else
                    {
                      logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__
                                                      << "Font has no encoding: using STD encoding instad of "
                                                      << name;

                      object().set_base_encoding(core::STANDARD);
                    }
                }

              if(encoding.hasKey("/Differences"))
                {
                  QPDFObjectHandle differences = encoding.getKey("/Differences");
                  parse_differences(differences);
                }

              {
                core::object<core::DIFFERENCES>& font_differences = object().get_font_differences();

                parser<core::DIFFERENCES> parser;
                parser.execute(object().get_name(),
                               font_differences,
                               encoding);
              }
            }
          else
            {
              logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                               << "encoding ignored: not a name nor dictionary";
            }
        }
      else if(_handle.hasKey("/CIDSystemInfo"))
        {
          logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                          << "detected /CIDSystemInfo";

          QPDFObjectHandle cid_system_info = _handle.getKey("/CIDSystemInfo");

          std::string registry = cid_system_info.getKey("/Registry").getUTF8Value();
          std::string ordering = cid_system_info.getKey("/Ordering").getUTF8Value();

          int supplement = cid_system_info.getKey("/Supplement").getIntValue();

          std::string cmap_name = registry+"-"+ordering+"-"+std::to_string(supplement);

          if(cids.has(cmap_name))
            {
              object().set_base_encoding(core::CMAP_RESOURCES);
            }
          else
            {
              logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                              << "unknown cmap '" << cmap_name
                                              << "': using STD encoding";

              object().set_base_encoding(core::STANDARD);
            }
        }
      else
        {

        }

      auto enc = object().encoding();

      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                      << "detected encoding: " << to_string(enc);

    }

    void parser<core::FONT>::parse_metrics()
    {
      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t" << __FUNCTION__;

      if(_handle.getKey("/Subtype").getName() == "/Type1" and
         core::object<core::FONT>::my_fm_dictionary.has(_handle.getKey("/BaseFont").getName()))
        {
          std::string name = _handle.getKey("/BaseFont").getName();
	  logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
					  << "predefined font: " << name;

          object().set_font_metrics(name);

	  core::object<core::FONT>::font_metric_type& fmt = object().current_font_metrics();
	  //fmt.print(std::cout);

	  parse_metrics_("not applicable", fmt);
	  //fmt.print(std::cout);
        }
      else if(_handle.getKey("/Subtype").getName() == "/Type0")
        {
          std::string encoding_name = "not applicable";
          if(_handle.hasKey("/Encoding"))
            {
              encoding_name = _handle.getKey("/Encoding").getName();
            }

          core::object<core::FONT>::font_cmap_type   font_cmap;
          core::object<core::FONT>::font_metric_type font_metrics;

          // the DescendantFonts has only 1 element in the vector for type0! --> p 280
          for(QPDFObjectHandle descendant : _handle.getKey("/DescendantFonts").getArrayAsVector())
            {
              core::object<core::FONT> f;

              parser<core::FONT> p(descendant, f);

	      p.parse_encoding(_handle);
	      
	      p.parse_cmap(descendant);
	      p.parse_cmap_resources(descendant);
	      
	      p.parse_metrics_(encoding_name);

              font_cmap = f.get_cmap();
              font_metrics = f.current_font_metrics();
            }

          // if we already have a cmap, then we do not want to overwrite it!
	  // however, if we found a cmap in the descendant, we want to keep it
          if(object().get_cmap().size()==0)
	    {
	      object().set_cmap(font_cmap);
	    }

          object().set_font_metrics(font_metrics);
        }
      else if(_handle.getKey("/Subtype").getName() == "/TrueType" and
              core::object<core::FONT>::my_fm_dictionary.has(_handle.getKey("/BaseFont").getName()))
        {
          logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                          << "setting font-metrics for TrueType";

          std::string name = _handle.getKey("/BaseFont").getName();

          object().set_font_metrics(name);
        }
      else
        {
          parse_metrics_("not applicable");
        }
    }

    void parser<core::FONT>::parse_metrics_(std::string cmap_name)
    {
      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
				      << __FUNCTION__ << "\t cmap_name: " << cmap_name;

      //std::cout << "\t initialize the font-metrics for "<< object().name << "\n";
      //print_dict(_handle);
      
      core::object<core::FONT>::font_metric_type fm;

      parse_metrics_(cmap_name, fm);

      object().set_font_metrics(fm);
    }
    
    void parser<core::FONT>::parse_metrics_(std::string cmap_name,
					    core::object<core::FONT>::font_metric_type& fm)
    {
      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t" << __FUNCTION__;
      
      QPDFObjectHandle bbox;
      if(_handle.hasKey("/FontBBox"))
        {
          bbox = _handle.getKey("/FontBBox");
        }

      // parse the FontDescriptor --> p 281
      if(_handle.hasKey("/FontDescriptor"))
        {
          QPDFObjectHandle font_descriptor = _handle.getKey("/FontDescriptor");

          if(font_descriptor.hasKey("/Flags"))
            {
            }

          if(font_descriptor.hasKey("/FontBBox"))
            {
              bbox = font_descriptor.getKey("/FontBBox");

              fm.bbox[0] = bbox.getArrayItem(0).getNumericValue();
              fm.bbox[1] = bbox.getArrayItem(1).getNumericValue();
              fm.bbox[2] = bbox.getArrayItem(2).getNumericValue();
              fm.bbox[3] = bbox.getArrayItem(3).getNumericValue();
            }

          if(font_descriptor.hasKey("/ItalicAngle"))
            {
              fm.italic_angle = font_descriptor.getKey("/ItalicAngle").getNumericValue();
            }

          if(font_descriptor.hasKey("/Ascent"))
            {
              fm.ascent = font_descriptor.getKey("/Ascent").getNumericValue();

              // if the Ascent is zero, we take by default the Times-Roman value
              if(std::abs(fm.ascent)<1.e-3)
                fm.ascent = 683;
            }

          if(font_descriptor.hasKey("/Descent"))
            {
              fm.descent = std::abs(font_descriptor.getKey("/Descent").getNumericValue());

              // if the Ascent is set zero, we take by default the Times-Roman value
              if(std::abs(fm.descent)<1.e-3)
                fm.descent = 217;
            }

          if(font_descriptor.hasKey("/CapHeight"))
            {
              fm.cap_height = font_descriptor.getKey("/CapHeight").getNumericValue();
            }

          if(font_descriptor.hasKey("/XHeight"))
            {
              fm.x_height = font_descriptor.getKey("/XHeight").getNumericValue();
            }

          if(font_descriptor.hasKey("/StemV"))
            {
              fm.StemV = font_descriptor.getKey("/StemV").getNumericValue();
            }

          if(font_descriptor.hasKey("/StemH"))
            {
              fm.StemH = font_descriptor.getKey("/StemH").getNumericValue();
            }

          if(font_descriptor.hasKey("/AvgWidth"))
            {
              fm.average_width = font_descriptor.getKey("/AvgWidth").getNumericValue();
            }

          if(font_descriptor.hasKey("/MaxWidth"))
            {
              fm.max_width = font_descriptor.getKey("/MaxWidth").getNumericValue();
            }

          if(font_descriptor.hasKey("/MissingWidth"))
            {
              fm.missing_width = font_descriptor.getKey("/MissingWidth").getNumericValue();
            }

          if(font_descriptor.hasKey("/FontFile"))
            {
              logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                              << "ignoring /FontFile in font_descriptor!";
            }

          if(font_descriptor.hasKey("/FontFile2"))
            {
              logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                              << "ignoring /FontFile2 in font_descriptor!";

              /*
                core::fontfile2 fontfile2_obj;
                parser<core::FONTFILE2> p(font_descriptor.getKey("/FontFile2"), fontfile2_obj);
                p.parse(object().get_name());
              */
            }

          if(font_descriptor.hasKey("/FontFile3"))
            {
              logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                              << "ignoring /FontFile3 in font_descriptor!";

              /*
                core::fontfile3 fontfile3_obj;
                parser<core::FONTFILE3> p(font_descriptor.getKey("/FontFile3"), fontfile3_obj);
                p.parse(object().get_name());
              */
            }
        }

      std::string name = _handle.getKey("/Subtype").getName();
      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t detected /Subtype: " << name;

      //if(bbox) // use the new `bool()`      
      if(bbox.isInitialized())
	{
          if(not fm.ascent)
            {
              fm.ascent = bbox.getArrayItem(3).getNumericValue();
            }

          if(not fm.descent)
            {
              fm.descent = std::abs(bbox.getArrayItem(1).getNumericValue());
            }
        }

      if(_handle.hasKey("/Widths"))
        {
	  logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t detected /Widths";
	  
          std::vector<QPDFObjectHandle> raw = _handle.getKey("/Widths").getArrayAsVector();
          uint32_t first_char = 0;

          if(_handle.hasKey("/FirstChar"))
            {
              first_char += _handle.getKey("/FirstChar").getIntValue();
            }

          for(uint32_t i=0; i<raw.size(); i++)
            {
	      logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t width of char "
					      << (first_char+i) << " -> " << raw.at(i).getNumericValue();
              fm.widths[first_char + i] = raw.at(i).getNumericValue();
            }
        }
      else if(_handle.getKey("/Subtype").getName() == "/CIDFontType0" or
              _handle.getKey("/Subtype").getName() == "/CIDFontType2")
        {
          //std::cout << "font-name : " << object().name << "\n";
          // p 269

          if(_handle.hasKey("/DW"))
            {
              fm.default_width = _handle.getKey("/DW").getNumericValue();
            }
          else
            {
	      //logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t";
              fm.default_width = 1000;
            }

          if(_handle.hasKey("/W") and object().encoding()==core::CMAP_RESOURCES)
            {
              logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                              << "parsing /W for a CMAP_RESOURCE!";

              QPDFObjectHandle cid_system_info = _handle.getKey("/CIDSystemInfo");

              cid_cmap cids;
              if(not cids.has(cmap_name))
                {
                  std::string registry = cid_system_info.getKey("/Registry").getUTF8Value();
                  std::string ordering = cid_system_info.getKey("/Ordering").getUTF8Value();

                  int supplement = cid_system_info.getKey("/Supplement").getIntValue();

                  cmap_name = registry+"-"+ordering+"-"+std::to_string(supplement);
                }

              //cid_cmap cids;
              if(cids.has(cmap_name))
                {
                  logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                                  << "loading cmap-name: " << cmap_name;

                  std::map<uint32_t, uint32_t> cmap_to_cid;
                  {
                    cmap_to_cid = cids.load(cmap_name);
                  }

                  std::map<uint32_t, int> widths;
                  {
                    std::vector<QPDFObjectHandle> W = _handle.getKey("/W").getArrayAsVector();
                    parse_metrics_widths(W, widths);
                  }

                  //print_dict(_handle);

                  {
                    for(auto itr=cmap_to_cid.begin(); itr!=cmap_to_cid.end(); itr++)
                      {
                        if(widths.count(itr->second)==1)
                          {
                            //logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
			    //<< "cmap-id " << itr->first << " to width (="
			    //<< widths[itr->second]
			    //<< ") with cid-id: " << itr->second;

                            fm.widths[itr->first] = widths[itr->second];
                          }
                        else
                          {
                            //logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
			    //<< "could not resolve width for cmap-id: "
			    //<< itr->first << " with cid-id: " << itr->second;
                          }
                      }
                  }
                }

            }
          else if(_handle.hasKey("/W"))
            {
              logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                              << "parsing /W";

              uint32_t first_char;
              uint32_t last_char;

              auto W = _handle.getKey("/W").getArrayAsVector();

              for(auto itr = W.begin(); itr != W.end(); itr++)
                {
                  //std::cout << itr->unparse() << "\n";

                  first_char = itr->getIntValue();
                  itr++;

                  //std::cout << itr->unparse() << "\n";

                  if(itr->isArray())
                    {
                      uint32_t i = 0;
                      for(auto w : itr->getArrayAsVector())
                        {
                          //std::cout << w.unparse() << "\n";
                          //std::cout << first_char + (i++) << "\t" << w.getNumericValue() << "\n";
                          fm.widths[first_char + (i++)] = w.getNumericValue();
                        }
                    }
                  else
                    {
                      last_char = itr->getIntValue();
                      //std::cout << "\t range : " << first_char << "\t" << last_char << "\n";
                      itr++;

                      //std::cout << itr->unparse() << "\n";

                      for(double w = itr->getNumericValue(); first_char <= last_char; first_char++)
                        {
                          //std::cout << first_char << "\t" << w << "\n";
                          fm.widths[first_char] = w;
                        }
                    }
                }
            }
          else
            {
              logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
                                              << "no /W parameter";
            }

          if(_handle.hasKey("/DW2"))
            {
              logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__
                                              << "\tignoring /DW2 in the font-parsing!";
            }

          if(_handle.hasKey("/W2"))
            {
              logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__
                                              << "\tignoring /W2 in the font-parsing!";
            }
        }

      {
        //fm.print(std::cout);
      }

      /*
        {
        std::cout << fm.default_width << "\t" << fm.ascent << "\t" << fm.descent << "\n";
        for(auto itr = fm.widths.begin(); itr!= fm.widths.end(); itr++)
        std::cout << itr->first << "\t" << itr->second << "\n";
        std::cout << "\n";
        for(auto itr = fm.named_widths.begin(); itr!= fm.named_widths.end(); itr++)
        std::cout << itr->first << "\t" << itr->second << "\n";
        std::cout << "\n";
        }
      */
    }

    void parser<core::FONT>::parse_metrics_widths(std::vector<QPDFObjectHandle>& W,
                                                  std::map<uint32_t, int>& widths)
    {
      uint32_t first_char;
      uint32_t last_char;

      for(auto itr = W.begin(); itr != W.end(); itr++)
        {
          //std::cout << itr->unparse() << "\n";

          first_char = itr->getIntValue();
          itr++;

          //std::cout << itr->unparse() << "\n";

          if(itr->isArray())
            {
              uint32_t i = 0;
              for(auto w : itr->getArrayAsVector())
                {
                  //std::cout << w.unparse() << "\n";
                  //std::cout << first_char + (i++) << "\t" << w.getNumericValue() << "\n";
                  widths[first_char + (i++)] = w.getNumericValue();
                }
            }
          else
            {
              last_char = itr->getIntValue();
              //std::cout << "\t range : " << first_char << "\t" << last_char << "\n";
              itr++;

              //std::cout << itr->unparse() << "\n";

              for(double w = itr->getNumericValue(); first_char <= last_char; first_char++)
                {
                  //std::cout << first_char << "\t" << w << "\n";
                  widths[first_char] = w;
                }
            }
        }

    }

  }
}

#endif
