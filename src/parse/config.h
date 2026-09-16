//-*-C++-*-

#ifndef PDF_DECODER_CONFIGS_H
#define PDF_DECODER_CONFIGS_H

namespace pdflib
{

  struct decode_config
  {
    static constexpr double DEFAULT_HORIZONTAL_CELL_TOLERANCE = 1.0;
    static constexpr double DEFAULT_WORD_SPACE_WIDTH_FACTOR_FOR_MERGE = 0.33;
    static constexpr double DEFAULT_LINE_SPACE_WIDTH_FACTOR_FOR_MERGE = 1.0;
    static constexpr double DEFAULT_LINE_SPACE_WIDTH_FACTOR_FOR_MERGE_WITH_SPACE = 0.33;
    static constexpr double DEFAULT_MIN_VISIBLE_CLIP_EXTENT = 1e-3;

    std::string page_boundary = "crop_box";

    bool do_sanitization = true;

    bool keep_char_cells = true;
    bool keep_shapes = true;
    bool keep_bitmaps = true;

    int max_num_lines = -1;   // -1 means no cap
    int max_num_bitmaps = -1; // -1 means no cap
    double min_visible_clip_extent = DEFAULT_MIN_VISIBLE_CLIP_EXTENT;

    bool create_word_cells = true;
    bool create_line_cells = true;
    bool enforce_same_font = true;      // word & line cell creation

    // word & line cell creation parameters
    double horizontal_cell_tolerance = DEFAULT_HORIZONTAL_CELL_TOLERANCE;

    // word cell creation
    double word_space_width_factor_for_merge = DEFAULT_WORD_SPACE_WIDTH_FACTOR_FOR_MERGE;

    // line cell creation
    double line_space_width_factor_for_merge = DEFAULT_LINE_SPACE_WIDTH_FACTOR_FOR_MERGE;
    double line_space_width_factor_for_merge_with_space =
      DEFAULT_LINE_SPACE_WIDTH_FACTOR_FOR_MERGE_WITH_SPACE;

    bool populate_json_objects = false;

    // Extract embedded font programs (/FontFile, /FontFile2, /FontFile3) and
    // attach them to text render instructions. Off by default so that
    // parse-only workloads (text extraction) never pay for font stream
    // decoding; the render pipeline turns it on.
    bool extract_font_programs = false;

    // Defilter the pixel payload of image XObjects, and decode and resample
    // the /SMask (or /Mask stencil) alpha plane that goes with it. When off,
    // bitmaps are still found and measured -- bounding box, visibility,
    // dimensions, colour space, filters and decode parameters all come from
    // the XObject dictionary -- but no sample data is touched. Callers that
    // want the image bytes, and the render pipeline, must turn this on.
    // Only meaningful together with keep_bitmaps.
    bool extract_bitmap_pixels = true;

    // Device resolution the decoded image samples will end up being drawn at,
    // in pixels per PDF unit (72 ppi baseline, so 2.0 is a scale-2 render).
    // When a stored image carries many more samples than that -- a 600 dpi
    // scan drawn onto a 2x canvas is 8x oversampled in each direction -- the
    // codecs that can decode at a reduced resolution are asked to, which is
    // cheaper than decoding every sample and letting the rasteriser throw the
    // excess away.
    //
    // 0 means "unknown": every image is decoded at its full stored resolution.
    // That is the parse-only behaviour and the default, because parse callers
    // hand the samples out and no rendering resolution is implied. The render
    // pipeline sets it from render_config::scale.
    //
    // Only ever reduces to a power of two that still leaves at least one
    // stored sample per device pixel, so the rasteriser is never asked to
    // magnify what it used to minify. Rendered output does change: the
    // discarded resolution levels are ones the rasteriser would have
    // resampled away, but not with the same filter the codec uses.
    double bitmap_target_pixels_per_unit = 0.0;

    // threading
    bool do_thread_safe = true; // slight compute/memory overhead in single threaded case
    int release_native_memory_every_n_pages = 0; // 0 disables allocator trimming

