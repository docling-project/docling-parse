//-*-C++-*-

#ifndef PDF_DECODER_CONFIGS_H
#define PDF_DECODER_CONFIGS_H

namespace pdflib
{

  struct decode_page_config
  {
    std::string page_boundary = "crop_box";

    bool do_sanitization = true;

    bool keep_char_cells = true;
    bool keep_lines = true;
    bool keep_bitmaps = true;

    int max_num_lines = -1;   // -1 means no cap
    int max_num_bitmaps = -1; // -1 means no cap

    bool create_word_cells = true;
    bool create_line_cells = true;
    bool enforce_same_font = true;      // word & line cell creation

    // word & line cell creation parameters
    double horizontal_cell_tolerance = 1.0;

    // word cell creation
    double word_space_width_factor_for_merge = 0.33;

    // line cell creation
    double line_space_width_factor_for_merge = 1.0;
    double line_space_width_factor_for_merge_with_space = 0.33;

    nlohmann::json to_json() const;
    void from_json(const nlohmann::json& j);

    bool load(const std::string& filename);
    bool save(const std::string& filename) const;

    std::string to_string() const;
  };

  nlohmann::json decode_page_config::to_json() const
  {
    nlohmann::json j;

    j["page_boundary"] = page_boundary;

    j["do_sanitization"] = do_sanitization;

    j["keep_char_cells"] = keep_char_cells;
    j["keep_lines"] = keep_lines;
    j["keep_bitmaps"] = keep_bitmaps;

    j["max_num_lines"] = max_num_lines;
    j["max_num_bitmaps"] = max_num_bitmaps;

    j["create_word_cells"] = create_word_cells;
    j["create_line_cells"] = create_line_cells;
    j["enforce_same_font"] = enforce_same_font;

    j["horizontal_cell_tolerance"] = horizontal_cell_tolerance;

    j["word_space_width_factor_for_merge"] = word_space_width_factor_for_merge;

    j["line_space_width_factor_for_merge"] = line_space_width_factor_for_merge;
    j["line_space_width_factor_for_merge_with_space"] = line_space_width_factor_for_merge_with_space;

    return j;
  }

  void decode_page_config::from_json(const nlohmann::json& j)
  {
    if(j.count("page_boundary")) { page_boundary = j["page_boundary"]; }

    if(j.count("do_sanitization")) { do_sanitization = j["do_sanitization"]; }

    if(j.count("keep_char_cells")) { keep_char_cells = j["keep_char_cells"]; }
    if(j.count("keep_lines")) { keep_lines = j["keep_lines"]; }
    if(j.count("keep_bitmaps")) { keep_bitmaps = j["keep_bitmaps"]; }

    if(j.count("max_num_lines")) { max_num_lines = j["max_num_lines"]; }
    if(j.count("max_num_bitmaps")) { max_num_bitmaps = j["max_num_bitmaps"]; }

    if(j.count("create_word_cells")) { create_word_cells = j["create_word_cells"]; }
    if(j.count("create_line_cells")) { create_line_cells = j["create_line_cells"]; }
    if(j.count("enforce_same_font")) { enforce_same_font = j["enforce_same_font"]; }

    if(j.count("horizontal_cell_tolerance")) { horizontal_cell_tolerance = j["horizontal_cell_tolerance"]; }

    if(j.count("word_space_width_factor_for_merge")) { word_space_width_factor_for_merge = j["word_space_width_factor_for_merge"]; }

    if(j.count("line_space_width_factor_for_merge")) { line_space_width_factor_for_merge = j["line_space_width_factor_for_merge"]; }
    if(j.count("line_space_width_factor_for_merge_with_space")) { line_space_width_factor_for_merge_with_space = j["line_space_width_factor_for_merge_with_space"]; }
  }

  bool decode_page_config::load(const std::string& filename)
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

  bool decode_page_config::save(const std::string& filename) const
  {
    std::ofstream ofs(filename);
    if(!ofs)
      {
        return false;
      }

    ofs << std::setw(2) << to_json();
    return true;
  }

  std::string decode_page_config::to_string() const
  {
    std::stringstream ss;

    ss << std::left
       << std::setw(48) << "parameter" << "value" << "\n"
       << std::string(64, '-') << "\n"
       << std::setw(48) << "page_boundary" << page_boundary << "\n"
       << std::setw(48) << "do_sanitization" << (do_sanitization ? "true" : "false") << "\n"
       << std::setw(48) << "keep_char_cells" << (keep_char_cells ? "true" : "false") << "\n"
       << std::setw(48) << "keep_lines" << (keep_lines ? "true" : "false") << "\n"
       << std::setw(48) << "keep_bitmaps" << (keep_bitmaps ? "true" : "false") << "\n"
       << std::setw(48) << "max_num_lines" << max_num_lines << "\n"
       << std::setw(48) << "max_num_bitmaps" << max_num_bitmaps << "\n"
       << std::setw(48) << "create_word_cells" << (create_word_cells ? "true" : "false") << "\n"
       << std::setw(48) << "create_line_cells" << (create_line_cells ? "true" : "false") << "\n"
       << std::setw(48) << "enforce_same_font" << (enforce_same_font ? "true" : "false") << "\n"
       << std::setw(48) << "horizontal_cell_tolerance" << horizontal_cell_tolerance << "\n"
       << std::setw(48) << "word_space_width_factor_for_merge" << word_space_width_factor_for_merge << "\n"
       << std::setw(48) << "line_space_width_factor_for_merge" << line_space_width_factor_for_merge << "\n"
       << std::setw(48) << "line_space_width_factor_for_merge_with_space" << line_space_width_factor_for_merge_with_space << "\n";

    return ss.str();
  }

}

#endif
