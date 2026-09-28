//-*-C++-*-

#ifndef PDF_PAGE_FONT_BUILTIN_ENCODING_H
#define PDF_PAGE_FONT_BUILTIN_ENCODING_H

#include <ft2build.h>
#include FT_FREETYPE_H

namespace pdflib
{
  // Reading text for the character codes of a simple font that maps them
  // through the builtin encoding of its embedded program (PDF 32000-1,
  // 9.6.6.2 and 9.6.6.4): a symbolic font with no declared base encoding.
  // Its codes bear no relation to the standard Latin tables; only the font
  // program says which glyph a code selects, and the glyph is what carries
  // the reading text.
  //
  // FreeType exposes the builtin mapping as a charmap: Adobe custom /
  // standard / Latin-1 for Type 1 and CFF programs, the Microsoft symbol
  // (3,0) table with the 0xF000 convention for TrueType, and whatever
  // single table a subsetter left behind. The glyph then yields text through
  // its PostScript name (AGL, uniXXXX, uXXXX[XX]) or, failing that, through
  // the program's own Unicode charmap. A glyph that neither names nor maps
  // stays unresolved so the caller can emit a marker rather than fabricate;
  // a blank glyph (no contours) reads as a space.
  //
  // This is the text-side twin of the glyph-identity lookup the renderer
  // performs for the same fonts (freetype_font_cache::char_code_to_glyph_index),
  // so the extracted text and the drawn glyph agree on what a code means.
  //
  // Each decode() opens its own FT_Library and closes it before returning:
  // FreeType is only thread-safe across separate libraries, and the
  // threaded parser decodes fonts of different pages concurrently.
  class builtin_font_encoding
  {
  public:

    // code -> utf8 for every single-byte code the program resolves.
    static std::map<uint32_t, std::string> decode(const std::vector<uint8_t>& program,
                                                  font_glyphs& glyphs,
                                                  const std::string& font_name);

    // Glyph index of a character code through the builtin charmaps; 0 when
    // none of them maps the code.
    static FT_UInt char_code_to_glyph_index(FT_Face face, FT_ULong code);

    // Reading text of a PostScript glyph name; empty when the name carries
    // no text (an index-style name such as g12 or gid00012, or a name found
    // in no table).
    static std::string glyph_name_to_utf8(const std::string& glyph_name,
                                          font_glyphs& glyphs);

  private:

    static bool glyph_is_blank(FT_Face face, FT_UInt glyph_index);

    static void append_code_point(std::string& text, uint32_t code_point);
  };

  inline std::map<uint32_t, std::string>
  builtin_font_encoding::decode(const std::vector<uint8_t>& program,
                                font_glyphs& glyphs,
                                const std::string& font_name)
  {
    std::map<uint32_t, std::string> result;

    FT_Library library = nullptr;
    if(FT_Init_FreeType(&library) != 0)
      {
        LOG_S(WARNING) << "FreeType unavailable: builtin encoding of " << font_name
                       << " not read";
        return result;
      }

    FT_Face face = nullptr;
    const FT_Error error = FT_New_Memory_Face(library,
                                              program.data(),
                                              static_cast<FT_Long>(program.size()),
                                              0, &face);
    if(error != 0 or face == nullptr)
      {
        LOG_S(WARNING) << "FreeType could not open the embedded program of "
                       << font_name << " (error " << error << "): builtin encoding not read";
        FT_Done_FreeType(library);
        return result;
      }

    // glyph index -> code point through the program's Unicode charmap, when
    // it has one (FreeType also synthesizes one from glyph names).
    std::unordered_map<FT_UInt, FT_ULong> code_point_of_glyph;
    if(FT_Select_Charmap(face, FT_ENCODING_UNICODE) == 0)
      {
        FT_UInt glyph_index = 0;
        FT_ULong code_point = FT_Get_First_Char(face, &glyph_index);
        while(glyph_index != 0)
          {
            code_point_of_glyph.emplace(glyph_index, code_point);
            code_point = FT_Get_Next_Char(face, code_point, &glyph_index);
          }
      }

    const bool has_glyph_names = FT_HAS_GLYPH_NAMES(face);

    int unresolved = 0;
    for(uint32_t code = 0; code < 256; code++)
      {
        const FT_UInt glyph_index = char_code_to_glyph_index(face, code);
        if(glyph_index == 0)
          {
            continue;
          }

        std::string text;

        if(has_glyph_names)
          {
            char buffer[128] = {0};
            if(FT_Get_Glyph_Name(face, glyph_index, buffer, sizeof(buffer)) == 0)
              {
                text = glyph_name_to_utf8(buffer, glyphs);
              }
          }

        if(text.empty())
          {
            auto itr = code_point_of_glyph.find(glyph_index);
            if(itr != code_point_of_glyph.end())
              {
                append_code_point(text, static_cast<uint32_t>(itr->second));
              }
          }

        if(text.empty() and glyph_is_blank(face, glyph_index))
          {
            text = " ";
          }

        if(text.empty())
          {
            unresolved += 1;
            continue;
          }

        result[code] = text;
      }

    LOG_S(INFO) << "builtin encoding of " << font_name << ": " << result.size()
                << " code(s) resolved, " << unresolved << " glyph(s) without text"
                << " (glyph-names=" << has_glyph_names
                << ", unicode-charmap=" << (not code_point_of_glyph.empty()) << ")";

    FT_Done_Face(face);
    FT_Done_FreeType(library);

    return result;
  }

