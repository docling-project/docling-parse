//-*-C++-*-

#ifndef PDF_PAGE_FONT_RESOURCE_H
#define PDF_PAGE_FONT_RESOURCE_H

#include <parse/qpdf/qpdf_compat.h>

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_FONT>
  {
  public:

    const static inline std::string RESOURCE_DIR_KEY = "pdf_resource_directory";
    
  public:

    pdf_resource(pdf_timings& timings);
    ~pdf_resource();

    static void initialise(nlohmann::json                            data,
			   std::unordered_map<std::string, double>& timings);

    nlohmann::json get();

    std::string get_encoding_name();
    font_encoding_name get_encoding();

    std::string get_key();
    std::string get_name();
    std::string get_base_font();

    double      get_width(uint32_t c, bool verbose=true);
    std::string get_string(uint32_t c);

    double get_space_width();
    double get_average_width();

    double get_ascent();
    double get_descent();

    double get_capheight();
    double get_xheight();
    bool has_char_bbox(const uint32_t& c);
    std::array<double, 4> get_char_bbox(const uint32_t& c);
    bool has_char_bbox(const std::string& c);
    std::array<double, 4> get_char_bbox(const std::string& c);

    std::array<double, 4> get_font_bbox() { return font_bbox; }
    const embedded_font_program& get_font_program() const { return font_program; }

    // Lazily extracts the embedded font program (first call only) and returns
    // the shared render-facing blob; null when the font has no usable embedded
    // program. All text instructions of this font share the same blob.
    std::shared_ptr<const embedded_font_blob> get_embedded_font_blob();

    // Raw glyph name (no leading '/') that /Encoding /Differences assigns to
    // this character code; empty when the code has no override. Used by the
    // renderer for glyph-identity lookups in embedded font programs.
    std::string get_glyph_name(uint32_t code);
    
    std::string get_utf8_string(std::string line, bool is_hex_str);

    // only needed for the cmap-resource files
    bool numb_is_in_cmap(uint32_t c); 
    
    void set(std::string      font_key_,
             nlohmann::json&  json_font_,
             QPDFObjectHandle qpdf_font_);

  private:

    std::string get_correct_character(uint32_t c);
    std::string get_character_from_encoding(uint32_t c);

    void init_encoding();
    void init_subtype();

    void init_base_font();
    void init_font_name();
    void init_font_bbox();
    void init_font_matrix();
    void init_font_program();
    bool try_init_font_program_from_descriptor(QPDFObjectHandle font_obj,
                                               nlohmann::json const& font_json,
                                               bool from_descendant_font);
    bool try_init_font_program_direct(QPDFObjectHandle font_obj,
                                      nlohmann::json const& font_json,
                                      bool from_descendant_font);
    void populate_font_program(QPDFObjectHandle descriptor_obj,
                               QPDFObjectHandle stream_obj,
                               nlohmann::json const& descriptor_json,
                               std::string const& source_path,
                               embedded_font_file_kind kind,
                               bool from_descendant_font);

    embedded_font_format resolve_embedded_font_format() const;
    bool resolve_cid_to_gid_identity() const;
    void build_embedded_font_blob();
    
    void init_ascent_and_descent();

    void init_default_width();

    void init_char_widths();

    void init_fchar();
    void init_lchar();
    void init_widths();
    void init_ws();

    void init_cmap(pdf_timings& timings);
    void init_cmap_resource();

    void init_differences();

    void init_charprocs();
    void init_space_index();

    void print_tables();

  private:

    static font_glyphs    glyphs;
    static font_cids      cids;
    static font_encodings encodings;
    static base_fonts     bfonts;

  private:

    pdf_timings& timings;
    
    nlohmann::json   json_font;
    nlohmann::json   desc_font; // derived from json_font, only for '/Type-0'

    QPDFObjectHandle qpdf_font;
    //QPDFObjectHandle qpdf_desc_font; // derived from json_font, only for '/Type-0'

    std::string        encoding_name;
    font_encoding_name encoding;
    bool               has_explicit_encoding; // true if encoding was found in PDF, false if defaulted

    font_subtype_name  subtype;

    std::string font_key;
    std::string font_name;
    std::string base_font;

    std::array<double, 4> font_bbox {0, 0, 0, 0};
    std::array<double, 6> font_matrix {0.001, 0, 0, 0.001, 0, 0};
    double type3_xscale = 1.0;
    double type3_yscale = 1.0;

    double ascent;
    double descent;

    double capheight;
    double xheight;

    double stemv, stemh;
    
    int fchar, lchar;

    bool   has_default_width=false;
    double default_width;

    std::unordered_map<uint32_t   , double> numb_to_widths;
    std::unordered_map<std::string, double> name_to_widths;

    std::unordered_map<std::string, char_description> name_to_descr;

    bool cmap_initialized;
    bool diff_initialized;

    //std::unordered_map<uint32_t, std::string> cmap_numb_to_char;
    cmap_value cmap_numb_to_char;
    std::unordered_map<uint32_t, std::string> diff_numb_to_char;
    std::unordered_map<uint32_t, std::string> diff_numb_to_name;

    std::unordered_map<uint32_t, int> unknown_numbs;

    uint32_t space_index;
    embedded_font_program font_program;

    bool font_blob_initialized = false;
    std::shared_ptr<const embedded_font_blob> font_blob;
  };

  font_glyphs    pdf_resource<PAGE_FONT>::glyphs = font_glyphs();
  font_cids      pdf_resource<PAGE_FONT>::cids = font_cids();
  font_encodings pdf_resource<PAGE_FONT>::encodings = font_encodings();
  base_fonts     pdf_resource<PAGE_FONT>::bfonts = base_fonts();

  pdf_resource<PAGE_FONT>::pdf_resource(pdf_timings& timings):
    timings(timings)
  {}
  
  pdf_resource<PAGE_FONT>::~pdf_resource()
  {
    if(unknown_numbs.size()>0)
      {
        LOG_S(WARNING) << "font " << font_name << " has some unknown chars:";
        for(auto itr=unknown_numbs.begin(); itr!=unknown_numbs.end(); itr++)
          {
            LOG_S(WARNING) << "\t" << itr->first << "\t" << itr->second;
          }
      }
  }

  void pdf_resource<PAGE_FONT>::initialise(nlohmann::json                            data,
					   std::unordered_map<std::string, double>& timings)
  {
    LOG_S(INFO) << __FUNCTION__ << ": " << data.dump(2);
    
    std::string PDFS_RESOURCES_DIR = "../docling_parse/pdf_resources/";
    LOG_S(INFO) << "default pdf-resource-dir: " << PDFS_RESOURCES_DIR;
    
    //std::string pdf_resources_dir = data.value("pdf-resource-directory", PDFS_RESOURCES_DIR);
    std::string pdf_resources_dir = data.value(RESOURCE_DIR_KEY, PDFS_RESOURCES_DIR);
    pdf_resources_dir += (pdf_resources_dir.back()=='/'? "" : "/");
    
    std::string glyphs_dir, cids_dir, encodings_dir, bfonts_dir;
    
    if(utils::filesystem::is_dir(pdf_resources_dir))
      {
	LOG_S(INFO) << "pdf_resources_dir: " << pdf_resources_dir;

	glyphs_dir    = pdf_resources_dir+"glyphs/";
	cids_dir      = pdf_resources_dir+"cmap-resources/";
	encodings_dir = pdf_resources_dir+"encodings/";
	bfonts_dir    = pdf_resources_dir+"fonts/";	
      }
    else
      {
	std::string message = "no existing pdf_resources_dir: " +  pdf_resources_dir; 
	LOG_S(ERROR) << message;
	throw std::logic_error(message);
      }
    
    utils::timer timer;
    
    {
      timer.reset();

      glyphs.initialise(glyphs_dir);

      timings["init-glyphs"] = timer.get_time();
    }

    {
      timer.reset();
      
      cids.initialise(cids_dir);
      
      timings["init-cids"] = timer.get_time();
    }

    {
      timer.reset();

      encodings.initialise(encodings_dir, glyphs);

      timings["init-encodings"] = timer.get_time();
    }

    {
      timer.reset();

      bfonts.initialise(bfonts_dir, glyphs);

      timings["init-bfonts"] = timer.get_time();
    }
  }

  nlohmann::json pdf_resource<PAGE_FONT>::get()
  {
    return json_font;
  }

  std::string pdf_resource<PAGE_FONT>::get_encoding_name()
  {
    return encoding_name;
  }

  font_encoding_name pdf_resource<PAGE_FONT>::get_encoding()
  {
    return encoding;
  }

  std::string pdf_resource<PAGE_FONT>::get_key()
  {
    return font_key;
  }

  std::string pdf_resource<PAGE_FONT>::get_name()
  {
    return font_name;
  }

  std::string pdf_resource<PAGE_FONT>::get_base_font()
  {
    return base_font;
  }

  bool pdf_resource<PAGE_FONT>::numb_is_in_cmap(uint32_t v)
  {
    //LOG_S(INFO) << "# cmap: " << cmap_numb_to_char.size();
    return (cmap_numb_to_char.count(v)==1);
  }

  double pdf_resource<PAGE_FONT>::get_width(uint32_t c, bool verbose)
  {
    if(numb_to_widths.count(c)==1)
      {
        return numb_to_widths[c];
      }
    else if(has_default_width)
      {
	return default_width;
      }    
    else if(bfonts.has_corresponding_font(font_name) or
	    bfonts.has_corresponding_font(base_font))
      {
	std::string fontname = bfonts.has_corresponding_font(font_name)
	  ? bfonts.get_corresponding_font(font_name)
	  : bfonts.get_corresponding_font(base_font);
	
        auto& bfont = bfonts.get(fontname);

        if(bfont.has(c))
          {
            return bfont.get_width(c);
          }
	else if(bfont.has(get_string(c)))
	  {
	    return bfont.get_width(get_string(c));
	  }
	else if(has_default_width)
	  {
	    return default_width;
	  }
        else if(verbose)
          {	    
            LOG_S(WARNING) << "fontname " << fontname
			   << " does not have numb_to_width for " << c 
			   << " (space-index=" << space_index << ")";
          }
	else
	  {}
      }
    else if(c==space_index)
      {
	return 500;
      }
    else if(verbose)
      {
        LOG_S(WARNING) << "font does not have numb_to_width for " << c
		       << " nor a known font [base-font=" << base_font << ", font-name=" << font_name
		       << ", font-key=" << font_key << "]"
		       << " --> falling back on default width in " << __FUNCTION__;
      }
    
    return 500.0;
  }

  double pdf_resource<PAGE_FONT>::get_space_width()
  {
    //LOG_S(INFO) << __FUNCTION__ 
    //<< "\tspace-index: " << space_index 
    //<< "\t font-name: " << font_name
    //<< "\t font-key: " << font_key;

    if(space_index!=-1)
      {
        return get_width(space_index);
      }

    return 500.0;
  }

  double pdf_resource<PAGE_FONT>::get_average_width()
  {
    LOG_S(WARNING) << "implement " << __FUNCTION__;
    return 500.0;
  }

  double pdf_resource<PAGE_FONT>::get_ascent()
  {
    return ascent;
  }

  double pdf_resource<PAGE_FONT>::get_descent()
  {
    return descent;
  }

  double pdf_resource<PAGE_FONT>::get_capheight()
  {
    return capheight;
  }

  double pdf_resource<PAGE_FONT>::get_xheight()
  {
    return xheight;
  }

  bool pdf_resource<PAGE_FONT>::has_char_bbox(const uint32_t& c)
  {
    if(bfonts.has_corresponding_font(font_name) or
       bfonts.has_corresponding_font(base_font))
      {
        std::string fontname = bfonts.has_corresponding_font(font_name)
          ? bfonts.get_corresponding_font(font_name)
          : bfonts.get_corresponding_font(base_font);

        auto& bfont = bfonts.get(fontname);
        return bfont.has_char_bbox(c);
      }

    return false;
  }

  std::array<double, 4> pdf_resource<PAGE_FONT>::get_char_bbox(const uint32_t& c)
  {
    std::string fontname = bfonts.has_corresponding_font(font_name)
      ? bfonts.get_corresponding_font(font_name)
      : bfonts.get_corresponding_font(base_font);

    auto& bfont = bfonts.get(fontname);
    return bfont.get_char_bbox(c);
  }

  bool pdf_resource<PAGE_FONT>::has_char_bbox(const std::string& c)
  {
    if(bfonts.has_corresponding_font(font_name) or
       bfonts.has_corresponding_font(base_font))
      {
        std::string fontname = bfonts.has_corresponding_font(font_name)
          ? bfonts.get_corresponding_font(font_name)
          : bfonts.get_corresponding_font(base_font);

        auto& bfont = bfonts.get(fontname);
        return bfont.has_char_bbox(c);
      }

    return false;
  }

  std::array<double, 4> pdf_resource<PAGE_FONT>::get_char_bbox(const std::string& c)
  {
    std::string fontname = bfonts.has_corresponding_font(font_name)
      ? bfonts.get_corresponding_font(font_name)
      : bfonts.get_corresponding_font(base_font);

    auto& bfont = bfonts.get(fontname);
    return bfont.get_char_bbox(c);
  }
  
  std::string pdf_resource<PAGE_FONT>::get_string(uint32_t c)
  {
    //LOG_S(INFO) << __FUNCTION__ << "\t" << c;

    switch(encoding)
      {
      case IDENTITY_H:
      case IDENTITY_V:
        {
          std::string result = "";

          if(cmap_numb_to_char.count(c))
            {
              result += cmap_numb_to_char.at(c);
            }
	  else if(32<=c)
            {
              utf8::append(c, std::back_inserter(result));
            }
          else
            {
              LOG_S(ERROR) << "could not decode character with value=" << c
			     << " for encoding=" << to_string(encoding)
			     << ", fontname=" << font_name
			     << " and subtype=" << subtype;
	      
	      result = "GLYPH<c="+std::to_string(c)+",font="+font_name+">";
            }

          return result;
        }
        break;

      case STANDARD:
      case MACROMAN:
      case MACEXPERT:
      case WINANSI:
        {
          std::string result = "";

          result += get_correct_character(c);

          return result;
        }
        break;

      case CMAP_RESOURCES:
	{
          if(cmap_numb_to_char.count(c))
	    {
	      return cmap_numb_to_char.at(c);
	    }
	  else if(32<=c)
            {
              std::string tmp;
              utf8::append(c, std::back_inserter(tmp));

	      return tmp;
            }
	  else
	    {
	      LOG_S(ERROR) << "could not decode character with value=" << c
			     << " for encoding=" << to_string(encoding)
			     << ", fontname=" << font_name
			     << " and subtype=" << subtype;
	      return "GLYPH<c="+std::to_string(c)+",font="+font_name+">";
	    }
	}
	break;

      default:
        {
          LOG_S(ERROR) << "could not decode character with value=" << c
                       << " for encoding=" << to_string(encoding)
                       << ", fontname=" << font_name
                       << " and subtype=" << subtype;

          return std::string("GLYPH<UNKNOWN>");
        }
      }
  }

  std::string pdf_resource<PAGE_FONT>::get_correct_character(uint32_t c)
  {
    // Sometimes, a font has differences-map and a cmap
    // defined at the same time. So far, it seems that the
    // differences should take precedent over the cmap. This
    // is however not really clear (eg p 292). Notice also that
    // we init the cmap before we init the difference and that the
    // difference inherits the content of a the cmap. It is a bit
    // messy and unclear her.

    /*
    if(diff_numb_to_char.count(c)>0 and cmap_numb_to_char.count(c)>0)
      {
	LOG_S(WARNING) << "there might be some confusion here: "
		       << "diff["<<c<<"]: " << diff_numb_to_char.at(c) << " "
		       << "cmap["<<c<<"]: " << cmap_numb_to_char.at(c);
      }
    */
    
    if(diff_initialized and diff_numb_to_char.count(c)>0)
      {
        return diff_numb_to_char.at(c);
      }
    else if(cmap_initialized and cmap_numb_to_char.count(c)>0)
      {
        return cmap_numb_to_char.at(c);
      }         
    else if(bfonts.has_corresponding_font(font_name))
      {
        // check if the font-name is registered as a 'special' font, eg
        // the TeX mathematical fonts

        std::string fontname = bfonts.get_corresponding_font(font_name);
	//LOG_S(WARNING) << "detected a known font: " << font_name << " -> " << fontname;

        auto& fm = bfonts.get(fontname);

        // If font declares a specific encoding (MacRoman, WinAnsi, etc.) AND it was
        // explicitly specified in the PDF, use that encoding instead of base font's built-in mapping
        if(has_explicit_encoding &&
           (encoding == MACROMAN || encoding == MACEXPERT || encoding == WINANSI || encoding == STANDARD))
          {
            return get_character_from_encoding(c);
          }
        else if(fm.has(c))
          {
            return fm.to_utf8(c);
          }
        else if(bfonts.is_core_14_font(fontname))
	  {
	    /*
	      logging_lib::warn("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t"
	      << "font " << font_name << " found in the Core 14 metrics: " << c
	      << "; Encoding: " << to_string(_encoding)
	      << "; font-name: " << font_name;
	    */
	    return get_character_from_encoding(c);
	  }
	else
	  {
	    /*
	    std::string notdef="GLYPH<"+std::to_string(c)+">";

	    unknown_numbs[c] += 1;

	    LOG_S(ERROR) << " Symbol not found in special font: " << c
			 << "; Encoding: "  << to_string(encoding)
			 << "; font-name: " << font_name
			 << " (corresponding font: " << fontname << ")";

	    return notdef;
	    */

	    LOG_S(WARNING) << " Symbol not found in special font: " << c
			   << "; Encoding: "  << to_string(encoding)
			   << "; font-name: " << font_name
			   << " (corresponding font: " << fontname << ")";

	    return get_character_from_encoding(c);
	  }
      }
    else
      {
	//LOG_S(WARNING) << "no known font: " << font_name;
        return get_character_from_encoding(c);
      }
  }

  std::string pdf_resource<PAGE_FONT>::get_character_from_encoding(uint32_t c)
  {
    auto& base_encoding = encodings.get(encoding).get_numb_to_utf8();

    auto itr = std::find_if(base_encoding.begin(), base_encoding.end(),
                            [&] (const std::pair<uint32_t, std::string> & item)
                            {
                              return item.first == c;
                            });

    if(itr != base_encoding.end())
      {
        return itr->second;
      }
    else
      {
        auto& cencoding = encodings.get(STANDARD).get_numb_to_utf8();

        auto std_itr = std::find_if(cencoding.begin(), cencoding.end(),
                                    [&] (const std::pair<uint32_t, std::string> & item)
                                    {
                                      return item.first == c;
                                    });

        if(std_itr != cencoding.end())
          {
            return std_itr->second;
          }
        else
          {
            std::string notdef="GLYPH<"+std::to_string(c)+">";

            unknown_numbs[c] += 1;

            LOG_S(ERROR) << "Symbol not found: " << int(c)
                         << "; Encoding: "  << to_string(encoding)
                         << "; font-name: " << font_name;
	    
            return notdef;
          }
      }
  }

  void pdf_resource<PAGE_FONT>::set(std::string      font_key_,
                                    nlohmann::json&  json_font_,
                                    QPDFObjectHandle qpdf_font_)
  {
    LOG_S(INFO) << __FUNCTION__ << " font: " << font_key_;

    /*
      if(true)
      {
      print_obj(qpdf_font_);
      
      try
      {
      LOG_S(INFO) << "font [key='" << font_key_ << "']:\n" << json_font_.dump(2);
      }
      catch(std::exception e)
      {
      LOG_S(ERROR) << "could not dump the json-representation of the font [key=" 
      << font_key_ << "] with error: " << e.what();
      }
      }
    */

    {
      utils::timer font_timer;

      font_key  = font_key_;
      json_font = json_font_;
      qpdf_font = qpdf_font_;

      double font_time = font_timer.get_time();
      timings.add_timing(pdf_timings::KEY_FONT_INIT_COPY, font_time);
    }
    
    {
      utils::timer font_timer;
      
      init_encoding();
      init_subtype();
      
      init_base_font();
      
      init_font_name();
      init_font_bbox();
      init_font_matrix();
      // init_font_program(); // extraction is lazy: see get_embedded_font_blob()
      
      init_ascent_and_descent();
      
      init_default_width();
      
      init_char_widths();

      double font_time = font_timer.get_time();
      timings.add_timing(pdf_timings::KEY_FONT_INIT_METRICS, font_time);
    }
    
    {
      utils::timer font_timer;
      
      init_cmap(timings);

      double font_time = font_timer.get_time();
      timings.add_timing(pdf_timings::KEY_FONT_CMAP, font_time);
    }

    {
      utils::timer font_timer;
      
      init_cmap_resource();

      double font_time = font_timer.get_time();
      timings.add_timing(pdf_timings::KEY_FONT_CMAP_RESOURCES, font_time);
    }
    
    LOG_S(INFO) << __FUNCTION__ << "\t cmap-init: " << cmap_initialized;
    LOG_S(INFO) << __FUNCTION__ << "\t cmap-size: " << cmap_numb_to_char.size();

    {
      utils::timer font_timer;
      
      init_charprocs();
      
      init_differences();
      
      init_space_index();

      double font_time = font_timer.get_time();
      timings.add_timing(pdf_timings::KEY_FONT_CHARS, font_time);
    }
    
    unknown_numbs.clear();

    /*
      if(true)
      {
      print_tables();
      }
    */
  }

  void pdf_resource<PAGE_FONT>::init_encoding()
  {
    LOG_S(INFO) << __FUNCTION__;

    std::vector<std::string> keys_0 = {"/Encoding", "/BaseEncoding"};
    std::vector<std::string> keys_1 = {"/Encoding"};

    std::string name;
    if(utils::json::has(keys_0, json_font))
      {
        name = utils::json::get(keys_0, json_font);
        encoding = to_encoding_name(name);
        has_explicit_encoding = true;

        LOG_S(INFO) << "font-encoding [" << name << "]: " << to_string(encoding);
      }
    else if(utils::json::has(keys_1, json_font))
      {
        auto result = utils::json::get(keys_1, json_font);

        if(result.is_string())
          {
            encoding_name = result.get<std::string>();

	    if(cids.has(encoding_name))
	      {
		encoding = CMAP_RESOURCES;
		has_explicit_encoding = true;
	      }
	    else if(encoding_name.find("stream") != std::string::npos)
	      {
		LOG_S(WARNING) << "font-encoding [" << name << "] contains stream, "
			       << "falling back to STANDARD encoding";

		/*
		encoding = to_encoding_name(encoding_name);
		auto qpdf_obj = qpdf_font.getKey("/Encoding");

		if(qpdf_obj.isStream())
		  {
		    std::vector<qpdf_stream_instruction> stream;

		    // decode the stream
		    {
		      qpdf_stream_decoder decoder(stream);
		      decoder.decode(qpdf_obj);

		      decoder.print();
		    }
		  }
		else
		  {
		    LOG_S(WARNING) << "could not init stream ...";
		  }
		*/
		encoding = STANDARD;
		has_explicit_encoding = false;
	      }
	    else
	      {
		encoding = to_encoding_name(encoding_name);
		has_explicit_encoding = true;
	      }

            LOG_S(INFO) << "font-encoding [" << name << "]: " << to_string(encoding);
          }
        else if(result.is_object() && result.count("/BaseEncoding") == 1 && result["/BaseEncoding"].is_string())
          {
            // Extract /BaseEncoding from encoding dictionary
            std::string base_enc = result["/BaseEncoding"].get<std::string>();
            encoding = to_encoding_name(base_enc);
            has_explicit_encoding = true;
            LOG_S(INFO) << "font-encoding from object /BaseEncoding [" << base_enc << "]: " << to_string(encoding);
          }
        else
          {
            LOG_S(WARNING) << " --> font-encoding falling back to STANDARD with font-encoding [object]: " << result.dump();

            encoding = STANDARD;
            has_explicit_encoding = false;
          }
      }
    else
      {
        LOG_S(WARNING) << "font-encoding not defined, falling back to STANDARD";
        encoding = STANDARD;
        has_explicit_encoding = false;
      }
  }

  void pdf_resource<PAGE_FONT>::init_subtype()
  {
    LOG_S(INFO) << __FUNCTION__;

    std::vector<std::string> keys = {"/Subtype"};

    if(utils::json::has(keys, json_font))
      {
        std::string name = utils::json::get(keys, json_font);
        subtype = to_subtype_name(name);

        LOG_S(INFO) << "subtype [" << name << "]: " << to_string(subtype);

        std::vector<std::string> keys_0 = {"/DescendantFonts"};
        if(subtype==TYPE_0 and utils::json::has(keys_0, json_font))
          {
            auto desc_fonts = utils::json::get(keys_0, json_font);

	    if(desc_fonts.size()==1)
	      {
		LOG_S(INFO) << "found the descendant font";// << desc_font.dump(2);
		desc_font = desc_fonts[0];

		//qpdf_desc_font = qpdf_font.getKey(keys_0.at(0)).getArrayItem(0);		
	      }
	    else
	      {
		std::string message = "no descendant font!";
		LOG_S(ERROR) << message;
		
		throw std::logic_error(message);
	      }
          }
        else if(subtype==TYPE_0)
          {
            LOG_S(WARNING) << "no descendant font! [this might be a problem]";// << desc_font.dump(2);
          }
        else
          {
            LOG_S(INFO) << "no descendant font";// << desc_font.dump(2);
          }
      }
    else
      {
        subtype=NULL_TYPE;
        LOG_S(ERROR) << "could not find subtype in font: " << json_font.dump(2);
      }
  }

  void pdf_resource<PAGE_FONT>::init_base_font()
  {
    LOG_S(INFO) << __FUNCTION__;

    std::vector<std::string> keys = {"/BaseFont"};

    base_font = "null";
    if(utils::json::has(keys, json_font))
      {
        base_font = utils::json::get(keys, json_font);
        LOG_S(INFO) << "base-font: " << base_font;
      }
    else if(utils::json::has(keys, desc_font))
      {
        base_font = utils::json::get(keys, desc_font);
        LOG_S(INFO) << "base-font: " << base_font;
      }
    else
      {
        LOG_S(ERROR) << "could not find base-name";
      }
  }

  void pdf_resource<PAGE_FONT>::init_font_name()
  {
    LOG_S(INFO) << __FUNCTION__;

    std::vector<std::string> keys_0 = {"/FontDescriptor", "/FontName"};
    std::vector<std::string> keys_1 = {"/Name"};

    font_name = "null";
    if(utils::json::has(keys_0, json_font))
      {
        font_name = utils::json::get(keys_0, json_font);
        LOG_S(INFO) << "font-name: " << font_name;
      }
    else if(utils::json::has(keys_0, desc_font))
      {
        font_name = utils::json::get(keys_0, desc_font);
        LOG_S(INFO) << "font-name: " << font_name;
      }
    else if(utils::json::has(keys_1, json_font))
      {
        font_name = utils::json::get(keys_1, json_font);
        LOG_S(INFO) << "font-name: " << font_name;
      }
    else if(base_font!="null")
      {
        font_name = base_font;
        LOG_S(INFO) << "font-name [from base-font]: " << font_name;        
      }
    else
      {
        LOG_S(ERROR) << "could not find font-name";
      }
  }

  void pdf_resource<PAGE_FONT>::init_font_bbox()
  {
    LOG_S(INFO) << __FUNCTION__;// << "\t" << json_font.dump(2);

    std::vector<std::string> keys_0 = {"/FontDescriptor", "/FontBBox"};
    std::vector<std::string> keys_1 = {"/FontBBox"};
    nlohmann::json json_bbox;
    
    if(utils::json::has(keys_0, json_font))
      {
        json_bbox = utils::json::get(keys_0, json_font);
      }
    else if(utils::json::has(keys_0, desc_font))
      {
        json_bbox = utils::json::get(keys_0, desc_font);
      }
    else if(utils::json::has(keys_1, json_font))
      {
        //assert(subtype==TYPE_3);

        json_bbox = utils::json::get(keys_1, json_font);
      }
    else if(utils::json::has(keys_1, desc_font))
      {
        //assert(subtype==TYPE_3);

        json_bbox = utils::json::get(keys_1, desc_font);
      }
    else if(bfonts.has(base_font)==1)
      {
        LOG_S(WARNING) << "font-bbox retrieved from base-font";
        font_bbox = bfonts[base_font].get_font_bbox();
      }
    else
      {
        LOG_S(WARNING) << "could not find font-bbox";
      }

    if (json_bbox != nullptr)
      {
        if (json_bbox.is_array() and json_bbox.size() == 4)
          {
            for(int d=0; d<4; d++)
              {
                font_bbox[d] = json_bbox[d].get<double>();
              }
          }
        else
          {
            LOG_S(ERROR) << "expected 4 elements in font-bbox, got: " << json_bbox;
          }
      }

    LOG_S(INFO) << " -> font-bbox: [" 
                << font_bbox[0] << ", "
                << font_bbox[1] << ", "
                << font_bbox[2] << ", "
                << font_bbox[3] << "]";
  }


  void pdf_resource<PAGE_FONT>::init_font_matrix()
  {
    LOG_S(INFO) << __FUNCTION__;// << "\t" << json_font.dump(2);

    std::vector<std::string> keys_0 = {"/FontMatrix"};

    if(utils::json::has(keys_0, json_font))
      {
        //assert(subtype==TYPE_3);
        auto json_matrix = utils::json::get(keys_0, json_font);

        if (json_matrix.is_array() and json_matrix.size() == 6)
          {
            for(int d=0; d<6; d++)
              {
                font_matrix[d] = json_matrix[d].get<double>();
              }
            type3_xscale = font_matrix[0] * 1000.0;
            type3_yscale = font_matrix[3] * 1000.0;
          }
        else
          {
            LOG_S(ERROR) << "expected 6 elements in font-matrix, got: " << json_matrix;
          }
      }
    else
      {
        LOG_S(INFO) << "using default font-matrix";
      }

    LOG_S(INFO) << " -> font-matrix: ["
                << font_matrix[0] << ", "
                << font_matrix[1] << ", "
                << font_matrix[2] << ", "
                << font_matrix[3] << ", "
                << font_matrix[4] << ", "
                << font_matrix[5] << "]";
  }

  void pdf_resource<PAGE_FONT>::init_font_program()
  {
    LOG_S(INFO) << __FUNCTION__
                << " for font-key=" << font_key
                << " font-name=" << font_name
                << " base-font=" << base_font
                << " subtype=" << to_string(subtype);

    font_program = embedded_font_program();
    font_program.base_font = base_font;
    font_program.font_name = font_name;

    bool found = false;

    LOG_S(INFO) << __FUNCTION__ << ": probing primary font descriptor";
    found = try_init_font_program_from_descriptor(qpdf_font, json_font, false);

    if(not found and subtype==TYPE_0 and qpdf_font.hasKey("/DescendantFonts"))
      {
        LOG_S(INFO) << __FUNCTION__ << ": probing descendant font descriptor";
        auto desc_fonts = qpdf_font.getKey("/DescendantFonts");
        if(desc_fonts.isArray() and desc_fonts.getArrayNItems() > 0)
          {
            auto qpdf_desc_font = desc_fonts.getArrayItem(0);
            found = try_init_font_program_from_descriptor(qpdf_desc_font, desc_font, true);
            if(not found)
              {
                LOG_S(INFO) << __FUNCTION__ << ": probing descendant font directly";
                found = try_init_font_program_direct(qpdf_desc_font, desc_font, true);
              }
          }
        else
          {
            LOG_S(INFO) << __FUNCTION__ << ": /DescendantFonts missing or empty at qpdf level";
          }
      }

    if(not found)
      {
        LOG_S(INFO) << __FUNCTION__ << ": probing primary font object directly";
        found = try_init_font_program_direct(qpdf_font, json_font, false);
      }

    if(not found and subtype==TYPE_0 and qpdf_font.hasKey("/DescendantFonts"))
      {
        auto desc_fonts = qpdf_font.getKey("/DescendantFonts");
        if(desc_fonts.isArray() and desc_fonts.getArrayNItems() > 0)
          {
            LOG_S(INFO) << __FUNCTION__ << ": probing descendant font directly as final fallback";
            auto qpdf_desc_font = desc_fonts.getArrayItem(0);
            found = try_init_font_program_direct(qpdf_desc_font, desc_font, true);
          }
      }

    if(found)
      {
        LOG_S(INFO) << __FUNCTION__
                    << ": found embedded font program"
                    << " kind=" << to_string(font_program.kind)
                    << " source=" << font_program.source_path
                    << " declared-subtype=" << font_program.declared_subtype
                    << " raw-size=" << font_program.raw_size
                    << " decoded-size=" << font_program.decoded_size
                    << " length=" << font_program.length
                    << " length1=" << font_program.length1
                    << " length2=" << font_program.length2
                    << " length3=" << font_program.length3;
      }
    else
      {
        LOG_S(INFO) << __FUNCTION__ << ": no embedded font program found";
      }
  }

  bool pdf_resource<PAGE_FONT>::try_init_font_program_from_descriptor(
    QPDFObjectHandle font_obj,
    nlohmann::json const& font_json,
    bool from_descendant_font)
  {
    LOG_S(INFO) << __FUNCTION__
                << ": from_descendant_font=" << from_descendant_font;

    if(not font_obj.isDictionary())
      {
        LOG_S(INFO) << __FUNCTION__ << ": font object is not a dictionary";
        return false;
      }

    if(not font_obj.hasKey("/FontDescriptor"))
      {
        LOG_S(INFO) << __FUNCTION__ << ": no /FontDescriptor on qpdf font object";
        return false;
      }

    auto descriptor_obj = font_obj.getKey("/FontDescriptor");
    if(not descriptor_obj.isDictionary())
      {
        LOG_S(INFO) << __FUNCTION__ << ": /FontDescriptor is not a dictionary";
        return false;
      }

    nlohmann::json descriptor_json = nullptr;
    if(font_json.is_object() and font_json.count("/FontDescriptor") == 1)
      {
        descriptor_json = font_json["/FontDescriptor"];
      }

    struct candidate_spec
    {
      const char* key;
      embedded_font_file_kind kind;
    };

    const std::array<candidate_spec, 3> specs = {{
        {"/FontFile",  FONT_FILE_TYPE1},
        {"/FontFile2", FONT_FILE_TRUETYPE},
        {"/FontFile3", FONT_FILE_CFF},
      }};

    for(const auto& spec : specs)
      {
        LOG_S(INFO) << __FUNCTION__ << ": checking descriptor key " << spec.key;
        if(not descriptor_obj.hasKey(spec.key))
          {
            continue;
          }

        auto stream_obj = descriptor_obj.getKey(spec.key);
        if(not stream_obj.isStream())
          {
            LOG_S(INFO) << __FUNCTION__ << ": key " << spec.key << " exists but is not a stream";
            continue;
          }

        populate_font_program(descriptor_obj,
                              stream_obj,
                              descriptor_json,
                              std::string("/FontDescriptor") + spec.key,
                              spec.kind,
                              from_descendant_font);
        return true;
      }

    LOG_S(INFO) << __FUNCTION__ << ": no embedded font stream found in descriptor";
    return false;
  }

  bool pdf_resource<PAGE_FONT>::try_init_font_program_direct(
    QPDFObjectHandle font_obj,
    nlohmann::json const& font_json,
    bool from_descendant_font)
  {
    LOG_S(INFO) << __FUNCTION__
                << ": from_descendant_font=" << from_descendant_font;

    if(not font_obj.isDictionary())
      {
        LOG_S(INFO) << __FUNCTION__ << ": font object is not a dictionary";
        return false;
      }

    struct candidate_spec
    {
      const char* key;
      embedded_font_file_kind kind;
    };

    const std::array<candidate_spec, 3> specs = {{
        {"/FontFile",  FONT_FILE_TYPE1},
        {"/FontFile2", FONT_FILE_TRUETYPE},
        {"/FontFile3", FONT_FILE_CFF},
      }};

    for(const auto& spec : specs)
      {
        LOG_S(INFO) << __FUNCTION__ << ": checking direct key " << spec.key;
        if(not font_obj.hasKey(spec.key))
          {
            continue;
          }

        auto stream_obj = font_obj.getKey(spec.key);
        if(not stream_obj.isStream())
          {
            LOG_S(INFO) << __FUNCTION__ << ": key " << spec.key << " exists but is not a stream";
            continue;
          }

        populate_font_program(font_obj,
                              stream_obj,
                              font_json,
                              spec.key,
                              spec.kind,
                              from_descendant_font);
        return true;
      }

    LOG_S(INFO) << __FUNCTION__ << ": no direct embedded font stream found";
    return false;
  }

  void pdf_resource<PAGE_FONT>::populate_font_program(
    QPDFObjectHandle descriptor_obj,
    QPDFObjectHandle stream_obj,
    nlohmann::json const& descriptor_json,
    std::string const& source_path,
    embedded_font_file_kind kind,
    bool from_descendant_font)
  {
    LOG_S(INFO) << __FUNCTION__
                << ": source_path=" << source_path
                << " kind=" << to_string(kind)
                << " from_descendant_font=" << from_descendant_font;

    font_program = embedded_font_program();
    font_program.found = true;
    font_program.kind = kind;
    font_program.source_path = source_path;
    font_program.base_font = base_font;
    font_program.font_name = font_name;
    font_program.from_descendant_font = from_descendant_font;
    font_program.descriptor_json = descriptor_json;
    font_program.stream_dict_json = to_json(stream_obj, {}, 0, 2);
    // Disabled: dumping the full stream dictionary per font floods the logs.
    // LOG_S(INFO) << __FUNCTION__
    //             << ": stream-dict-json=\n"
    //             << font_program.stream_dict_json.dump(2);

    auto subtype_info = to_string(stream_obj, "/Subtype");
    if(subtype_info.first)
      {
        font_program.declared_subtype = subtype_info.second;
      }

    auto update_length = [&](const char* key, int& dst)
    {
      if(stream_obj.hasKey(key) and stream_obj.getKey(key).isInteger())
        {
          dst = stream_obj.getKey(key).getIntValue();
          LOG_S(INFO) << __FUNCTION__ << ": " << key << "=" << dst;
        }
      else
        {
          LOG_S(INFO) << __FUNCTION__ << ": " << key << " not present as integer";
        }
    };

    update_length("/Length", font_program.length);
    update_length("/Length1", font_program.length1);
    update_length("/Length2", font_program.length2);
    update_length("/Length3", font_program.length3);

    // Disabled: a font program is binary data, not a content stream —
    // running the instruction decoder over it is meaningless work.
    // try
    //   {
    //     LOG_S(INFO) << __FUNCTION__ << ": decoding font stream with qpdf_stream_decoder";
    //     qpdf_stream_decoder decoder(font_program.decoded_stream);
    //     decoder.decode(stream_obj);
    //     LOG_S(INFO) << __FUNCTION__
    //                 << ": decoded stream instruction count="
    //                 << font_program.decoded_stream.size();
    //     decoder.print();
    //   }
    // catch(const std::exception& e)
    //   {
    //     LOG_S(INFO) << __FUNCTION__ << ": failed to decode stream instructions: " << e.what();
    //   }

    // Disabled: keeping raw (compressed) bytes next to the decoded bytes
    // doubles the memory per font; the renderer only needs decoded bytes.
    // try
    //   {
    //     font_program.raw_data = to_shared_ptr(stream_obj.getRawStreamData());
    //     if(font_program.raw_data)
    //       {
    //         font_program.raw_size = font_program.raw_data->getSize();
    //       }
    //     LOG_S(INFO) << __FUNCTION__ << ": raw_size=" << font_program.raw_size;
    //   }
    // catch(const std::exception& e)
    //   {
    //     LOG_S(INFO) << __FUNCTION__ << ": failed to read raw stream data: " << e.what();
    //   }

    try
      {
        font_program.decoded_data = to_shared_ptr(stream_obj.getStreamData(qpdf_dl_all));
        if(font_program.decoded_data)
          {
            font_program.decoded_size = font_program.decoded_data->getSize();
          }
        LOG_S(INFO) << __FUNCTION__ << ": decoded_size=" << font_program.decoded_size;
      }
    catch(const std::exception& e)
      {
        LOG_S(INFO) << __FUNCTION__ << ": failed to read decoded stream data: " << e.what();
      }

    LOG_S(INFO) << __FUNCTION__
                << ": completed"
                << " kind=" << to_string(font_program.kind)
                << " source=" << font_program.source_path
                << " declared_subtype=" << font_program.declared_subtype;
  }

  std::shared_ptr<const embedded_font_blob> pdf_resource<PAGE_FONT>::get_embedded_font_blob()
  {
    if(not font_blob_initialized)
      {
        font_blob_initialized = true;

        init_font_program();
        build_embedded_font_blob();
      }

    return font_blob;
  }

  std::string pdf_resource<PAGE_FONT>::get_glyph_name(uint32_t code)
  {
    auto itr = diff_numb_to_name.find(code);
    if(itr != diff_numb_to_name.end())
      {
        return itr->second;
      }

    return "";
  }

  embedded_font_format pdf_resource<PAGE_FONT>::resolve_embedded_font_format() const
  {
    switch(font_program.kind)
      {
      case FONT_FILE_TYPE1:
        {
          return embedded_font_format::TYPE1;
        }
      case FONT_FILE_TRUETYPE:
        {
          return embedded_font_format::TRUETYPE;
        }
      case FONT_FILE_CFF:
        {
          if(font_program.declared_subtype == "/Type1C")
            {
              return embedded_font_format::TYPE1C;
            }
          if(font_program.declared_subtype == "/CIDFontType0C")
            {
              return embedded_font_format::CID_TYPE0C;
            }
          if(font_program.declared_subtype == "/OpenType")
            {
              return embedded_font_format::OPENTYPE;
            }
          return embedded_font_format::UNKNOWN;
        }
      default:
        {
          return embedded_font_format::UNKNOWN;
        }
      }
  }

  bool pdf_resource<PAGE_FONT>::resolve_cid_to_gid_identity() const
  {
    if(subtype != TYPE_0)
      {
        return false;
      }

    // PDF spec: /CIDToGIDMap defaults to /Identity when absent. Any stream
    // value means an explicit map that we do not resolve here.
    std::vector<std::string> keys = {"/CIDToGIDMap"};
    if(not utils::json::has(keys, desc_font))
      {
        return true;
      }

    nlohmann::json value = utils::json::get(keys, desc_font);
    return value.is_string() and value.get<std::string>() == "/Identity";
  }

  void pdf_resource<PAGE_FONT>::build_embedded_font_blob()
  {
    if(not font_program.found or
       not font_program.decoded_data or
       font_program.decoded_data->getSize() == 0)
      {
        LOG_S(INFO) << __FUNCTION__ << ": no embedded font bytes for font-key=" << font_key;
        return;
      }

    auto bytes = std::make_shared<std::vector<uint8_t> >(
      font_program.decoded_data->getBuffer(),
      font_program.decoded_data->getBuffer() + font_program.decoded_data->getSize());

    // The blob owns the only long-lived copy; drop the qpdf buffer so the
    // bytes are not held twice.
    font_program.decoded_data.reset();

    embedded_font_format format = resolve_embedded_font_format();

    font_blob = std::make_shared<const embedded_font_blob>(
      embedded_font_blob::compute_cache_key(*bytes),
      font_name,
      base_font,
      font_program.source_path,
      format,
      subtype == TYPE_0,
      resolve_cid_to_gid_identity(),
      std::move(bytes));

    LOG_S(INFO) << __FUNCTION__
                << ": font-key=" << font_key
                << " font-name=" << font_name
                << " source=" << font_blob->get_source_key()
                << " format=" << to_string(font_blob->get_format())
                << " bytes=" << font_blob->byte_size()
                << " cache-key=" << font_blob->get_cache_key()
                << " cid=" << font_blob->get_is_cid_font()
                << " cid-to-gid-identity=" << font_blob->get_cid_to_gid_identity();
  }

  void pdf_resource<PAGE_FONT>::init_ascent_and_descent()
  {
    LOG_S(INFO) << __FUNCTION__;

    ascent=0;
    {
      std::vector<std::string> keys = {"/FontDescriptor", "/Ascent"};

      bool ascent_defined=false;
      if(utils::json::has(keys, json_font))
        {
          ascent = utils::json::get(keys, json_font);
          ascent_defined=true;

          LOG_S(INFO) << "ascent: " << ascent;
        }
      else if(utils::json::has(keys, desc_font))
        {
          ascent = utils::json::get(keys, desc_font);
          ascent_defined=true;

          LOG_S(INFO) << "ascent: " << ascent;
        }
      else
        {
          LOG_S(WARNING) << "'ascend' was not explicitely defined ...";
        }

      if(not ascent_defined)
        {
          if(bfonts.has(base_font))
            {
              ascent = bfonts[base_font].get_ascend();
              LOG_S(WARNING) << " -> ascend (=" << ascent << ") retrieved from base-font (=" << base_font << ")";
            }
          else if(std::abs(font_bbox[3])>1.e-3)
            {
              ascent = font_bbox[3];
              LOG_S(WARNING) << " -> falling back on font-bbox for ascent (=" << ascent << ")";
            }
          else 
            {
              // from times-Roman
              ascent = 683.0;
              LOG_S(WARNING) << " -> falling back on the default value for ascent (=" << ascent << ")";
            }
        }
    }

    descent=0;
    {
      std::vector<std::string> keys = {"/FontDescriptor", "/Descent"};

      bool descent_defined=false;
      if(utils::json::has(keys, json_font))
        {
          descent = utils::json::get(keys, json_font);
          descent_defined=true;

          LOG_S(INFO) << "descent: " << descent;
        }
      else if(utils::json::has(keys, desc_font))
        {
          descent = utils::json::get(keys, desc_font);
          descent_defined=true;

          LOG_S(INFO) << "descent: " << descent;
        }
      else
        {
          LOG_S(WARNING) << "'descend' was not explicitely defined ...";
        }

      if(not descent_defined)
        {
          if(bfonts.has(base_font))
            {
              descent = bfonts[base_font].get_descend();
              LOG_S(WARNING) << " -> descend (=" << descent << ") retrieved from base-font (=" << base_font << ")";
            }
          else if(std::abs(font_bbox[1])>1.e-3)
            {
              descent = font_bbox[1];
              LOG_S(WARNING) << " -> falling back on font-bbox for descent (=" << descent << ")";
            }
          else
            {
              // from times-Roman
              descent = -250.0;
              LOG_S(WARNING) << " -> falling back on default value for descent (=" << descent << ")";
            }
        }
    }

    if(std::abs( ascent)<1.e-3 and 
       std::abs(descent)<1.e-3   )
      {
        LOG_S(ERROR) << "ascent (=" << ascent << ") and descent (=" << descent << ") are "
                     << "equal to zero. This might lead to weird representation!";

	if(std::abs(font_bbox[1])>1.e-3)
	  {
	    descent = font_bbox[1];
	    LOG_S(WARNING) << " -> falling back on font-bbox for descent (=" << descent << ")";
	  }

	if(std::abs(font_bbox[3])>1.e-3)
	  {
	    ascent = font_bbox[3];
	    LOG_S(WARNING) << " -> falling back on font-bbox for ascent (=" << ascent << ")";
	  }
      }

    capheight=0;
    {
      std::vector<std::string> keys = {"/FontDescriptor", "/CapHeight"};
      
      //bool capheight_defined=false;
      if(utils::json::has(keys, json_font))
        {
          capheight = utils::json::get(keys, json_font);
          //capheight_defined=true;

          LOG_S(INFO) << "capheight: " << capheight;
        }
      else if(utils::json::has(keys, desc_font))
        {
          capheight = utils::json::get(keys, desc_font);
          //capheight_defined=true;

          LOG_S(INFO) << "capheight: " << capheight;
        }
      else
        {
          LOG_S(WARNING) << "'capheight' was not explicitely defined ...";
	  if(bfonts.has(base_font))
	    {
	      capheight = bfonts[base_font].get_capheight();
	      LOG_S(WARNING) << " -> capheight (=" << capheight << ") retrieved from base-font (=" << base_font << ")";
	    }
	  else
	    {
	      capheight = ascent;
	      LOG_S(WARNING) << " -> capheight defaulting to ascent (=" << capheight << ")";
	    }
        }
    }

    xheight=0;
    {
      std::vector<std::string> keys = {"/FontDescriptor", "/XHeight"};
      
      //bool xheight_defined=false;
      if(utils::json::has(keys, json_font))
        {
          xheight = utils::json::get(keys, json_font);
          //xheight_defined=true;

          LOG_S(INFO) << "xheight: " << xheight;
        }
      else if(utils::json::has(keys, desc_font))
        {
          xheight = utils::json::get(keys, desc_font);
          //xheight_defined=true;

          LOG_S(INFO) << "xheight: " << xheight;
        }
      else
        {
          LOG_S(WARNING) << "'xheight' was not explicitely defined ...";
	  if(bfonts.has(base_font))
	    {
	      xheight = bfonts[base_font].get_xheight();
	      if(std::abs(xheight) > 1.e-3)
		{
		  LOG_S(WARNING) << " -> xheight (=" << xheight << ") retrieved from base-font (=" << base_font << ")";
		}
	    }
        }
    }

    ascent *= type3_yscale;
    descent *= type3_yscale;
    capheight *= type3_yscale;
    xheight *= type3_yscale;
  }

  void pdf_resource<PAGE_FONT>::init_default_width()
  {
    LOG_S(INFO) << __FUNCTION__;

    has_default_width=false;

    std::vector<std::string> f_keys = {"/DW"};

    if(utils::json::has(f_keys, json_font))
      {
	has_default_width = true;
        default_width     = utils::json::get(f_keys, json_font).get<double>();

        LOG_S(INFO) << "default-width: " << default_width;
      }
    else if(utils::json::has(f_keys, desc_font))
      {
	has_default_width = true;
        default_width     = utils::json::get(f_keys, desc_font).get<double>();

        LOG_S(INFO) << "default-width: " << default_width;
      }
    else
      {
	default_width = 500;
        LOG_S(WARNING) << "could not find default-width: defaulting to " << default_width;
      }    
  }

  void pdf_resource<PAGE_FONT>::init_char_widths()
  {
    LOG_S(INFO) << __FUNCTION__;

    init_fchar();
    init_lchar();

    init_widths();
    init_ws();
  }

  void pdf_resource<PAGE_FONT>::init_fchar()
  {
    LOG_S(INFO) << __FUNCTION__;

    fchar=-1;

    std::vector<std::string> f_keys = {"/FirstChar"};
    if(utils::json::has(f_keys, json_font))
      {
        fchar = utils::json::get(f_keys, json_font).get<int>();
        LOG_S(INFO) << "fchar: " << fchar;
      }
    else if(utils::json::has(f_keys, desc_font))
      {
        fchar = utils::json::get(f_keys, desc_font).get<int>();
        LOG_S(INFO) << "fchar: " << fchar;
      }
    else
      {
        LOG_S(WARNING) << "could not find first-char: defaulting to " << fchar;
      }
  }

  void pdf_resource<PAGE_FONT>::init_lchar()
  {
    LOG_S(INFO) << __FUNCTION__;

    lchar=-1;

    std::vector<std::string> l_keys = {"/LastChar"};
    if(utils::json::has(l_keys, json_font))
      {
        lchar = utils::json::get(l_keys, json_font).get<int>();
        LOG_S(INFO) << "lchar: " << lchar;
      }
    else if(utils::json::has(l_keys, desc_font))
      {
        lchar = utils::json::get(l_keys, desc_font).get<int>();
        LOG_S(INFO) << "lchar: " << lchar;
      }
    else
      {
        LOG_S(WARNING) << "could not find last-char: defaulting to " << lchar;
      }
  }

  void pdf_resource<PAGE_FONT>::init_widths()
  {
    LOG_S(INFO) << __FUNCTION__;

    std::vector<double> values={};
    {
      std::vector<std::string> keys = {"/Widths"};

      bool found_widths = false;
      if(utils::json::has(keys, json_font) and (not found_widths))
        {
          auto result = utils::json::get(keys, json_font);
          LOG_S(INFO) << "widths: " << result.dump();

	  if(result.is_array())
	    {
	      values = result.get<std::vector<double> >();
	      found_widths = true;
	    }
        }
      else if(utils::json::has(keys, desc_font) and (not found_widths))
        {
          auto result = utils::json::get(keys, desc_font);
          LOG_S(INFO) << "widths: " << result.dump();

	  if(result.is_array())
	    {
	      values = result.get<std::vector<double> >();
	      found_widths = true;
	    }
        }
      else if(not found_widths)
        {
          LOG_S(WARNING) << "could not find widths";
        }
    }

    if(fchar==-1 and lchar==-1 and values.size()==0)
      {
	LOG_S(WARNING) << "did not detect any /Widths";
        return;
      }

    if(values.size()!=(lchar-fchar+1))
      {
        LOG_S(ERROR) << "values.size()!=(lchar-fchar+1) -> "
                     << values.size() << "!=" << lchar << "-" << fchar << "+1";
      }

    int cnt=0;
    for(int ind=fchar; ind<=lchar; ind++)
      {
	if(cnt>=values.size())
	  {
	    LOG_S(ERROR) << "going out of bounds with " << cnt << " >= " << values.size();
	    continue;
	  }
	
        numb_to_widths[ind] = values[cnt++] * type3_xscale;
        //LOG_S(INFO) << "index: " << ind << " -> width: " << numb_to_widths.at(ind);
      }
  }

  void pdf_resource<PAGE_FONT>::init_ws()
  {
    LOG_S(INFO) << __FUNCTION__;

    nlohmann::json ws;

    {
      std::vector<std::string> keys = {"/W"};

      if(utils::json::has(keys, json_font))
        {
          ws = utils::json::get(keys, json_font);
        }
      else if(utils::json::has(keys, desc_font))
        {
          ws = utils::json::get(keys, desc_font);
        }
      else
        {
          LOG_S(WARNING) << "could not find '/W'";
          return;
        }

      LOG_S(INFO) << "detected '/W'";
    }

    int beg=-1;
    int end=-1;

    for(int l=0; l<ws.size(); )
      {
        LOG_S(INFO) << l << "\t" << ws[l].is_number() << "\t beg: " << ws[l].dump();

        //assert(l<ws.size());
	
        beg = ws[l].get<int>();
        l += 1;

        if(l==0)
          {
            fchar=beg;
          }

        if(ws[l].is_number())
          {
            //LOG_S(INFO) << l << "\t" << ws[l].is_number() << "\t end: " << ws[l].dump();

            //assert(l<ws.size());

	    if(l>=ws.size())
	      {
		LOG_S(WARNING) << "index " << l << " is out of bounds " << ws.size();
		continue;
	      }
	    
            end = ws[l].get<int>();
            l += 1;

            //LOG_S(INFO) << l << "\t" << ws[l].is_number() << "\t w: " << ws[l].dump();

            //assert(l<ws.size());
	    if(l>=ws.size())
	      {
		LOG_S(WARNING) << "index " << l << " is out of bounds " << ws.size();
		continue;
	      }
	    
            double w = ws[l].get<double>();
            l += 1;

            for(int id=beg; id<=end; id++)
              {
		//LOG_S(WARNING) << "\t" << id << " -> " << w;
                numb_to_widths[id] = w * type3_xscale;
              }
          }
        else if(ws[l].is_array())
          {
            //LOG_S(INFO) << l << "\t" << ws[l].is_number() << "\t widths: " << ws[l].dump();

            //assert(l<ws.size());
	    if(l>=ws.size())
	      {
		LOG_S(WARNING) << "index " << l << " is out of bounds " << ws.size();
		continue;
	      }
	    
            std::vector<double> w = ws[l].get<std::vector<double> >();
            l += 1;

            for(int k=0; k<w.size(); k++)
              {
		//LOG_S(WARNING) << "\t" << beg+k  << " -> " << w[k];

                numb_to_widths[beg+k] = w[k] * type3_xscale;
              }
          }
        else if(ws[l].is_null())
          {
	    LOG_S(WARNING) << "\t ws[" << l << "] is null ... skipping now";
	    l += 1;
	  }
        else
          {
	    std::stringstream message;
	    message <<  "unknown type in " << __FUNCTION__ << " for " << ws.dump(2);	    

	    LOG_S(ERROR) << message.str();
	    throw std::logic_error(message.str());
          }
      }
  }

  void pdf_resource<PAGE_FONT>::init_cmap(pdf_timings& timings)
  {
    LOG_S(INFO) << __FUNCTION__;

    std::vector<std::string> keys = { "/ToUnicode" };

    if(utils::json::has(keys, json_font))
      {
        LOG_S(INFO) << "found a /ToUnicode cmap: starting to decode ...";

        if(not qpdf_font.hasKey("/ToUnicode"))
          {
            auto tmp = to_json(qpdf_font);

	    std::stringstream ss;
	    ss << "qpdf-font: " << tmp.dump();
	    
            LOG_S(ERROR) << ss.str();
	    throw std::logic_error(ss.str());
          }

        auto qpdf_obj = qpdf_font.getKey("/ToUnicode");

	if(qpdf_obj.isStream())
	  {
	    std::vector<qpdf_stream_instruction> stream;
	    
	    // decode the stream
	    {
	      utils::timer font_timer;
	      
	      qpdf_stream_decoder decoder(stream);
	      decoder.decode(qpdf_obj);
	      
	      //decoder.print();

	      double font_time = font_timer.get_time();
	      timings.add_timing(pdf_timings::KEY_FONT_CMAP_STREAM_DECODE, font_time);
	    }

	    // interprete the stream
	    {
	      // empty key_root => cmap timings aggregate into the static
	      // cmap-parse-* keys (rather than per-font dynamic keys)
	      std::string key_root = "";

	      cmap_parser parser;
	      parser.parse(stream, timings, key_root);

	      //parser.print();

	      cmap_numb_to_char = parser.get();
	    }
	  }
	else if(qpdf_obj.isString())
	  {
	    auto _ = to_json(qpdf_obj);	    
	    std::string message = "qpdf_obj.isString(): " + _.dump(2);

	    LOG_S(ERROR) << message;
	    throw std::logic_error(message);
	  }
	else if(qpdf_obj.isName())
	  {
	    auto _ = to_json(qpdf_obj);	    
	    std::string message = "qpdf_obj.isName(): " + _.dump(2);

	    LOG_S(ERROR) << message;
	    //throw std::logic_error(message);	    
	  }    
	else
	  {
	    auto _ = to_json(qpdf_obj);	    
	    std::string message = "qpdf_obj is unknown: " + _.dump(2);

	    LOG_S(ERROR) << message;
	    throw std::logic_error(message);
	  }

        /*
        {
          for(auto itr=cmap_numb_to_char.begin(); itr!=cmap_numb_to_char.end(); itr++)
            {
              LOG_S(INFO) << "\t" << itr->first << " -> " << itr->second;
            }
        }
        */

        cmap_initialized = true;
      }
    else
      {
	cmap_initialized = false;
      }
  }

  void pdf_resource<PAGE_FONT>::init_cmap_resource()
  {
    LOG_S(INFO) << __FUNCTION__;

    if(cmap_initialized) // we found a `ToUnicode` before. No need to go deeper! 
      {
	LOG_S(WARNING) << "We found a `ToUnicode` before. No need to go deeper!";
	return;
      }
    //else

    if(subtype==TYPE_0 and desc_font!=NULL and 
       cids.has(encoding_name) )
      {
	try
	  {
	    LOG_S(INFO) << "descendant-font: " << desc_font.dump(2);
	  }
	catch(const std::exception& exc)
	  {
	    LOG_S(ERROR) << "could not dump the descendant font with error: " 
			 << exc.what();
	  }

	LOG_S(INFO) << "encoding-name: " << encoding_name;

	if(cids.decode_cmap_resource(encoding_name))
	  {
	    font_cid& cid = cids.get(encoding_name);
	
	    cmap_numb_to_char = cid.get();	

	    cid.decode_widths(numb_to_widths);	

	    cmap_initialized = true;	    
	  }
	else
	  {
	    cmap_initialized = false;	    
	  }
      }
    else if(subtype==TYPE_0 and desc_font!=NULL)
      {
	try
	  {
	    LOG_S(INFO) << "descendant-font: " << desc_font.dump(2);
	  }
	catch(const std::exception& exc)
	  {
	    LOG_S(ERROR) << "could not dump the descendant font with error: " 
			 << exc.what();
	  }

	LOG_S(INFO) << "encoding-type: " << to_string(encoding);
	LOG_S(INFO) << "encoding-name: " << encoding_name;

	std::vector<std::string> key_registry = {"/CIDSystemInfo", "/Registry"};
	std::vector<std::string> key_ordering = {"/CIDSystemInfo", "/Ordering"};
	std::vector<std::string> key_supplement = {"/CIDSystemInfo", "/Supplement"};

	std::string registry_   = utils::json::get(key_registry, desc_font).get<std::string>();
	std::string ordering_   = utils::json::get(key_ordering, desc_font).get<std::string>();
	int         supplement_ = utils::json::get(key_supplement, desc_font).get<int>();
	
	LOG_S(INFO) << "found descendant-font without /ToUnicode";
	LOG_S(INFO) << " --> registry: " << registry_;
	LOG_S(INFO) << " --> ordering: " << ordering_;
	LOG_S(INFO) << " --> supplement: " << supplement_;
	
	int supplement = cids.get_supplement(registry_, ordering_);

	if(supplement_>supplement)
	  {
	    LOG_S(ERROR) << "Unknown CIDSystemInfo with "
			   << "registry: " << registry_ << " "
			   << "ordering: " << ordering_ << " "
			   << "supplement: " << supplement_ << " "
			   << "max-supplement: " << supplement;

	    cmap_initialized = false;
	    return;
	  }

	std::string encoding_name = registry_+"-"+ordering_+"-"+std::to_string(supplement_);

	/*
	if(cids.has_cmap_resource(name))
	  {
	    LOG_S(INFO) << "found cid with name: " << name;

	    font_cid cid;

	    cids.decode_cmap_resource(name, cid);	
	    
	    cmap_numb_to_char = cid.get();
	    
	    cmap_initialized = true;	    
	  }
	*/
	if(cids.decode_cmap_resource(encoding_name))
	  {
	    font_cid& cid = cids.get(encoding_name);
	
	    cmap_numb_to_char = cid.get();	

	    cid.decode_widths(numb_to_widths);	

	    cmap_initialized = true;	    
	  }
	else
	  {
	    LOG_S(ERROR) << "Unknown CIDSystemInfo with "
			   << "registry: " << registry_ << " "
			   << "ordering: " << ordering_ << " "
			   << "supplement: " << supplement_ << " "
			   << "max-supplement: " << supplement;

	    cmap_initialized = false;
	  }
      }
    else
      {
        cmap_initialized = false;
        LOG_S(WARNING) << "could not find cmap in '/ToUnicode'";
      }

    /*
    // FIXME
    if(cmap_numb_to_char.size()==0)
      {
	throw std::logic_error(__FUNCTION__);
      }
    */
  }

  // p 263
  void pdf_resource<PAGE_FONT>::init_differences()
  {
    LOG_S(INFO) << __FUNCTION__;

    std::vector<std::string> keys = { "/Encoding", "/Differences" };

    // Create a regex object
    std::regex re_01(R"(\/(.+)\.(.+))");
    std::regex re_02(R"((\/)?(uni|UNI)([0-9A-Fa-f]{4}))");
    std::regex re_03(R"((\/)(g|G)\d+)");
    std::regex re_04(R"((\/)(C)(\d+))");
    
    if(utils::json::has(keys, json_font))
      {
        auto diffs = utils::json::get(keys, json_font);
        //LOG_S(INFO) << "diffs: " << diffs.dump(2);

        if(diffs.is_array())
          {
            int         numb=-1;
            std::string name="null";

            for(int l=0; l<diffs.size(); l++)
              {
                if(diffs[l].is_number())
                  {
                    numb = diffs[l].get<int>();
                  }
                else if(diffs[l].is_string())
                  {
                    name = diffs[l].get<std::string>();

		    // Object to hold the match results
		    std::smatch match;
		    
                    std::string name_ = "", font_subname = "";
		    if(std::regex_search(name, match, re_01))
		      {
			name_ = match[1].str();
			font_subname = utils::string::to_lower(match[2].str());

			LOG_S(WARNING) << name << " => (" << name_ << ", " << font_subname << ")"; 
		      }                    
		    else if(name.size()>0 and name[0]=='/')
                      {
                        name_ = name.substr(1, name.size()-1);
                      }
		    else
		      {}

		    // Keep the raw glyph name (with any ".suffix", without the
		    // leading '/'): it is the identity of the glyph inside the
		    // embedded font program.
		    if(numb >= 0 and name.size() > 1 and name[0] == '/')
		      {
			diff_numb_to_name[numb] = name.substr(1);
		      }

		    LOG_S(INFO) << name << ", in cmap: " << cmap_numb_to_char.count(numb) << ", #-names: " << name_to_descr.size() << ", type: " << subtype;
		    
                    if(subtype==TYPE_3 and //name_to_descr.count(name)==1 and // only for TYPE_3 fonts
                       cmap_numb_to_char.count(numb)==1)
                      {
			LOG_S(WARNING) << "overloading difference from cmap";
                        diff_numb_to_char[numb] = cmap_numb_to_char.at(numb);
                      }

		    // FIXME: might need to be commented out or fixed
		    /*
                    else if(name_to_descr.count(name)==1 and
                            cmap_numb_to_char.count(numb)==0)
                      {
		        //assert(subtype==TYPE_3);

                        LOG_S(WARNING) << "could not resolve the character (name="<<name
                                       <<", numb="<<numb<<") for TYPE_3 font:" << font_name;

                        diff_numb_to_char[numb] = "glyph["+font_name+"|"+name+"]";
			//diff_numb_to_char[numb] = "glyph["+font_name+"|"+name+"]";
		      }
		    */
		    else if(glyphs.has(name) and font_subname=="sups")
                      {
                        diff_numb_to_char[numb] = "$^{" + glyphs[name] + "}";
                        LOG_S(INFO) << "differences[" << numb << "] -> " << name
				    << " -> " << diff_numb_to_char[numb];
                      }
		    else if(glyphs.has(name) and font_subname=="subs")
                      {
                        diff_numb_to_char[numb] = "$_{" + glyphs[name] + "}";
                        LOG_S(INFO) << "differences[" << numb << "] -> " << name
				    << " -> " << diff_numb_to_char[numb];
                      }		    
                    else if(glyphs.has(name))
                      {
                        diff_numb_to_char[numb] = glyphs[name];
                        LOG_S(INFO) << "differences[" << numb << "] -> " << name
				    << " -> " << diff_numb_to_char[numb];
                      }

		    else if(glyphs.has(name_) and font_subname=="sups")
                      {
                        diff_numb_to_char[numb] = "$^{" + glyphs[name_] + "}";
                        LOG_S(INFO) << "differences[" << numb << "] -> " << name_
				    << " -> " << diff_numb_to_char[numb];
                      }
		    else if(glyphs.has(name_) and font_subname=="subs")
                      {
                        diff_numb_to_char[numb] = "$_{" + glyphs[name_] + "}";
                        LOG_S(INFO) << "differences[" << numb << "] -> " << name_
				    << " -> " << diff_numb_to_char[numb];
                      }		    
		    else if(glyphs.has(name_))
                      {
                        diff_numb_to_char[numb] = glyphs[name_];
                        LOG_S(INFO) << "differences[" << numb << "] -> " << name_
				    << " -> " << diff_numb_to_char[numb];
                      }
		    else if(name_.find('_') != std::string::npos)
		      {
			// Adobe Glyph Naming: underscores separate ligature components
			// e.g. /f_i -> fi (U+FB01), /f_f_i -> ffi (U+FB03)
			// Strategy 1: remove underscores and look up the joined name
			std::string joined = name_;
			joined.erase(std::remove(joined.begin(), joined.end(), '_'), joined.end());
			if(glyphs.has(joined))
			  {
			    diff_numb_to_char[numb] = glyphs[joined];
			    LOG_S(INFO) << "differences[" << numb << "] -> " << name_
					<< " -> " << diff_numb_to_char[numb]
					<< " (ligature join)";
			  }
			else
			  {
			    // Strategy 2: decompose on '_' and concatenate each component
			    std::string result;
			    std::istringstream iss(name_);
			    std::string component;
			    bool all_found = true;
			    while(std::getline(iss, component, '_'))
			      {
				if(glyphs.has(component))
				  {
				    result += glyphs[component];
				  }
				else
				  {
				    all_found = false;
				    break;
				  }
			      }
			    if(all_found and not result.empty())
			      {
				diff_numb_to_char[numb] = result;
				LOG_S(INFO) << "differences[" << numb << "] -> " << name_
					    << " -> " << diff_numb_to_char[numb]
					    << " (ligature decompose)";
			      }
			    else
			      {
				diff_numb_to_char[numb] = name;
				LOG_S(WARNING) << "differences[" << numb << "] -> " << name
					       << " (unresolved ligature)";
			      }
			  }
		      }
		    /*
                    else if(name_.size()>0)
                      {
                        diff_numb_to_char[numb] = name_;
                        LOG_S(WARNING) << "differences["<<numb<<"] -> " << name_;
                      }
		    */
		    else if(std::regex_search(name, match, re_02))
		      {
			std::string unicode_hex = match[3].str();
			// LOG_S(WARNING) << "name: " << name << ", unicode_hex: " << unicode_hex << ", len: " << unicode_hex.size();
			
			diff_numb_to_char[numb] = utils::string::hex_to_utf8(unicode_hex, 4);
			LOG_S(WARNING) << "differences["<<numb<<"] -> "
				       << diff_numb_to_char[numb]
				       << " (from " << name << ")";
		      }
		    else if(std::regex_search(name_, match, re_02))
		      {
			std::string unicode_hex = match[3].str();
			// LOG_S(WARNING) << "name: " << name_ << ", unicode_hex: " << unicode_hex << ", len: " << unicode_hex.size();
			
			diff_numb_to_char[numb] = utils::string::hex_to_utf8(unicode_hex, 4);
			LOG_S(WARNING) << "differences["<<numb<<"] -> "
				       << diff_numb_to_char[numb]
				       << " (from " << name << ")";
		      }
		    else if(std::regex_match(name, match, re_03) and cmap_numb_to_char.count(numb)==1) // if the name is of type /g23 of /G23 and we have a match in the cmap
		      {
			LOG_S(WARNING) << "overloading difference from cmap";
                        diff_numb_to_char[numb] = cmap_numb_to_char.at(numb);
			//diff_numb_to_char[numb] = name;
			//LOG_S(ERROR) << "weird differences["<<numb<<"] -> " << name;
		      }
		    else if(std::regex_match(name, match, re_04)) // if the name is of type /C<decimal> treat the number as a Unicode code point
		      {
			uint32_t codepoint = static_cast<uint32_t>(std::stoul(match[3].str()));
			std::vector<uint32_t> vec = {codepoint};
			diff_numb_to_char[numb] = utils::string::vec_to_utf8(vec);
			LOG_S(INFO) << "differences[" << numb << "] -> " << name
				    << " -> " << diff_numb_to_char[numb]
				    << " (codepoint=" << codepoint << ")";
		      }
                    else
                      {
                        diff_numb_to_char[numb] = name;
                        LOG_S(WARNING) << "differences["<<numb<<"] -> " << name;
                      }

                    LOG_S(INFO) << font_name << ": differences["<<numb<<"] -> " << name << " -> " << diff_numb_to_char[numb];

                    numb += 1;
                  }
                else
                  {
                    LOG_S(WARNING) << "item [" << diffs[l].dump(2)
                                   << "] is not a string nor a number in the difference-vector: " << diffs.dump(2);
                  }
              }
          }
        else
          {
            LOG_S(WARNING) << "/Differences is not a vector: " << diffs.dump(2);
          }

        diff_initialized = true;
      }
    else
      {
        diff_initialized = false;
        LOG_S(WARNING) << "could not find differences in /Encoding/Differences";
      }
  }

  void pdf_resource<PAGE_FONT>::init_charprocs()
  {
    LOG_S(INFO) << __FUNCTION__;

    std::vector<std::string> keys = { "/CharProcs" };

    if(utils::json::has(keys, json_font))
      {
        //assert(subtype==TYPE_3);

        QPDFObjectHandle qpdf_char_procs = qpdf_font.getKey(keys.front());
        LOG_S(WARNING) << "found CharProcs: " << qpdf_char_procs.getTypeName();        
        
        auto json_char_procs = utils::json::get(keys, json_font);
       
        for(auto& pair : json_char_procs.items()) 
          {
            std::string key = pair.key();

            if(qpdf_char_procs.hasKey(key))
              {
                QPDFObjectHandle qpdf_char_proc = qpdf_char_procs.getKey(key);
                //LOG_S(INFO) << "decoding: " << key << " -> " << qpdf_char_proc.getTypeName();

                //assert(qpdf_char_proc.isStream());
		if(not qpdf_char_proc.isStream())
		  {
		    std::string message = "not qpdf_obj.isStream()";
		    LOG_S(ERROR) << message;
		    throw std::logic_error(message);
		  }
		
                std::vector<qpdf_stream_instruction> stream={};

                // decode the stream
                {
                  qpdf_stream_decoder decoder(stream);
                  decoder.decode(qpdf_char_proc);                  
                  decoder.print();
                }

		LOG_S(INFO) << "key: " << key << " => #-streams: " << stream.size();
		
                // interprete the stream
                {
                  char_processor parser;
                  parser.parse(stream);

                  name_to_descr[key] = parser.parse(stream);
		  //LOG_S(INFO) << key << ": " << name_to_descr.at(key);

                  //parser.print();          
                  //cmap_numb_to_char = parser.get();

                  // FIXME: place-holder for now
                  //char_description desc;
                  //name_to_descr[key] = desc; 
                }
              }
            else
              {
                LOG_S(WARNING) << "could not find key: " << key;
              }            
          }
      }    
  }

  void pdf_resource<PAGE_FONT>::init_space_index()
  {
    LOG_S(INFO) << __FUNCTION__;

    // FIXME: do we want to include all here: http://jkorpela.fi/chars/spaces.html
    std::vector<std::string> space_in_hex = { "0020", "2002"};
    std::vector<std::string> space_in_str = {};
    for(auto hex:space_in_hex)
      {
	std::string str = utils::string::hex_to_utf8(hex, 4);
	LOG_S(INFO) << "\t" << hex << "\t'" << str << "'";

	space_in_str.push_back(str);
      }

    space_index = -1;

    for(auto str:space_in_str)
      {
	for(auto itr=cmap_numb_to_char.begin(); itr!=cmap_numb_to_char.end(); itr++)
	  {
	    if(space_index==-1 and (itr->second)==str and 
	       numb_to_widths.count(itr->first)==1  ) 
	      {
		space_index = itr->first;
	      }
	    else if(space_index!=-1)
	      {
		break;
	      }
	    else
	      {}
	  }
	
	for(auto itr=diff_numb_to_char.begin(); itr!=diff_numb_to_char.end(); itr++)
	  {
	    if(space_index==-1 and (itr->second)==str and 
	       numb_to_widths.count(itr->first)==1 ) 
	      {
		space_index = itr->first;
	      }
	    else if(space_index!=-1)
	      {
		break;
	      }
	    else
	      {}
	  }
      }

    for(auto itr=cmap_numb_to_char.begin(); itr!=cmap_numb_to_char.end(); itr++)
      {
        if(space_index==-1 and itr->second=="\t" and numb_to_widths.count(itr->first)==1)
          {
            space_index = itr->first;
          }
        else if(space_index!=-1)
          {
            break;
          }
        else
          {}
      }
    
    for(auto itr=diff_numb_to_char.begin(); itr!=diff_numb_to_char.end(); itr++)
      {
        if(space_index==-1 and itr->second=="\t" and numb_to_widths.count(itr->first)==1)
          {
            space_index = itr->first;
          }
        else if(space_index!=-1)
          {
            break;
          }
        else
          {}
      }

    // just a guess ...
    if(space_index==-1 and get_string(32)==" ")
      {
        space_index = 32;
      }

    for(int ind=fchar; ind<=lchar; ind++)
      {
        if(space_index==-1 and ind!=-1 and get_string(ind)==" ")
          {
            space_index = ind;
          }
        else if(space_index!=-1)
          {
            break;
          }
        else
          {}
      }
  }

  void pdf_resource<PAGE_FONT>::print_tables()
  {
    LOG_S(INFO) << __FUNCTION__;

    std::set<uint32_t> numbs;
    
    for(auto itr=numb_to_widths.begin(); itr!=numb_to_widths.end(); itr++)
      {
        numbs.insert(itr->first);
      }
    
    for(auto itr=cmap_numb_to_char.begin(); itr!=cmap_numb_to_char.end(); itr++)
      {
        numbs.insert(itr->first);
      }
    
    for(auto itr=diff_numb_to_char.begin(); itr!=diff_numb_to_char.end(); itr++)
      {
        numbs.insert(itr->first);
      }
    
    LOG_S(INFO) << "tables of " << font_name;
    LOG_S(INFO) << "space-index: " << space_index;
    LOG_S(INFO) << std::setw(16) << "counter" 
		<< std::setw(16) << "number" 
		<< std::setw(16) << "numb_to_widths" 
		<< std::setw(16) << "get_width" 
		<< std::setw(16) << "cmap" 
		<< std::setw(16) << "diff";

    int num=32;

    int l=0;
    for(auto numb:numbs)
      {
        std::string width = " --- ";
        if(numb_to_widths.count(numb)==1)
          width = std::to_string(numb_to_widths[numb]);

        std::string width_ = " --- ";
	width_ = std::to_string(get_width(numb, false));
        
        std::string cmap = " --- ";
        if(cmap_numb_to_char.count(numb)==1)
          cmap = "'"+cmap_numb_to_char.at(numb)+"'";
        
        std::string diff = " --- ";
        if(diff_numb_to_char.count(numb)==1)
          diff = "'"+diff_numb_to_char[numb]+"'";

	if(l<num/2)
	  {
	    LOG_S(INFO) << std::setw(16) << l
			<< std::setw(16) << numb 
			<< std::setw(16) << width 
			<< std::setw(16) << width_ 
			<< std::setw(16) << cmap 
			<< std::setw(16) << diff;
	  }
	else if(l==num/2 and numbs.size()>num/2)
	  {
	    LOG_S(WARNING) << "... ignoring lines ..."; 
	  }
	else if(numbs.size()-num/4<l)
	  {
	    LOG_S(INFO) << std::setw(16) << l 
			<< std::setw(16) << numb 
			<< std::setw(16) << width 
			<< std::setw(16) << width_ 
			<< std::setw(16) << cmap 
			<< std::setw(16) << diff;
	  }	
	else 
	  {}

	l += 1;
      }
  }

}

#endif
