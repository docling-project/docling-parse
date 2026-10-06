//-*-C++-*-

#ifndef PDF_CELLS_CONSTANTS_H
#define PDF_CELLS_CONSTANTS_H

namespace pdflib
{

  class text_constants
  {

  public:

    // FIXME: we might at some point move this into a file into the resources ...
    const static inline std::vector<std::pair<std::string, std::string> > replacements = {
      {R"(\f_f_i)", "ffi"},
      {R"(\f_f_l)", "ffl"},
      {R"(\f_i)", "fi"},
      {R"(\f_l)", "fl"},
      {R"(\f_f)", "ff"},

      {R"(/f_f_i)", "ffi"},
      {R"(/f_f_l)", "ffl"},
      {R"(/f_i)", "fi"},
      {R"(/f_l)", "fl"},
      {R"(/f_f)", "ff"},
      
      {R"(f_f_i)", "ffi"},
      {R"(f_f_l)", "ffl"},
      {R"(f_i)", "fi"},
      {R"(f_l)", "fl"},
      {R"(f_f)", "ff"},

      {"\uFB00", "ff"},
      {"\uFB01", "fi"},
      {"\uFB02", "fl"},
      {"\uFB03", "ffi"},
      {"\uFB04", "ffl"},
      
      {"\u2000", " "},
      {"\u2001", " "},
      {"\u2002", " "},
      {"\u2003", " "},
      {"\u2004", " "},
      {"\u2005", " "},
      {"\u2006", " "},
      {"\u2007", " "},
      {"\u2008", " "},
      {"\u2009", " "},      
      {"\u200A", " "},

      {"\u200B", ""},
      {"\u200C", ""},
      {"\u200D", ""},
      {"\u200E", ""},
      {"\u200F", ""},      
      
      // Only the hyphen variants fold to `-`: the line-joining dehyphenation
      // downstream recognises an ASCII hyphen alone. U+2012..U+2015 (figure,
      // en and em dash, horizontal bar) and U+2212 (minus) carry meaning of
      // their own (`3-5` is not `3–5`, `2024–2026` is a range) and are kept
      // (docling-parse#362).
      {"\u2010", "-"},
      {"\u2011", "-"},

      {"\u2018", "'"},
      {"\u2019", "'"},
      {"\u201A", ","},
      {"\u201B", "'"},
      {"\u201C", "\""},
      {"\u201D", "\""},
      {"\u201E", "\""},
      {"\u201F", "\""},
    };

  };

}

#endif
