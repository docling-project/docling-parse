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
    bool enforce_same_font = true;
  };

}

#endif