    // honor /ActualText marked-content replacement text (PDF 32000-1, 14.9.4):
    // the producer-declared exact Unicode of a glyph run (ligatures, composed
    // accents, hyphenation). Only substituted into spans that drew text cells.
    bool apply_actual_text = true;

    // debug: in production, we dont want to have ugly GLYPH<...>
    bool keep_glyphs = false;
    bool keep_qpdf_warnings = false;

    nlohmann::json to_json() const;
    void from_json(const nlohmann::json& j);

    bool load(const std::string& filename);
    bool save(const std::string& filename) const;

    std::string to_string() const;
  };

  nlohmann::json decode_config::to_json() const
  {
    nlohmann::json j;

    j["page_boundary"] = page_boundary;

    j["do_sanitization"] = do_sanitization;

    j["keep_char_cells"] = keep_char_cells;
    j["keep_shapes"] = keep_shapes;
    j["keep_bitmaps"] = keep_bitmaps;

    j["max_num_lines"] = max_num_lines;
    j["max_num_bitmaps"] = max_num_bitmaps;
    j["min_visible_clip_extent"] = min_visible_clip_extent;

    j["create_word_cells"] = create_word_cells;
    j["create_line_cells"] = create_line_cells;
    j["enforce_same_font"] = enforce_same_font;

    j["horizontal_cell_tolerance"] = horizontal_cell_tolerance;

    j["word_space_width_factor_for_merge"] = word_space_width_factor_for_merge;

    j["line_space_width_factor_for_merge"] = line_space_width_factor_for_merge;
    j["line_space_width_factor_for_merge_with_space"] = line_space_width_factor_for_merge_with_space;

    j["populate_json_objects"] = populate_json_objects;
    j["extract_font_programs"] = extract_font_programs;
    j["extract_bitmap_pixels"] = extract_bitmap_pixels;
    j["bitmap_target_pixels_per_unit"] = bitmap_target_pixels_per_unit;
    j["release_native_memory_every_n_pages"] = release_native_memory_every_n_pages;

    j["apply_actual_text"] = apply_actual_text;

    j["keep_glyphs"] = keep_glyphs;
    j["keep_qpdf_warnings"] = keep_qpdf_warnings;

    return j;
  }

  void decode_config::from_json(const nlohmann::json& j)
  {
    if(j.count("page_boundary")) { page_boundary = j["page_boundary"]; }

    if(j.count("do_sanitization")) { do_sanitization = j["do_sanitization"]; }

    if(j.count("keep_char_cells")) { keep_char_cells = j["keep_char_cells"]; }
    if(j.count("keep_shapes")) { keep_shapes = j["keep_shapes"]; }
    if(j.count("keep_bitmaps")) { keep_bitmaps = j["keep_bitmaps"]; }

    if(j.count("max_num_lines")) { max_num_lines = j["max_num_lines"]; }
    if(j.count("max_num_bitmaps")) { max_num_bitmaps = j["max_num_bitmaps"]; }
    if(j.count("min_visible_clip_extent")) { min_visible_clip_extent = j["min_visible_clip_extent"]; }

    if(j.count("create_word_cells")) { create_word_cells = j["create_word_cells"]; }
    if(j.count("create_line_cells")) { create_line_cells = j["create_line_cells"]; }
    if(j.count("enforce_same_font")) { enforce_same_font = j["enforce_same_font"]; }

    if(j.count("horizontal_cell_tolerance")) { horizontal_cell_tolerance = j["horizontal_cell_tolerance"]; }

    if(j.count("word_space_width_factor_for_merge")) { word_space_width_factor_for_merge = j["word_space_width_factor_for_merge"]; }

    if(j.count("line_space_width_factor_for_merge")) { line_space_width_factor_for_merge = j["line_space_width_factor_for_merge"]; }
    if(j.count("line_space_width_factor_for_merge_with_space")) { line_space_width_factor_for_merge_with_space = j["line_space_width_factor_for_merge_with_space"]; }

    if(j.count("populate_json_objects")) { populate_json_objects = j["populate_json_objects"]; }
    if(j.count("extract_font_programs")) { extract_font_programs = j["extract_font_programs"]; }
    if(j.count("extract_bitmap_pixels")) { extract_bitmap_pixels = j["extract_bitmap_pixels"]; }
    if(j.count("bitmap_target_pixels_per_unit")) { bitmap_target_pixels_per_unit = j["bitmap_target_pixels_per_unit"]; }
    if(j.count("release_native_memory_every_n_pages")) { release_native_memory_every_n_pages = j["release_native_memory_every_n_pages"]; }

    if(j.count("apply_actual_text")) { apply_actual_text = j["apply_actual_text"]; }

    if(j.count("keep_glyphs")) { keep_glyphs = j["keep_glyphs"]; }
    if(j.count("keep_qpdf_warnings")) { keep_qpdf_warnings = j["keep_qpdf_warnings"]; }
  }