  // Twin of freetype_font_cache::char_code_to_glyph_index in the renderer:
  // the two must agree, or the text would describe a different glyph than
  // the one drawn.
  inline FT_UInt builtin_font_encoding::char_code_to_glyph_index(FT_Face face, FT_ULong code)
  {
    // The builtin encodings of Type 1 / CFF programs; Adobe custom comes
    // first because a subset program exposes its own encoding through it.
    const FT_Encoding encodings[] = {
      FT_ENCODING_ADOBE_CUSTOM,
      FT_ENCODING_ADOBE_STANDARD,
      FT_ENCODING_ADOBE_LATIN_1,
    };

    for(FT_Encoding encoding : encodings)
      {
        if(FT_Select_Charmap(face, encoding) != 0)
          {
            continue;
          }

        const FT_UInt glyph_index = FT_Get_Char_Index(face, code);
        if(glyph_index != 0)
          {
            return glyph_index;
          }
      }

    // Symbolic TrueType programs carry a (3,0) symbol cmap whose codes live
    // by convention in the private-use range at 0xF000 (9.6.6.4).
    if(code <= 0xFF and FT_Select_Charmap(face, FT_ENCODING_MS_SYMBOL) == 0)
      {
        const FT_UInt glyph_index = FT_Get_Char_Index(face, 0xF000u + code);
        if(glyph_index != 0)
          {
            return glyph_index;
          }

        const FT_UInt raw_index = FT_Get_Char_Index(face, code);
        if(raw_index != 0)
          {
            return raw_index;
          }
      }

    // Last resort: every charmap the program actually carries, whatever its
    // platform -- subsetters leave single (1,0) format-6 tables that none of
    // the named lookups above select.
    for(FT_Int i = 0; i < face->num_charmaps; i++)
      {
        if(FT_Set_Charmap(face, face->charmaps[i]) != 0)
          {
            continue;
          }

        FT_UInt glyph_index = FT_Get_Char_Index(face, code);
        if(glyph_index == 0 and code <= 0xFF)
          {
            glyph_index = FT_Get_Char_Index(face, 0xF000u + code);
          }
        if(glyph_index != 0)
          {
            return glyph_index;
          }
      }

    return 0;
  }

  inline std::string builtin_font_encoding::glyph_name_to_utf8(const std::string& glyph_name,
                                                               font_glyphs& glyphs)
  {
    std::string name = glyph_name;

    if(name.empty() or name == ".notdef")
      {
        return "";
      }

    // A stylistic suffix (/a.sc, /one.oldstyle, /uni0041.alt) does not
    // change the reading text.
    const std::size_t dot = name.find('.');
    if(dot != std::string::npos and dot > 0)
      {
        name = name.substr(0, dot);
      }

    // Index-style names (g12, glyph12, gid00012, cid12, index12) identify
    // the glyph inside the program but carry no text (docling-parse#238).
    static const std::regex re_index(R"((gid|glyph|g|cid|index)\d+)", std::regex::icase);
    if(std::regex_match(name, re_index))
      {
        return "";
      }

    if(glyphs.has(name))
      {
        return glyphs[name];
      }

    std::string text;
    std::smatch match;

    // uniXXXX, or a sequence of them for a ligature (Adobe glyph naming)
    static const std::regex re_uni(R"(uni((?:[0-9A-Fa-f]{4})+))");
    if(std::regex_match(name, match, re_uni))
      {
        const std::string hex = match[1].str();
        for(std::size_t i = 0; i + 4 <= hex.size(); i += 4)
          {
            append_code_point(text, std::stoul(hex.substr(i, 4), nullptr, 16));
          }
        return text;
      }

    // uXXXX to uXXXXXX
    static const std::regex re_u(R"(u([0-9A-Fa-f]{4,6}))");
    if(std::regex_match(name, match, re_u))
      {
        append_code_point(text, std::stoul(match[1].str(), nullptr, 16));
        return text;
      }

    return "";
  }

  inline bool builtin_font_encoding::glyph_is_blank(FT_Face face, FT_UInt glyph_index)
  {
    const FT_Int32 flags = FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP;
    if(FT_Load_Glyph(face, glyph_index, flags) != 0)
      {
        return false;
      }

    const FT_GlyphSlot slot = face->glyph;
    return slot->format == FT_GLYPH_FORMAT_OUTLINE and
      slot->outline.n_contours == 0 and
      slot->outline.n_points == 0;
  }

  inline void builtin_font_encoding::append_code_point(std::string& text, uint32_t code_point)
  {
    // Surrogates and values beyond U+10FFFF have no UTF-8 form.
    if(utf8::internal::is_code_point_valid(code_point))
      {
        utf8::append(code_point, std::back_inserter(text));
      }
  }

}

#endif
