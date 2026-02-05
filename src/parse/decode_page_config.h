//-*-C++-*-

#ifndef PDF_DECODE_PAGE_CONFIG_H
#define PDF_DECODE_PAGE_CONFIG_H

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
  };

}

#endif