  bool decode_config::load(const std::string& filename)
  {
    std::ifstream ifs(filename);
    if(!ifs)
      {
        return false;
      }

    nlohmann::json j;
    ifs >> j;
    from_json(j);

    return true;
  }

  bool decode_config::save(const std::string& filename) const
  {
    std::ofstream ofs(filename);
    if(!ofs)
      {
        return false;
      }

    ofs << std::setw(2) << to_json();
    return true;
  }

  std::string decode_config::to_string() const
  {
    std::stringstream ss;

    ss << std::left
       << std::setw(48) << "parameter" << "value" << "\n"
       << std::string(64, '-') << "\n"
       << std::setw(48) << "page_boundary" << page_boundary << "\n"
       << std::setw(48) << "do_sanitization" << (do_sanitization ? "true" : "false") << "\n"
       << std::setw(48) << "keep_char_cells" << (keep_char_cells ? "true" : "false") << "\n"
       << std::setw(48) << "keep_shapes" << (keep_shapes ? "true" : "false") << "\n"
       << std::setw(48) << "keep_bitmaps" << (keep_bitmaps ? "true" : "false") << "\n"
       << std::setw(48) << "extract_bitmap_pixels" << (extract_bitmap_pixels ? "true" : "false") << "\n"
       << std::setw(48) << "bitmap_target_pixels_per_unit" << bitmap_target_pixels_per_unit << "\n"
       << std::setw(48) << "max_num_lines" << max_num_lines << "\n"
       << std::setw(48) << "max_num_bitmaps" << max_num_bitmaps << "\n"
       << std::setw(48) << "min_visible_clip_extent" << min_visible_clip_extent << "\n"
       << std::setw(48) << "create_word_cells" << (create_word_cells ? "true" : "false") << "\n"
       << std::setw(48) << "create_line_cells" << (create_line_cells ? "true" : "false") << "\n"
       << std::setw(48) << "enforce_same_font" << (enforce_same_font ? "true" : "false") << "\n"
       << std::setw(48) << "horizontal_cell_tolerance" << horizontal_cell_tolerance << "\n"
       << std::setw(48) << "word_space_width_factor_for_merge" << word_space_width_factor_for_merge << "\n"
       << std::setw(48) << "line_space_width_factor_for_merge" << line_space_width_factor_for_merge << "\n"
       << std::setw(48) << "line_space_width_factor_for_merge_with_space" << line_space_width_factor_for_merge_with_space << "\n"
       << std::setw(48) << "populate_json_objects" << (populate_json_objects ? "true" : "false") << "\n"
       << std::setw(48) << "extract_font_programs" << (extract_font_programs ? "true" : "false") << "\n"
       << std::setw(48) << "release_native_memory_every_n_pages" << release_native_memory_every_n_pages << "\n"
       << std::setw(48) << "apply_actual_text" << (apply_actual_text ? "true" : "false") << "\n"
       << std::setw(48) << "keep_glyphs" << (keep_glyphs ? "true" : "false") << "\n"
       << std::setw(48) << "keep_qpdf_warnings" << (keep_qpdf_warnings ? "true" : "false") << "\n";

    return ss.str();
  }

}

#endif
