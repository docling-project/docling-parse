//-*-C++-*-

#ifndef PDF_PAGE_FONT_RESOURCE_H
#define PDF_PAGE_FONT_RESOURCE_H

#include <parse/utils/ccitt/ccitt_utils.h>

#include <parse/qpdf/qpdf_compat.h>

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_FONT>
  {
  public:

    bool is_type3() const { return subtype == TYPE_3; }

    // One Type3 glyph, extracted from its /CharProcs procedure. The dominant
    // Type3 pattern (TeX / dvipdfm output) draws each glyph as a single inline
    // 1-bit image mask placed by a `cm`; this captures exactly that shape.
    // Glyphs whose procedure does not match stay invalid and draw nothing --
    // preferable to the black placeholder boxes they used to produce.
    struct type3_glyph
    {
      bool valid = false;
      int w = 0;
      int h = 0;
      // 8-bit mask in the renderer's image-mask convention: 0 paints the fill
      // colour, 255 leaves the page.
      std::shared_ptr<std::vector<uint8_t> > mask;
      // Maps the image's unit square into glyph space (the charproc's cm).
      std::array<double, 6> cm = {1, 0, 0, 1, 0, 0};
    };

    // /FontMatrix: glyph space -> text space (Type3 only; identity-scaled
    // 0.001 default matches the spec default for simple fonts).
    const std::array<double, 6>& get_font_matrix();

    // Lazily extracts and caches the glyph for a character code; nullptr when
    // the code has no procedure or the procedure is not the inline-mask form.
    std::shared_ptr<type3_glyph> get_type3_glyph(uint32_t code);



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

    // Vertical writing mode (9.7.4.3): a composite font whose CMap sets
    // WMode 1 stacks its glyphs downwards instead of advancing to the right.
    bool        is_vertical() const { return vertical; }

    // Vertical displacement w1 of a CID, in glyph space (/W2, /DW2). It is
    // normally negative: the next glyph sits below this one.
    double      get_vertical_displacement(uint32_t c) const;

    // Vertical component of the position vector v, in glyph space. The glyph
    // is drawn with its horizontal origin this far below the current point.
    double      get_vertical_origin_y() const { return vertical_origin_y; }
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

    // Codespace ranges of the predefined CMap named by /Encoding. They decide
    // how many bytes each code in a show-string occupies; empty for a font
    // that is not driven by a cmap-resource.
    const std::vector<cmap_codespace_range>& get_cmap_codespaces() const;
    
    void set(std::string font_key_,
             QPDFObjectHandle qpdf_font_);

  private:

    std::string get_correct_character(uint32_t c);
    std::string resolve_unmapped_character(uint32_t c);
    std::string get_character_from_encoding(uint32_t c);

    std::shared_ptr<type3_glyph> parse_type3_charproc(const std::string& src);

    // Charprocs with no inline image: filled paths (re / m l c h f). The
    // paths are flattened and scanline-rasterised into the mask.
    std::shared_ptr<type3_glyph> rasterize_type3_vector_charproc(const std::string& src);

    void init_encoding();

    // Reads the CMap program a Type0 font carries in /Encoding, keeping its
    // codespace ranges so that show-strings can be split into codes.
    void init_embedded_cmap(QPDFObjectHandle qpdf_encoding);
    void init_subtype();

    void init_base_font();
    void init_font_name();
    void init_font_bbox();
    void init_font_matrix();
    void init_font_program();
    bool try_init_font_program_from_descriptor(QPDFObjectHandle font_obj,
                                               bool from_descendant_font);
    bool try_init_font_program_direct(QPDFObjectHandle font_obj,
                                      bool from_descendant_font);
    void populate_font_program(QPDFObjectHandle descriptor_obj,
                               QPDFObjectHandle stream_obj,
                               std::string const& source_path,
                               embedded_font_file_kind kind,
                               bool from_descendant_font);

    embedded_font_format resolve_embedded_font_format() const;
    bool resolve_cid_to_gid_identity() const;
    void build_embedded_font_blob();
    
    // Resolution of this font onto one of the built-in base fonts. See the
    // matched_*_ members for why these are cached.
    const base_font_match& matched_font_name();
    const base_font_match& matched_base_font();

    void init_ascent_and_descent();

    void init_default_width();

    void init_char_widths();

    void init_vertical_metrics();

    void init_fchar();
    void init_lchar();
    void init_widths();
    void init_ws();

    void init_cmap(pdf_timings& timings);
    void init_cmap_resource();

    void init_font_flags();

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

    // /FontMatrix and the Type3 glyphs extracted from /CharProcs, cached per
    // character code: a charproc is a content stream, so it is parsed once.
    std::array<double, 6> font_matrix_ = {0.001, 0, 0, 0.001, 0, 0};
    bool font_matrix_read_ = false;
    std::unordered_map<uint32_t, std::shared_ptr<type3_glyph> > type3_cache_;

    // Matching a PDF font name onto one of the ~190 built-in base fonts scans
    // all of them with a substring test per entry, and the call sites below ask
    // per glyph. font_name and base_font are both fixed once init_font_name()
    // has run, so each resolution is computed at most once per font resource.
    // matched_base_font_ backs the call sites that fall back to /BaseFont;
    // matched_font_name_ backs get_character_from_encoding(), which must not.
    bool            matched_font_name_resolved_ = false;
    base_font_match matched_font_name_;
    bool            matched_base_font_resolved_ = false;
    base_font_match matched_base_font_;

    nlohmann::json   json_font;

    QPDFObjectHandle qpdf_font;
    QPDFObjectHandle qpdf_desc_font; // derived from qpdf_font, only for '/Type-0'

    std::string        encoding_name;
    font_encoding_name encoding;
    bool               has_explicit_encoding; // true if encoding was found in PDF, false if defaulted
    bool               is_symbolic = false;   // /FontDescriptor /Flags bit 3 (PDF 32000-1 table 123)

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

    // vertical writing mode: /DW2 defaults to [880 -1000] (Table 117)
    bool   vertical = false;
    double vertical_origin_y = 0.880;
    double default_vertical_displacement = -1.000;
    std::unordered_map<uint32_t, double> numb_to_vertical_displacements;

    std::unordered_map<uint32_t   , double> numb_to_widths;
    std::unordered_map<std::string, double> name_to_widths;

    std::unordered_map<std::string, char_description> name_to_descr;

    bool cmap_initialized;
    bool diff_initialized;

    //std::unordered_map<uint32_t, std::string> cmap_numb_to_char;
    cmap_value cmap_numb_to_char;
    std::vector<cmap_codespace_range> cmap_codespaces;
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
    if(json_font.is_null() and not qpdf_font.isNull())
      {
        json_font = to_json(qpdf_font);
      }

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

  const std::vector<cmap_codespace_range>&
  pdf_resource<PAGE_FONT>::get_cmap_codespaces() const
  {
    return cmap_codespaces;
  }

  const base_font_match& pdf_resource<PAGE_FONT>::matched_font_name()
  {
    if(not matched_font_name_resolved_)
      {
        matched_font_name_ = bfonts.find_corresponding_font(font_name);
        matched_font_name_resolved_ = true;
      }

    return matched_font_name_;
  }

  const base_font_match& pdf_resource<PAGE_FONT>::matched_base_font()
  {
    if(not matched_base_font_resolved_)
      {
        // /FontDescriptor /FontName takes precedence: /BaseFont is consulted
        // only when the font name matches no built-in font at all.
        matched_base_font_ = matched_font_name();

        if(not matched_base_font_.font)
          {
            matched_base_font_ = bfonts.find_corresponding_font(base_font);
          }

        matched_base_font_resolved_ = true;
      }

    return matched_base_font_;
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
    else if(matched_base_font().font)
      {
	const std::string& fontname = matched_base_font().name;

        auto& bfont = *(matched_base_font().font);

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
    if(matched_base_font().font)
      {
        return matched_base_font().font->has_char_bbox(c);
      }

    return false;
  }

  std::array<double, 4> pdf_resource<PAGE_FONT>::get_char_bbox(const uint32_t& c)
  {
    // Every caller guards with has_char_bbox(). When nothing matched, keep the
    // previous behaviour of resolving the "Unknown" sentinel, which throws.
    if(not matched_base_font().font)
      {
        return bfonts.get("Unknown")->get_char_bbox(c);
      }

    return matched_base_font().font->get_char_bbox(c);
  }

  bool pdf_resource<PAGE_FONT>::has_char_bbox(const std::string& c)
  {
    if(matched_base_font().font)
      {
        return matched_base_font().font->has_char_bbox(c);
      }

    return false;
  }

  std::array<double, 4> pdf_resource<PAGE_FONT>::get_char_bbox(const std::string& c)
  {
    // Every caller guards with has_char_bbox(). When nothing matched, keep the
    // previous behaviour of resolving the "Unknown" sentinel, which throws.
    if(not matched_base_font().font)
      {
        return bfonts.get("Unknown")->get_char_bbox(c);
      }

    return matched_base_font().font->get_char_bbox(c);
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
	  // Without a /ToUnicode or a predefined CID cmap the code is only a
	  // CID; interpreting it as a scalar value is already a last resort,
	  // and it is not even possible for the values that Unicode reserves
	  // for surrogates or places beyond U+10FFFF.
	  else if(32<=c and utf8::internal::is_code_point_valid(c))
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
      case CMAP_STREAM:
	{
          if(cmap_numb_to_char.count(c))
	    {
	      return cmap_numb_to_char.at(c);
	    }
	  else if(32<=c and utf8::internal::is_code_point_valid(c))
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
    // For codes covered by /Encoding/Differences, diff_numb_to_char
    // already encodes the precedence of PDF 32000-1 section 9.10.2:
    // init_differences() resolves each code via the /ToUnicode cmap
    // first and only falls back to glyph-name based methods. For codes
    // not covered by /Differences, the cmap is consulted directly below.

    if(diff_initialized and diff_numb_to_char.count(c)>0)
      {
        return diff_numb_to_char.at(c);
      }
    else if(cmap_initialized and cmap_numb_to_char.count(c)>0)
      {
        return cmap_numb_to_char.at(c);
      }
    else if(matched_font_name().font)
      {
        // check if the font-name is registered as a 'special' font, eg
        // the TeX mathematical fonts. Note this deliberately does not fall back
        // to /BaseFont the way the metrics lookups do.

        const std::string& fontname = matched_font_name().name;

        auto& fm = *(matched_font_name().font);

        // If font declares a specific encoding (MacRoman, WinAnsi, etc.) AND it was
        // explicitly specified in the PDF, use that encoding instead of base font's built-in mapping
        if(has_explicit_encoding &&
           (encoding == MACROMAN || encoding == MACEXPERT || encoding == WINANSI || encoding == STANDARD))
          {
            return resolve_unmapped_character(c);
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
	    return resolve_unmapped_character(c);
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

	    return resolve_unmapped_character(c);
	  }
      }
    else
      {
	//LOG_S(WARNING) << "no known font: " << font_name;
        return resolve_unmapped_character(c);
      }
  }

  void pdf_resource<PAGE_FONT>::init_font_flags()
  {
    const int FLAG_SYMBOLIC = 1 << 2; // /Flags bit 3 (PDF 32000-1 table 123)

    int flags = 0;
    if(not qpdf_object::get_int(qpdf_font, {"/FontDescriptor", "/Flags"}, flags))
      {
        qpdf_object::get_int(qpdf_desc_font, {"/FontDescriptor", "/Flags"}, flags);
      }

    is_symbolic = ((flags & FLAG_SYMBOLIC) != 0);

    LOG_S(INFO) << __FUNCTION__ << ": flags=" << flags
                << ", is_symbolic=" << is_symbolic;
  }

  std::string pdf_resource<PAGE_FONT>::resolve_unmapped_character(uint32_t c)
  {
    // Fallback for codes that none of the authoritative sources resolved
    // (/Encoding/Differences, the /ToUnicode or predefined CID cmap, or a
    // known base-font table). When an explicit cmap exists but does not
    // cover this code, and the code bears no relation to the standard
    // Latin encodings (symbolic font, or composite font whose codes are
    // CIDs), the encoding tables would fabricate unrelated text — e.g. an
    // Arabic ligature glyph subsetted at code 0x23 coming out as '#'
    // (docling#3802). Emit a glyph marker instead, so downstream consumers
    // can detect the unresolved glyph.
    if(cmap_initialized and (is_symbolic or subtype==TYPE_0))
      {
        unknown_numbs[c] += 1;

        LOG_S(WARNING) << "Symbol not in the cmap of a symbolic or composite font: "
                       << int(c) << "; font-name: " << font_name
                       << "; emitting glyph marker instead of encoding fallback";

        return "GLYPH<" + std::to_string(c) + ">";
      }

    return get_character_from_encoding(c);
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

  void pdf_resource<PAGE_FONT>::set(std::string font_key_,
                                    QPDFObjectHandle qpdf_font_)
  {
    LOG_S(INFO) << __FUNCTION__ << " font: " << font_key_;

    /*
      if(true)
      {
      print_obj(qpdf_font_);
      
      try
      {
      LOG_S(INFO) << "font [key='" << font_key_ << "']:\n" << qpdf_object::debug(qpdf_font_);
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
      json_font = nullptr;
      qpdf_font = qpdf_font_;
      qpdf_desc_font = QPDFObjectHandle::newNull();

      double font_time = font_timer.get_time();
      timings.add_timing(pdf_timings::KEY_FONT_INIT_COPY, font_time);
    }
    
    {
      utils::timer font_timer;
      
      init_encoding();
      init_subtype();

      init_font_flags();

      init_base_font();
      
      init_font_name();
      init_font_bbox();
      init_font_matrix();
      // init_font_program(); // extraction is lazy: see get_embedded_font_blob()
      
      init_ascent_and_descent();
      
      init_default_width();
      
      init_char_widths();

      init_vertical_metrics();

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

    std::string name;
    if(qpdf_object::get_name_or_string(qpdf_font, {"/Encoding", "/BaseEncoding"}, name))
      {
        encoding = to_encoding_name(name);
        has_explicit_encoding = true;

        LOG_S(INFO) << "font-encoding [" << name << "]: " << to_string(encoding);
      }
    else if(qpdf_font.isDictionary() and qpdf_font.hasKey("/Encoding"))
      {
        auto result = qpdf_font.getKey("/Encoding");

        if(result.isStream())
          {
	    // /Encoding is a CMap program carried by the file itself (ISO
	    // 32000-1, 9.7.5.2). Its codespace is what says how many bytes each
	    // code takes; reading such a string one byte at a time split every
	    // two-byte code into a phantom glyph plus the real one, which shifted
	    // the whole run to the right of the background it was drawn on.
	    encoding = CMAP_STREAM;
	    has_explicit_encoding = true;

	    init_embedded_cmap(result);

            LOG_S(INFO) << "font-encoding [embedded cmap]: " << to_string(encoding);
          }
        else if(qpdf_object::get_name_or_string(result, encoding_name))
          {
	    if(cids.has(encoding_name))
	      {
		encoding = CMAP_RESOURCES;
		has_explicit_encoding = true;
	      }
	    else
	      {
		encoding = to_encoding_name(encoding_name);
		has_explicit_encoding = true;
	      }

            LOG_S(INFO) << "font-encoding [" << name << "]: " << to_string(encoding);
          }
        else
          {
            LOG_S(WARNING) << " --> font-encoding falling back to STANDARD with font-encoding [object]: "
                           << qpdf_object::debug(result);

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

  void pdf_resource<PAGE_FONT>::init_embedded_cmap(QPDFObjectHandle qpdf_encoding)
  {
    LOG_S(INFO) << __FUNCTION__;

    if(not qpdf_encoding.isStream())
      {
        LOG_S(WARNING) << "/Encoding is not a stream: no embedded cmap to read";
        return;
      }

    std::string program;
    try
      {
        auto buffer = qpdf_encoding.getStreamData(qpdf_dl_all);
        if(buffer)
          {
            program.assign(reinterpret_cast<const char*>(buffer->getBuffer()),
                           buffer->getSize());
          }
      }
    catch(const std::exception& e)
      {
        LOG_S(ERROR) << "could not decode the embedded cmap: " << e.what();
        return;
      }

    cmap_tables tables;

    // An embedded CMap may inherit from a predefined one. Only its name is
    // available here, so the inherited codespace is reported rather than
    // silently assumed.
    auto report_usecmap = [](const std::string& parent)
    {
      LOG_S(WARNING) << "embedded cmap inherits from " << parent
                     << ": the inherited entries are not applied";
    };

    std::istringstream in(program);
    scan_cmap_program(in, tables, report_usecmap);

    cmap_codespaces = tables.codespaces;

    if(cmap_codespaces.empty())
      {
        // Every conforming CMap declares a codespace; when one does not, two
        // bytes per code is what a Type0 font almost always means, and it is
        // the assumption that keeps the reader in step with the string.
        LOG_S(WARNING) << "embedded cmap declares no codespace: "
                       << "assuming two-byte codes";

        cmap_codespaces.push_back(cmap_codespace_range{0x0000, 0xFFFF, 2});
      }

    LOG_S(INFO) << "embedded cmap: " << cmap_codespaces.size() << " codespace-range(s), "
                << tables.code_to_cid.size() << " code-to-cid entries";
  }

  void pdf_resource<PAGE_FONT>::init_subtype()
  {
    LOG_S(INFO) << __FUNCTION__;

    std::string name;
    if(qpdf_object::get_name_or_string(qpdf_font, {"/Subtype"}, name))
      {
        subtype = to_subtype_name(name);

        LOG_S(INFO) << "subtype [" << name << "]: " << to_string(subtype);

        QPDFObjectHandle desc_fonts = qpdf_object::get_path(qpdf_font, {"/DescendantFonts"});
        if(subtype==TYPE_0 and desc_fonts.isArray())
          {
	    if(desc_fonts.getArrayNItems()==1)
	      {
		LOG_S(INFO) << "found the descendant font";// << desc_font.dump(2);
		qpdf_desc_font = desc_fonts.getArrayItem(0);
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
        LOG_S(ERROR) << "could not find subtype in font: " << qpdf_object::debug(qpdf_font);
      }
  }

  void pdf_resource<PAGE_FONT>::init_base_font()
  {
    LOG_S(INFO) << __FUNCTION__;

    base_font = "null";
    if(qpdf_object::get_name_or_string(qpdf_font, {"/BaseFont"}, base_font))
      {
        LOG_S(INFO) << "base-font: " << base_font;
      }
    else if(qpdf_object::get_name_or_string(qpdf_desc_font, {"/BaseFont"}, base_font))
      {
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

    font_name = "null";
    if(qpdf_object::get_name_or_string(qpdf_font, {"/FontDescriptor", "/FontName"}, font_name))
      {
        LOG_S(INFO) << "font-name: " << font_name;
      }
    else if(qpdf_object::get_name_or_string(qpdf_desc_font, {"/FontDescriptor", "/FontName"}, font_name))
      {
        LOG_S(INFO) << "font-name: " << font_name;
      }
    else if(qpdf_object::get_name_or_string(qpdf_font, {"/Name"}, font_name))
      {
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

    bool found_bbox = false;
    
    if(qpdf_object::get_number_array(qpdf_font, {"/FontDescriptor", "/FontBBox"}, font_bbox))
      {
        found_bbox = true;
      }
    else if(qpdf_object::get_number_array(qpdf_desc_font, {"/FontDescriptor", "/FontBBox"}, font_bbox))
      {
        found_bbox = true;
      }
    else if(qpdf_object::get_number_array(qpdf_font, {"/FontBBox"}, font_bbox))
      {
        //assert(subtype==TYPE_3);
        found_bbox = true;
      }
    else if(qpdf_object::get_number_array(qpdf_desc_font, {"/FontBBox"}, font_bbox))
      {
        //assert(subtype==TYPE_3);
        found_bbox = true;
      }
    else if(bfonts.has(base_font)==1)
      {
        LOG_S(WARNING) << "font-bbox retrieved from base-font";
        font_bbox = bfonts[base_font]->get_font_bbox();
      }
    else
      {
        LOG_S(WARNING) << "could not find font-bbox";
      }

    if (not found_bbox)
      {
        QPDFObjectHandle bbox = qpdf_object::get_path(qpdf_font, {"/FontDescriptor", "/FontBBox"});
        if(bbox.isNull()) { bbox = qpdf_object::get_path(qpdf_desc_font, {"/FontDescriptor", "/FontBBox"}); }
        if(bbox.isNull()) { bbox = qpdf_object::get_path(qpdf_font, {"/FontBBox"}); }
        if(bbox.isNull()) { bbox = qpdf_object::get_path(qpdf_desc_font, {"/FontBBox"}); }
        if(not bbox.isNull())
          {
            LOG_S(ERROR) << "expected 4 numeric elements in font-bbox, got: "
                         << qpdf_object::debug(bbox);
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

    if(qpdf_object::get_number_array(qpdf_font, {"/FontMatrix"}, font_matrix))
      {
        //assert(subtype==TYPE_3);
        type3_xscale = font_matrix[0] * 1000.0;
        type3_yscale = font_matrix[3] * 1000.0;
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
    found = try_init_font_program_from_descriptor(qpdf_font, false);

    if(not found and subtype==TYPE_0 and not qpdf_desc_font.isNull())
      {
        LOG_S(INFO) << __FUNCTION__ << ": probing descendant font descriptor";
        found = try_init_font_program_from_descriptor(qpdf_desc_font, true);
        if(not found)
          {
            LOG_S(INFO) << __FUNCTION__ << ": probing descendant font directly";
            found = try_init_font_program_direct(qpdf_desc_font, true);
          }
      }

    if(not found)
      {
        LOG_S(INFO) << __FUNCTION__ << ": probing primary font object directly";
        found = try_init_font_program_direct(qpdf_font, false);
      }

    if(not found and subtype==TYPE_0 and not qpdf_desc_font.isNull())
      {
        LOG_S(INFO) << __FUNCTION__ << ": probing descendant font directly as final fallback";
        found = try_init_font_program_direct(qpdf_desc_font, true);
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
    font_program.descriptor_json = to_json(descriptor_obj, {}, 0, 2);
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

    // The effective encoding of a simple font is the base encoding the font
    // dictionary declares, with /Differences applied on top (ISO 32000-1,
    // 9.6.6.2) -- and it takes precedence over whatever encoding the font
    // program carries. Returning a name only for the codes /Differences
    // mentions left every other code to be looked up through the program's
    // builtin encoding instead, so a /WinAnsiEncoding code 0xE1 drew the
    // Standard-encoding glyph at that slot: `Æ` where the page says `á`.
    //
    // A font dictionary that declares no base encoding is the one case where
    // the program's own encoding governs, so nothing is claimed for it here.
    if(has_explicit_encoding and
       (encoding==STANDARD  or encoding==MACROMAN or
        encoding==MACEXPERT or encoding==WINANSI))
      {
        auto& numb_to_name = encodings.get(encoding).get_numb_to_name();

        auto base = numb_to_name.find(code);
        if(base != numb_to_name.end())
          {
            return base->second;
          }
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
    QPDFObjectHandle value = qpdf_object::get_path(qpdf_desc_font, {"/CIDToGIDMap"});
    if(value.isNull())
      {
        return true;
      }

    std::string name;
    return qpdf_object::get_name_or_string(value, name) and name == "/Identity";
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

    // A symbolic simple font without /Encoding maps its character codes
    // through the font program's builtin cmap (9.6.6.4); the renderer must
    // then resolve glyphs by character code, not by Unicode text.
    bool uses_builtin_encoding = false;
    {
      const bool has_encoding = qpdf_object::has_path(qpdf_font, {"/Encoding"});

      uses_builtin_encoding = (subtype != TYPE_0) and
        is_symbolic and (not has_encoding);
    }

    font_blob = std::make_shared<const embedded_font_blob>(
      embedded_font_blob::compute_cache_key(*bytes),
      font_name,
      base_font,
      font_program.source_path,
      format,
      subtype == TYPE_0,
      resolve_cid_to_gid_identity(),
      uses_builtin_encoding,
      std::move(bytes));

    LOG_S(INFO) << __FUNCTION__
                << ": font-key=" << font_key
                << " font-name=" << font_name
                << " source=" << font_blob->get_source_key()
                << " format=" << to_string(font_blob->get_format())
                << " bytes=" << font_blob->byte_size()
                << " cache-key=" << font_blob->get_cache_key()
                << " cid=" << font_blob->get_is_cid_font()
                << " cid-to-gid-identity=" << font_blob->get_cid_to_gid_identity()
                << " uses-builtin-encoding=" << font_blob->get_uses_builtin_encoding();
  }

  void pdf_resource<PAGE_FONT>::init_ascent_and_descent()
  {
    LOG_S(INFO) << __FUNCTION__;

    ascent=0;
    {
      bool ascent_defined=false;
      if(qpdf_object::get_number(qpdf_font, {"/FontDescriptor", "/Ascent"}, ascent))
        {
          ascent_defined=true;

          LOG_S(INFO) << "ascent: " << ascent;
        }
      else if(qpdf_object::get_number(qpdf_desc_font, {"/FontDescriptor", "/Ascent"}, ascent))
        {
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
              ascent = bfonts[base_font]->get_ascend();
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
      bool descent_defined=false;
      if(qpdf_object::get_number(qpdf_font, {"/FontDescriptor", "/Descent"}, descent))
        {
          descent_defined=true;

          LOG_S(INFO) << "descent: " << descent;
        }
      else if(qpdf_object::get_number(qpdf_desc_font, {"/FontDescriptor", "/Descent"}, descent))
        {
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
              descent = bfonts[base_font]->get_descend();
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
      //bool capheight_defined=false;
      if(qpdf_object::get_number(qpdf_font, {"/FontDescriptor", "/CapHeight"}, capheight))
        {
          //capheight_defined=true;

          LOG_S(INFO) << "capheight: " << capheight;
        }
      else if(qpdf_object::get_number(qpdf_desc_font, {"/FontDescriptor", "/CapHeight"}, capheight))
        {
          //capheight_defined=true;

          LOG_S(INFO) << "capheight: " << capheight;
        }
      else
        {
          LOG_S(WARNING) << "'capheight' was not explicitely defined ...";
	  if(bfonts.has(base_font))
	    {
	      capheight = bfonts[base_font]->get_capheight();
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
      //bool xheight_defined=false;
      if(qpdf_object::get_number(qpdf_font, {"/FontDescriptor", "/XHeight"}, xheight))
        {
          //xheight_defined=true;

          LOG_S(INFO) << "xheight: " << xheight;
        }
      else if(qpdf_object::get_number(qpdf_desc_font, {"/FontDescriptor", "/XHeight"}, xheight))
        {
          //xheight_defined=true;

          LOG_S(INFO) << "xheight: " << xheight;
        }
      else
        {
          LOG_S(WARNING) << "'xheight' was not explicitely defined ...";
	  if(bfonts.has(base_font))
	    {
	      xheight = bfonts[base_font]->get_xheight();
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

    if(qpdf_object::get_number(qpdf_font, {"/DW"}, default_width))
      {
	has_default_width = true;

        LOG_S(INFO) << "default-width: " << default_width;
      }
    else if(qpdf_object::get_number(qpdf_desc_font, {"/DW"}, default_width))
      {
	has_default_width = true;

        LOG_S(INFO) << "default-width: " << default_width;
      }
    else
      {
        // ISO 32000-1 table 115: /DW defaults to 1000, one full em. Using 500
        // halves every advance of a CID font that omits it, so its cells come
        // out half the width of the glyphs actually drawn.
	default_width = 1000;
        // /DW belongs to a CID font, and only there does its default stand in
        // for a missing width. A simple font must keep falling through to its
        // base-14 metrics, which carry real per-glyph advances.
	has_default_width = (subtype == TYPE_0);
        LOG_S(WARNING) << "could not find default-width: defaulting to " << default_width;
      }    
  }

  // 9.7.4.3: the writing mode comes from the CMap. Every predefined vertical
  // CMap ends in "-V", and /Identity-V is the one that matters in practice; an
  // embedded CMap stream would say so through its own /WMode, which is not
  // read here.
  //
  // The metrics come from /DW2 and /W2 on the descendant font (Table 117).
  // /DW2 is [v_y w1] in glyph space units, defaulting to [880 -1000].
  void pdf_resource<PAGE_FONT>::init_vertical_metrics()
  {
    LOG_S(INFO) << __FUNCTION__;

    vertical = encoding_name.size() >= 2 and
               encoding_name.compare(encoding_name.size() - 2, 2, "-V") == 0;

    if(not vertical)
      {
        return;
      }

    LOG_S(INFO) << "vertical writing mode from encoding " << encoding_name;

    {
      QPDFObjectHandle dw2 = qpdf_object::get_path(qpdf_font, {"/DW2"});
      if(dw2.isNull()) { dw2 = qpdf_object::get_path(qpdf_desc_font, {"/DW2"}); }

      if(dw2.isArray() and dw2.getArrayNItems() >= 2)
        {
          double origin_y = 0.0;
          double displacement = 0.0;
          QPDFObjectHandle origin_obj = dw2.getArrayItem(0);
          QPDFObjectHandle displacement_obj = dw2.getArrayItem(1);
          if(qpdf_object::get_number(origin_obj, origin_y) and
             qpdf_object::get_number(displacement_obj, displacement))
            {
              vertical_origin_y = origin_y / 1000.0;
              default_vertical_displacement = displacement / 1000.0;
            }
        }

      LOG_S(INFO) << "vertical metrics: origin-y " << vertical_origin_y
                  << ", displacement " << default_vertical_displacement;
    }

    // /W2 entries come as `c [w1 v_x v_y ...]` or `c_first c_last w1 v_x v_y`;
    // only w1 is read, since the horizontal half of v is taken as w0/2 either
    // way and its vertical half rarely differs from /DW2.
    QPDFObjectHandle w2 = qpdf_object::get_path(qpdf_font, {"/W2"});
    if(w2.isNull()) { w2 = qpdf_object::get_path(qpdf_desc_font, {"/W2"}); }

    if(not w2.isArray())
      {
        return;
      }

    for(int l = 0; l + 1 < w2.getArrayNItems(); )
      {
        QPDFObjectHandle beg_obj = w2.getArrayItem(l);
        if(not beg_obj.isNumber())
          {
            LOG_S(WARNING) << "/W2 entry " << l << " is not a CID";
            break;
          }

        const int beg = static_cast<int>(utils::numeric::locale_safe_numeric_value(beg_obj));
        l += 1;

        QPDFObjectHandle value_obj = w2.getArrayItem(l);
        if(value_obj.isArray())
          {
            std::vector<double> triples = qpdf_object::get_number_array(value_obj);
            l += 1;

            for(std::size_t k = 0; k + 2 < triples.size(); k += 3)
              {
                numb_to_vertical_displacements[static_cast<uint32_t>(beg + k / 3)] =
                  triples[k] / 1000.0;
              }
          }
        else if(l + 3 < w2.getArrayNItems())
          {
            QPDFObjectHandle end_obj = w2.getArrayItem(l);
            QPDFObjectHandle w1_obj = w2.getArrayItem(l + 1);
            if(not end_obj.isNumber() or not w1_obj.isNumber())
              {
                LOG_S(WARNING) << "/W2 range contains non-numeric values";
                break;
              }

            const int end = static_cast<int>(utils::numeric::locale_safe_numeric_value(end_obj));
            const double w1 = utils::numeric::locale_safe_numeric_value(w1_obj) / 1000.0;
            l += 4;  // c_last, w1, v_x, v_y

            for(int id = beg; id <= end; id++)
              {
                numb_to_vertical_displacements[static_cast<uint32_t>(id)] = w1;
              }
          }
        else
          {
            LOG_S(WARNING) << "/W2 ends in the middle of a range";
            break;
          }
      }
  }

  double pdf_resource<PAGE_FONT>::get_vertical_displacement(uint32_t c) const
  {
    auto itr = numb_to_vertical_displacements.find(c);
    if(itr != numb_to_vertical_displacements.end())
      {
        return itr->second;
      }

    return default_vertical_displacement;
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

    if(qpdf_object::get_int(qpdf_font, {"/FirstChar"}, fchar))
      {
        LOG_S(INFO) << "fchar: " << fchar;
      }
    else if(qpdf_object::get_int(qpdf_desc_font, {"/FirstChar"}, fchar))
      {
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

    if(qpdf_object::get_int(qpdf_font, {"/LastChar"}, lchar))
      {
        LOG_S(INFO) << "lchar: " << lchar;
      }
    else if(qpdf_object::get_int(qpdf_desc_font, {"/LastChar"}, lchar))
      {
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
      bool found_widths = false;
      QPDFObjectHandle widths = qpdf_object::get_path(qpdf_font, {"/Widths"});
      if(widths.isNull()) { widths = qpdf_object::get_path(qpdf_desc_font, {"/Widths"}); }

      if(widths.isArray() and (not found_widths))
        {
          LOG_S(INFO) << "widths: " << qpdf_object::debug(widths);
          values = qpdf_object::get_number_array(widths);
          found_widths = true;
        }
      if(not found_widths)
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

    QPDFObjectHandle ws;

    {
      ws = qpdf_object::get_path(qpdf_font, {"/W"});
      if(ws.isNull()) { ws = qpdf_object::get_path(qpdf_desc_font, {"/W"}); }

      if(not ws.isArray())
        {
          LOG_S(WARNING) << "could not find '/W'";
          return;
        }

      LOG_S(INFO) << "detected '/W'";
    }

    int beg=-1;
    int end=-1;

    for(int l=0; l<ws.getArrayNItems(); )
      {
        QPDFObjectHandle beg_obj = ws.getArrayItem(l);
        LOG_S(INFO) << l << "\t" << beg_obj.isNumber() << "\t beg: " << qpdf_object::debug(beg_obj);

        //assert(l<ws.size());
	
        if(not beg_obj.isNumber())
          {
            LOG_S(WARNING) << "/W entry " << l << " is not a CID";
            break;
          }

        beg = static_cast<int>(utils::numeric::locale_safe_numeric_value(beg_obj));
        l += 1;

        if(l==0)
          {
            fchar=beg;
          }

        if(l>=ws.getArrayNItems())
          {
            LOG_S(WARNING) << "index " << l << " is out of bounds " << ws.getArrayNItems();
            continue;
          }

        QPDFObjectHandle value_obj = ws.getArrayItem(l);
        if(value_obj.isNumber())
          {
            end = static_cast<int>(utils::numeric::locale_safe_numeric_value(value_obj));
            l += 1;

	    if(l>=ws.getArrayNItems())
	      {
		LOG_S(WARNING) << "index " << l << " is out of bounds " << ws.getArrayNItems();
		continue;
	      }

            QPDFObjectHandle width_obj = ws.getArrayItem(l);
            if(not width_obj.isNumber())
              {
                LOG_S(WARNING) << "/W range width is not numeric: " << qpdf_object::debug(width_obj);
                break;
              }

            double w = utils::numeric::locale_safe_numeric_value(width_obj);
            l += 1;

            for(int id=beg; id<=end; id++)
              {
		//LOG_S(WARNING) << "\t" << id << " -> " << w;
                numb_to_widths[id] = w * type3_xscale;
              }
          }
        else if(value_obj.isArray())
          {
            std::vector<double> w = qpdf_object::get_number_array(value_obj);
            l += 1;

            for(int k=0; k<w.size(); k++)
              {
		//LOG_S(WARNING) << "\t" << beg+k  << " -> " << w[k];

                numb_to_widths[beg+k] = w[k] * type3_xscale;
              }
          }
        else if(value_obj.isNull())
          {
	    LOG_S(WARNING) << "\t ws[" << l << "] is null ... skipping now";
	    l += 1;
	  }
        else
          {
	    std::stringstream message;
	    message <<  "unknown type in " << __FUNCTION__ << " for " << qpdf_object::debug(ws);

	    LOG_S(ERROR) << message.str();
	    throw std::logic_error(message.str());
          }
      }
  }

  void pdf_resource<PAGE_FONT>::init_cmap(pdf_timings& timings)
  {
    LOG_S(INFO) << __FUNCTION__;

    QPDFObjectHandle qpdf_obj = qpdf_object::get_path(qpdf_font, {"/ToUnicode"});

    if(not qpdf_obj.isNull())
      {
        LOG_S(INFO) << "found a /ToUnicode cmap: starting to decode ...";

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
	    std::string message = "qpdf_obj.isString(): " + qpdf_object::debug(qpdf_obj);

	    LOG_S(ERROR) << message;
	    throw std::logic_error(message);
	  }
	else if(qpdf_obj.isName())
	  {
	    std::string message = "qpdf_obj.isName(): " + qpdf_object::debug(qpdf_obj);

	    LOG_S(ERROR) << message;
	    //throw std::logic_error(message);	    
	  }    
	else
	  {
	    std::string message = "qpdf_obj is unknown: " + qpdf_object::debug(qpdf_obj);

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

    if(subtype==TYPE_0 and not qpdf_desc_font.isNull() and
       cids.has(encoding_name) )
      {
	try
	  {
	    LOG_S(INFO) << "descendant-font: " << qpdf_object::debug(qpdf_desc_font);
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

	    cmap_codespaces   = cid.get_codespaces();

	    cid.decode_widths(numb_to_widths);	

	    cmap_initialized = true;	    
	  }
	else
	  {
	    cmap_initialized = false;	    
	  }
      }
    else if(subtype==TYPE_0 and not qpdf_desc_font.isNull())
      {
	try
	  {
	    LOG_S(INFO) << "descendant-font: " << qpdf_object::debug(qpdf_desc_font);
	  }
	catch(const std::exception& exc)
	  {
	    LOG_S(ERROR) << "could not dump the descendant font with error: " 
			 << exc.what();
	  }

	LOG_S(INFO) << "encoding-type: " << to_string(encoding);
	LOG_S(INFO) << "encoding-name: " << encoding_name;

	std::string registry_;
	std::string ordering_;
	int         supplement_ = 0;
        if(not qpdf_object::get_name_or_string(qpdf_desc_font, {"/CIDSystemInfo", "/Registry"}, registry_) or
           not qpdf_object::get_name_or_string(qpdf_desc_font, {"/CIDSystemInfo", "/Ordering"}, ordering_) or
           not qpdf_object::get_int(qpdf_desc_font, {"/CIDSystemInfo", "/Supplement"}, supplement_))
          {
            LOG_S(ERROR) << "incomplete /CIDSystemInfo in descendant font";
            cmap_initialized = false;
            return;
          }
	
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

	    cmap_codespaces   = cid.get_codespaces();

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

    QPDFObjectHandle diffs = qpdf_object::get_path(qpdf_font, {"/Encoding", "/Differences"});

    // Create a regex object
    std::regex re_01(R"(\/(.+)\.(.+))");
    std::regex re_02(R"((\/)?(uni|UNI)([0-9A-Fa-f]{4}))");
    std::regex re_04(R"((\/)(C)(\d+))");

    // The unicode replacement character U+FFFD in utf8: a /ToUnicode
    // mapping to this value means 'unknown character' and is treated as
    // no mapping at all, so the glyph-name based methods can still
    // recover the code (eg /bullet.003 mapped to U+FFFD by the cmap).
    const std::string replacement_char = "\xEF\xBF\xBD";

    // PDF 32000-1 (section 9.10.2) defines the /ToUnicode cmap as the
    // first and most authoritative method to map a character-code to
    // unicode; the glyph-name based methods only apply when no (valid)
    // cmap entry exists for the code.
    auto has_to_unicode = [&](int numb)
    {
      return cmap_initialized
	and cmap_numb_to_char.count(numb)==1
	and cmap_numb_to_char.at(numb).size()>0
	and cmap_numb_to_char.at(numb)!=replacement_char;
    };

    // Subset generators (FontForge, fontTools, mPDF, ...) name glyphs by
    // bare index: /gid00043, /g43, /glyph43, /cid43, /index43. Such a name
    // identifies the glyph inside the embedded font program but carries no
    // reading text, so keeping it fabricates plausible-looking garbage
    // ('gid00043gid00049...') that downstream quality gates cannot detect
    // (docling-project/docling-parse#238).
    std::regex re_gid(R"((gid|glyph|g|cid|index)\d+)", std::regex::icase);

    // Last-resort for glyph-names that neither the /ToUnicode cmap nor
    // any glyph-table could resolve (eg custom ligatures like /Th, /ft
    // or /tt in a font without cmap): keep the glyph-name itself without
    // the leading '/' and any '.suffix' as the reading text — unless the
    // name is a pure glyph-index, which becomes a GLYPH marker so the
    // unresolved glyph stays detectable.
    auto resolve_unknown_name = [&re_gid](const std::string& name, const std::string& name_)
    {
      std::string result = name_;
      if(result.empty())
	{
	  result = (name.size()>0 and name[0]=='/')? name.substr(1) : name;
	}

      if(std::regex_match(result, re_gid))
	{
	  LOG_S(WARNING) << "glyph-index name " << name
			 << ": emitting marker 'GLYPH<name:" << result << ">'";
	  return "GLYPH<name:"+result+">";
	}

      LOG_S(WARNING) << "unknown glyph-name " << name
		     << ": falling back to '" << result << "'";
      return result;
    };

    if(not diffs.isNull())
      {
        //LOG_S(INFO) << "diffs: " << diffs.dump(2);

        if(diffs.isArray())
          {
            int         numb=-1;
            std::string name="null";

            for(int l=0; l<diffs.getArrayNItems(); l++)
              {
                QPDFObjectHandle diff = diffs.getArrayItem(l);
                if(diff.isNumber())
                  {
                    numb = static_cast<int>(utils::numeric::locale_safe_numeric_value(diff));
                  }
                else if(qpdf_object::get_name_or_string(diff, name))
                  {
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
		    
                    // Resolution order following PDF 32000-1 section 9.10.2:
                    // the /ToUnicode cmap first, then glyph-name based methods
                    // (glyph-tables, ligature-names, uniXXXX, ...). The only
                    // deviation: '.sups'/'.subs' glyph-names resolve first,
                    // since they carry super-/sub-script semantics which the
                    // plain unicode of the /ToUnicode cmap would lose.
                    if(glyphs.has(name) and font_subname=="sups")
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
		    else if(has_to_unicode(numb)) // method 1 of PDF 32000-1 section 9.10.2
		      {
			diff_numb_to_char[numb] = cmap_numb_to_char.at(numb);
			LOG_S(INFO) << "differences[" << numb << "] -> " << name
				    << " -> " << diff_numb_to_char[numb]
				    << " (/ToUnicode)";
		      }
                    else if(glyphs.has(name)) // method 2: glyph-name -> unicode via the glyph-tables (AGL)
                      {
                        diff_numb_to_char[numb] = glyphs[name];
                        LOG_S(INFO) << "differences[" << numb << "] -> " << name
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
				diff_numb_to_char[numb] = resolve_unknown_name(name, joined);
			      }
			  }
		      }
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
                        diff_numb_to_char[numb] = resolve_unknown_name(name, name_);
                      }

                    LOG_S(INFO) << font_name << ": differences["<<numb<<"] -> " << name << " -> " << diff_numb_to_char[numb];

                    numb += 1;
                  }
                else
                  {
                    LOG_S(WARNING) << "item [" << qpdf_object::debug(diff)
                                   << "] is not a string nor a number in the difference-vector: "
                                   << qpdf_object::debug(diffs);
                  }
              }
          }
        else
          {
            LOG_S(WARNING) << "/Differences is not a vector: " << qpdf_object::debug(diffs);
          }

        diff_initialized = true;
      }
    else
      {
        diff_initialized = false;
        LOG_S(WARNING) << "could not find differences in /Encoding/Differences";
      }
  }

  inline const std::array<double, 6>& pdf_resource<PAGE_FONT>::get_font_matrix()
  {
    if(not font_matrix_read_)
      {
        font_matrix_read_ = true;
        try
          {
            if(qpdf_font.isDictionary() and qpdf_font.hasKey("/FontMatrix") and
               qpdf_font.getKey("/FontMatrix").isArray() and
               qpdf_font.getKey("/FontMatrix").getArrayNItems() >= 6)
              {
                QPDFObjectHandle fm = qpdf_font.getKey("/FontMatrix");
                for(int i = 0; i < 6; i++)
                  {
                    QPDFObjectHandle v = fm.getArrayItem(i);
                    if(v.isNumber()) { font_matrix_[i] = v.getNumericValue(); }
                  }
              }
          }
        catch(const std::exception& e)
          {
            LOG_S(WARNING) << "type3: could not read /FontMatrix: " << e.what();
          }
      }
    return font_matrix_;
  }

  inline std::shared_ptr<pdf_resource<PAGE_FONT>::type3_glyph>
  pdf_resource<PAGE_FONT>::get_type3_glyph(uint32_t code)
  {
    auto cached = type3_cache_.find(code);
    if(cached != type3_cache_.end()) { return cached->second; }

    std::shared_ptr<type3_glyph> result = nullptr;

    try
      {
        const std::string name = get_glyph_name(code);
        if(not name.empty() and
           qpdf_font.isDictionary() and qpdf_font.hasKey("/CharProcs") and
           qpdf_font.getKey("/CharProcs").isDictionary())
          {
            QPDFObjectHandle procs = qpdf_font.getKey("/CharProcs");
            const std::string key = "/" + name;
            if(procs.hasKey(key) and procs.getKey(key).isStream())
              {
                auto buffer = procs.getKey(key).getStreamData();
                if(buffer and buffer->getSize() > 0)
                  {
                    const char* raw = reinterpret_cast<const char*>(buffer->getBuffer());
                    result = parse_type3_charproc(std::string(raw, buffer->getSize()));
                  }
              }
          }
      }
    catch(const std::exception& e)
      {
        LOG_S(WARNING) << "type3: charproc extraction failed for code " << code
                       << ": " << e.what();
        result = nullptr;
      }

    type3_cache_.emplace(code, result);
    return result;
  }

  inline std::shared_ptr<pdf_resource<PAGE_FONT>::type3_glyph>
  pdf_resource<PAGE_FONT>::parse_type3_charproc(const std::string& src)
  {
    const size_t pos_bi = src.find("BI");
    if(pos_bi == std::string::npos)
      {
        return rasterize_type3_vector_charproc(src);
      }

    auto glyph = std::make_shared<type3_glyph>();

    // The last `cm` before BI places the image's unit square in glyph space.
    {
      std::istringstream is(src.substr(0, pos_bi));
      std::vector<std::string> toks;
      std::string t;
      while(is >> t) { toks.push_back(t); }

      bool found = false;
      for(size_t i = toks.size(); i-- > 0;)
        {
          if(toks[i] == "cm" and i >= 6)
            {
              try
                {
                  for(int j = 0; j < 6; j++)
                    {
                      glyph->cm[j] = utils::numeric::locale_safe_stod(toks[i - 6 + j]);
                    }
                  found = true;
                }
              catch(const std::exception&) {}
              break;
            }
        }
      if(not found) { return nullptr; }
    }

    const size_t pos_id = src.find("ID", pos_bi);
    if(pos_id == std::string::npos) { return nullptr; }

    // Inline image dictionary between BI and ID. Brackets and dict markers
    // become spaces and every '/' gets one in front, so "/D[1 0]" and
    // "/DP<</K -1/Columns 68>>" tokenize cleanly.
    int W = 0, H = 0, bpc = 1;
    bool is_mask = false, ccitt = false, raw_bits = true;
    ccitt::decode_parameters ccitt_parms;
    std::vector<double> decode_arr;
    {
      std::string dict = src.substr(pos_bi + 2, pos_id - pos_bi - 2);
      std::string clean;
      clean.reserve(dict.size() * 2);
      for(const char c : dict)
        {
          if(c == '[' or c == ']' or c == '<' or c == '>') { clean += ' '; continue; }
          if(c == '/') { clean += ' '; }
          clean += c;
        }
      std::istringstream is(clean);
      std::vector<std::string> toks;
      std::string t;
      while(is >> t) { toks.push_back(t); }

      auto num_after = [&](size_t i, double fallback) -> double
      {
        if(i + 1 < toks.size())
          {
            try { return utils::numeric::locale_safe_stod(toks[i + 1]); }
            catch(const std::exception&) {}
          }
        return fallback;
      };

      for(size_t i = 0; i < toks.size(); i++)
        {
          const std::string& tok = toks[i];
          if(tok == "/W" or tok == "/Width")            { W = static_cast<int>(num_after(i, 0)); }
          else if(tok == "/H" or tok == "/Height")      { H = static_cast<int>(num_after(i, 0)); }
          else if(tok == "/BPC" or tok == "/BitsPerComponent") { bpc = static_cast<int>(num_after(i, 1)); }
          else if(tok == "/IM" or tok == "/ImageMask")  { is_mask = (i + 1 < toks.size() and toks[i + 1] == "true"); }
          else if(tok == "/K")                          { ccitt_parms.k = static_cast<int>(num_after(i, 0)); }
          else if(tok == "/BlackIs1")                   { ccitt_parms.black_is_1 = (i + 1 < toks.size() and toks[i + 1] == "true"); }
          // /Columns is the width the row codes were written against, and it
          // defaults to 1728 rather than to the glyph's own width (Table 11).
          else if(tok == "/Columns")                    { ccitt_parms.columns = static_cast<int>(num_after(i, 1728)); }
          else if(tok == "/EncodedByteAlign")           { ccitt_parms.encoded_byte_align = (i + 1 < toks.size() and toks[i + 1] == "true"); }
          else if(tok == "/D" or tok == "/Decode")
            {
              decode_arr.clear();
              for(size_t j = i + 1; j < toks.size() and decode_arr.size() < 2; j++)
                {
                  try { decode_arr.push_back(utils::numeric::locale_safe_stod(toks[j])); }
                  catch(const std::exception&) { break; }
                }
            }
          else if(tok == "/F" or tok == "/Filter")
            {
              raw_bits = false;
              if(i + 1 < toks.size() and
                 (toks[i + 1] == "/CCF" or toks[i + 1] == "/CCITTFaxDecode"))
                {
                  ccitt = true;
                }
            }
        }
    }

    if(W <= 0 or H <= 0 or not is_mask or bpc != 1) { return nullptr; }
    if(not raw_bits and not ccitt) { return nullptr; }  // other filters: not handled

    // Raw data: one delimiter byte after ID, then bytes up to the final EI.
    size_t data_start = pos_id + 2;
    if(data_start < src.size() and
       (src[data_start] == ' ' or src[data_start] == '\n' or src[data_start] == '\r'))
      {
        data_start += 1;
      }
    size_t data_end = src.rfind("EI");
    if(data_end == std::string::npos or data_end <= data_start) { return nullptr; }
    while(data_end > data_start and
          (src[data_end - 1] == '\n' or src[data_end - 1] == '\r' or src[data_end - 1] == ' '))
      {
        data_end -= 1;
      }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(src.data()) + data_start;
    const size_t data_size = data_end - data_start;

    // -> 8-bit samples, one byte per pixel, 0 = dark.
    std::vector<uint8_t> samples;
    if(ccitt)
      {
        samples = ccitt::decode(data, data_size, W, H, ccitt_parms);
        if(samples.size() < static_cast<size_t>(W) * H) { return nullptr; }
      }
    else
      {
        const size_t stride = (static_cast<size_t>(W) + 7u) / 8u;
        if(data_size < stride * static_cast<size_t>(H)) { return nullptr; }
        samples.resize(static_cast<size_t>(W) * H);
        for(int row = 0; row < H; row++)
          {
            for(int col = 0; col < W; col++)
              {
                const uint8_t byte = data[row * stride + (col >> 3)];
                const int bit = (byte >> (7 - (col & 7))) & 1;
                samples[static_cast<size_t>(row) * W + col] =
                  static_cast<uint8_t>(bit ? 255 : 0);
              }
          }
      }

    // Renderer convention: sample 0 paints. /Decode [1 0] flips which bit
    // means ink relative to the default [0 1].
    const bool inverted = (decode_arr.size() >= 2 and decode_arr[0] > decode_arr[1]);
    if(inverted)
      {
        for(auto& v : samples) { v = static_cast<uint8_t>(255 - v); }
      }

    glyph->w = W;
    glyph->h = H;
    glyph->mask = std::make_shared<std::vector<uint8_t> >(std::move(samples));
    glyph->valid = true;
    return glyph;
  }


  inline std::shared_ptr<pdf_resource<PAGE_FONT>::type3_glyph>
  pdf_resource<PAGE_FONT>::rasterize_type3_vector_charproc(const std::string& src)
  {
    std::istringstream is(src);
    std::vector<std::string> toks;
    std::string t;
    while(is >> t) { toks.push_back(t); }

    auto num = [&](size_t i) -> double
    {
      try { return utils::numeric::locale_safe_stod(toks[i]); }
      catch(const std::exception&) { return 0.0; }
    };

    // Collect closed subpaths of the fill in glyph space. The charproc's own
    // graphics state matters: producers wrap each glyph in `q <translate> cm`
    // and draw RELATIVE paths, so ignoring cm scattered every glyph by its
    // missing offset -- letters off their line, i-dots detached.
    std::vector<std::vector<std::array<double, 2> > > polys;
    std::vector<std::array<double, 2> > cur;
    double cx = 0.0, cy = 0.0;   // current point, LOCAL coords
    bool any_fill = false;

    // Row-vector affine [a b c d e f], composed like the PDF cm operator.
    std::array<double, 6> ctm = {1, 0, 0, 1, 0, 0};
    std::vector<std::array<double, 6> > ctm_stack;

    auto apply = [&](double x, double y) -> std::array<double, 2>
    {
      return { ctm[0]*x + ctm[2]*y + ctm[4],
               ctm[1]*x + ctm[3]*y + ctm[5] };
    };

    auto close_cur = [&]()
    {
      if(cur.size() >= 3) { polys.push_back(cur); }
      cur.clear();
    };

    for(size_t i = 0; i < toks.size(); i++)
      {
        const std::string& op = toks[i];
        if(op == "q")  { ctm_stack.push_back(ctm); }
        else if(op == "Q")
          {
            if(not ctm_stack.empty()) { ctm = ctm_stack.back(); ctm_stack.pop_back(); }
          }
        else if(op == "cm" and i >= 6)
          {
            const std::array<double, 6> m = {num(i-6), num(i-5), num(i-4),
                                             num(i-3), num(i-2), num(i-1)};
            // CTM' = m x CTM (row-vector convention)
            ctm = { m[0]*ctm[0] + m[1]*ctm[2],
                    m[0]*ctm[1] + m[1]*ctm[3],
                    m[2]*ctm[0] + m[3]*ctm[2],
                    m[2]*ctm[1] + m[3]*ctm[3],
                    m[4]*ctm[0] + m[5]*ctm[2] + ctm[4],
                    m[4]*ctm[1] + m[5]*ctm[3] + ctm[5] };
          }
        else if(op == "re" and i >= 4)
          {
            const double x = num(i - 4), y = num(i - 3);
            const double w = num(i - 2), h = num(i - 1);
            close_cur();
            polys.push_back({{ apply(x, y), apply(x + w, y),
                               apply(x + w, y + h), apply(x, y + h) }});
          }
        else if(op == "m" and i >= 2)
          {
            close_cur();
            cx = num(i - 2); cy = num(i - 1);
            cur.push_back(apply(cx, cy));
          }
        else if(op == "l" and i >= 2)
          {
            cx = num(i - 2); cy = num(i - 1);
            cur.push_back(apply(cx, cy));
          }
        else if((op == "c" and i >= 6) or (op == "v" and i >= 4) or (op == "y" and i >= 4))
          {
            double x1, y1, x2, y2, x3, y3;
            if(op == "c")
              {
                x1 = num(i-6); y1 = num(i-5); x2 = num(i-4); y2 = num(i-3);
                x3 = num(i-2); y3 = num(i-1);
              }
            else if(op == "v")
              {
                x1 = cx; y1 = cy; x2 = num(i-4); y2 = num(i-3);
                x3 = num(i-2); y3 = num(i-1);
              }
            else
              {
                x1 = num(i-4); y1 = num(i-3); x3 = num(i-2); y3 = num(i-1);
                x2 = x3; y2 = y3;
              }
            for(int k = 1; k <= 8; k++)
              {
                const double u = k / 8.0, v = 1.0 - u;
                cur.push_back(apply(v*v*v*cx + 3*v*v*u*x1 + 3*v*u*u*x2 + u*u*u*x3,
                                    v*v*v*cy + 3*v*v*u*y1 + 3*v*u*u*y2 + u*u*u*y3));
              }
            cx = x3; cy = y3;
          }
        else if(op == "h") { if(not cur.empty()) { cur.push_back(cur.front()); } }
        else if(op == "f" or op == "f*" or op == "F" or op == "b" or op == "b*" or op == "B" or op == "B*")
          {
            close_cur();
            any_fill = true;
          }
      }
    close_cur();

    if(not any_fill or polys.empty()) { return nullptr; }

    double bx0 = polys[0][0][0], by0 = polys[0][0][1];
    double bx1 = bx0, by1 = by0;
    for(const auto& poly : polys)
      {
        for(const auto& pt : poly)
          {
            bx0 = std::min(bx0, pt[0]); bx1 = std::max(bx1, pt[0]);
            by0 = std::min(by0, pt[1]); by1 = std::max(by1, pt[1]);
          }
      }
    const double bw = bx1 - bx0, bh = by1 - by0;
    if(bw <= 0.0 or bh <= 0.0) { return nullptr; }

    constexpr int max_dim = 128;
    int W = max_dim, H = max_dim;
    if(bw >= bh) { H = std::max(1, static_cast<int>(std::lround(max_dim * bh / bw))); }
    else         { W = std::max(1, static_cast<int>(std::lround(max_dim * bw / bh))); }

    auto glyph = std::make_shared<type3_glyph>();
    glyph->w = W;
    glyph->h = H;
    // Renderer convention: 0 paints. Start transparent (255), carve ink.
    glyph->mask = std::make_shared<std::vector<uint8_t> >(
      static_cast<size_t>(W) * H, 255);

    // Even-odd scanline fill. Raster row 0 is the TOP of the image (matches
    // the inline-image convention the mask path expects).
    for(int row = 0; row < H; row++)
      {
        const double y = by1 - (row + 0.5) * bh / H;
        std::vector<double> xs;
        for(const auto& poly : polys)
          {
            const size_t n = poly.size();
            for(size_t i2 = 0, j2 = n - 1; i2 < n; j2 = i2++)
              {
                const double yi = poly[i2][1], yj = poly[j2][1];
                if((yi > y) != (yj > y))
                  {
                    xs.push_back(poly[j2][0] +
                                 (y - yj) * (poly[i2][0] - poly[j2][0]) / (yi - yj));
                  }
              }
          }
        std::sort(xs.begin(), xs.end());
        for(size_t k = 0; k + 1 < xs.size(); k += 2)
          {
            int c0 = static_cast<int>(std::ceil ((xs[k]     - bx0) / bw * W - 0.5));
            int c1 = static_cast<int>(std::floor((xs[k + 1] - bx0) / bw * W - 0.5));
            c0 = std::max(0, c0); c1 = std::min(W - 1, c1);
            for(int c = c0; c <= c1; c++)
              {
                (*glyph->mask)[static_cast<size_t>(row) * W + c] = 0;
              }
          }
      }

    // cm maps the image unit square onto the paths' bounds in glyph space.
    glyph->cm = {bw, 0.0, 0.0, bh, bx0, by0};
    glyph->valid = true;
    return glyph;
  }

  void pdf_resource<PAGE_FONT>::init_charprocs()
  {
    LOG_S(INFO) << __FUNCTION__;

    QPDFObjectHandle qpdf_char_procs = qpdf_object::get_path(qpdf_font, {"/CharProcs"});

    if(not qpdf_char_procs.isNull())
      {
        //assert(subtype==TYPE_3);

        LOG_S(WARNING) << "found CharProcs: " << qpdf_char_procs.getTypeName();        

        if(not qpdf_char_procs.isDictionary())
          {
            LOG_S(WARNING) << "/CharProcs is not a dictionary";
            return;
          }

        for(auto& key : qpdf_char_procs.getKeys())
          {
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
