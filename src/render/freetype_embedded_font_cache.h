//-*-C++-*-

#ifndef PDF_FREETYPE_EMBEDDED_FONT_CACHE_H
#define PDF_FREETYPE_EMBEDDED_FONT_CACHE_H

#include <blend2d/blend2d.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#ifndef LOGURU_WITH_STREAMS
#define LOGURU_WITH_STREAMS 1
#endif
#include <loguru.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <parse/page_items/embedded_font_blob.h>

namespace pdflib
{
  // Loads the embedded font program formats that Blend2D rejects — Type 1
  // (/FontFile) and bare CFF (/FontFile3 /Type1C, /CIDFontType0C) — through
  // FreeType, and converts glyph outlines into BLPath objects so that Blend2D
  // still performs all rasterization.
  //
  // Glyph identity is resolved in decreasing order of authority:
  //   1. CID with identity CIDToGIDMap — the CID is the glyph index;
  //   2. the glyph name assigned by /Encoding /Differences (FT_Get_Name_Index);
  //   3. the decoded PDF character code through the font's builtin encoding
  //      (Adobe custom / standard / Latin-1, and MS symbol with the 0xF000
  //      offset convention) — the faithful path for TeX fonts, whose PDF
  //      character codes are the font's own encoding;
  //   4. the Unicode codepoints of the cell text.
  // When nothing maps, the caller falls back to the system-resolved font for
  // that cell.
  //
  // FreeType is not thread-safe, so every entry point serializes on one
  // mutex; decomposed glyph paths are cached per face (in font units) to keep
  // the critical section short after warm-up.
  class freetype_embedded_font_cache
  {
  public:

    freetype_embedded_font_cache();
    ~freetype_embedded_font_cache();

    freetype_embedded_font_cache(const freetype_embedded_font_cache&) = delete;
    freetype_embedded_font_cache& operator=(const freetype_embedded_font_cache&) = delete;

    // False when FT_Init_FreeType failed; every build_text_path call then
    // returns false.
    bool available() const;

    // Builds the filled outline path of one text cell in Blend2D text space:
    // baseline at the origin, y pointing down, units equal to pixels at em
    // size `size`. Returns true when every character of the cell was mapped
    // to a glyph (an empty path is a valid result, e.g. spaces); returns
    // false when the font cannot be loaded or a character has no glyph, so
    // the caller can fall back.
    bool build_text_path(const std::shared_ptr<const embedded_font_blob>& blob,
                         const std::string& utf8_text,
                         int64_t char_code,
                         const std::string& glyph_name,
                         double size,
                         BLPath& path);

  private:

    struct glyph_entry
    {
      // Decomposed outline in font units (FreeType y-up convention).
      BLPath path;
      double advance = 0.0; // horizontal advance in font units
      bool failed = false;
    };

    struct face_entry
    {
      // FT_New_Memory_Face does not copy the bytes; keep them alive.
      std::shared_ptr<const std::vector<uint8_t> > bytes;

      FT_Face face = nullptr;
      bool failed = false;

      std::unordered_map<FT_UInt, glyph_entry> glyphs;
    };

    face_entry& get_face_entry(const std::shared_ptr<const embedded_font_blob>& blob);

    // Resolves the glyph indices of the cell; returns false when any
    // character cannot be mapped.
    bool resolve_glyph_indices(face_entry& entry,
                               const std::shared_ptr<const embedded_font_blob>& blob,
                               const std::string& utf8_text,
                               int64_t char_code,
                               const std::string& glyph_name,
                               std::vector<FT_UInt>& glyph_indices);

    FT_UInt char_code_to_glyph_index(FT_Face face, FT_ULong code);

    // Returns the cached (or freshly decomposed) outline and advance of one
    // glyph in font units. Returns nullptr on failure (failures are cached).
    const glyph_entry* get_glyph_entry(face_entry& entry, FT_UInt glyph_index);

    static bool decompose_outline(FT_Outline& outline, BLPath& path);

    static std::vector<uint32_t> decode_utf8(const std::string& text);

    FT_Library library_ = nullptr;
    std::mutex mutex_;
    std::unordered_map<std::string, face_entry> faces_;
  };

  inline freetype_embedded_font_cache::freetype_embedded_font_cache()
  {
    const FT_Error error = FT_Init_FreeType(&library_);
    if(error != 0)
      {
        library_ = nullptr;
        LOG_S(WARNING) << "freetype font cache: FT_Init_FreeType failed with error=" << error;
      }
  }

  inline freetype_embedded_font_cache::~freetype_embedded_font_cache()
  {
    for(auto& itr : faces_)
      {
        if(itr.second.face != nullptr)
          {
            FT_Done_Face(itr.second.face);
          }
      }
    faces_.clear();

    if(library_ != nullptr)
      {
        FT_Done_FreeType(library_);
      }
  }

  inline bool freetype_embedded_font_cache::available() const
  {
    return library_ != nullptr;
  }

  inline bool freetype_embedded_font_cache::build_text_path(
      const std::shared_ptr<const embedded_font_blob>& blob,
      const std::string& utf8_text,
      int64_t char_code,
      const std::string& glyph_name,
      double size,
      BLPath& path)
  {
    if(library_ == nullptr or blob == nullptr or not blob->has_bytes() or size <= 0.0)
      {
        return false;
      }

    std::lock_guard<std::mutex> lock(mutex_);

    face_entry& entry = get_face_entry(blob);
    if(entry.failed or entry.face == nullptr)
      {
        return false;
      }

    std::vector<FT_UInt> glyph_indices;
    if(not resolve_glyph_indices(entry, blob, utf8_text, char_code, glyph_name, glyph_indices))
      {
        return false;
      }

    const double units_per_em =
      (entry.face->units_per_EM > 0) ? entry.face->units_per_EM : 1000.0;
    const double scale = size / units_per_em;

    path.clear();

    double pen_x = 0.0;
    for(FT_UInt glyph_index : glyph_indices)
      {
        const glyph_entry* glyph = get_glyph_entry(entry, glyph_index);
        if(glyph == nullptr)
          {
            return false;
          }

        // Font units (y-up) -> Blend2D text space (y-down, pixels).
        const BLMatrix2D m(scale, 0.0,
                           0.0, -scale,
                           pen_x * scale, 0.0);
        path.add_path(glyph->path, m);

        pen_x += glyph->advance;
      }

    return true;
  }

  inline freetype_embedded_font_cache::face_entry&
  freetype_embedded_font_cache::get_face_entry(
      const std::shared_ptr<const embedded_font_blob>& blob)
  {
    auto itr = faces_.find(blob->get_cache_key());
    if(itr != faces_.end())
      {
        return itr->second;
      }

    face_entry entry;
    entry.bytes = blob->get_bytes();

    const FT_Error error = FT_New_Memory_Face(library_,
                                              entry.bytes->data(),
                                              static_cast<FT_Long>(entry.bytes->size()),
                                              0,
                                              &entry.face);
    entry.failed = (error != 0 or entry.face == nullptr);

    if(entry.failed)
      {
        entry.face = nullptr;
        entry.bytes.reset();

        LOG_S(INFO) << "freetype font cache: load failed"
                    << " font_name=`" << blob->get_font_name() << "`"
                    << " source=" << blob->get_source_key()
                    << " format=" << to_string(blob->get_format())
                    << " bytes=" << blob->byte_size()
                    << " ft_error=" << error
                    << " cache_key=" << blob->get_cache_key();
      }
    else
      {
        LOG_S(INFO) << "freetype font cache: loaded face"
                    << " font_name=`" << blob->get_font_name() << "`"
                    << " family=`" << (entry.face->family_name ? entry.face->family_name : "?") << "`"
                    << " glyphs=" << entry.face->num_glyphs
                    << " units_per_em=" << entry.face->units_per_EM
                    << " format=" << to_string(blob->get_format())
                    << " cache_key=" << blob->get_cache_key();
      }

    auto [inserted_itr, inserted] = faces_.emplace(blob->get_cache_key(), std::move(entry));
    return inserted_itr->second;
  }

  inline bool freetype_embedded_font_cache::resolve_glyph_indices(
      face_entry& entry,
      const std::shared_ptr<const embedded_font_blob>& blob,
      const std::string& utf8_text,
      int64_t char_code,
      const std::string& glyph_name,
      std::vector<FT_UInt>& glyph_indices)
  {
    glyph_indices.clear();

    // CID-keyed font with identity CIDToGIDMap: the character code is the
    // CID is the glyph index.
    if(blob->get_is_cid_font() and blob->get_cid_to_gid_identity() and char_code >= 0)
      {
        if(char_code < entry.face->num_glyphs)
          {
            glyph_indices.push_back(static_cast<FT_UInt>(char_code));
            return true;
          }
        return false;
      }

    // /Encoding /Differences overrides the builtin encoding, so its glyph
    // name is the most authoritative identity when present.
    if(not glyph_name.empty() and FT_HAS_GLYPH_NAMES(entry.face))
      {
        const FT_UInt glyph_index =
          FT_Get_Name_Index(entry.face, glyph_name.c_str());
        if(glyph_index != 0)
          {
            glyph_indices.push_back(glyph_index);
            return true;
          }
      }

    // Single-character cell: the decoded PDF character code through the
    // font's builtin encoding.
    if(char_code >= 0)
      {
        const FT_UInt glyph_index =
          char_code_to_glyph_index(entry.face, static_cast<FT_ULong>(char_code));
        if(glyph_index != 0)
          {
            glyph_indices.push_back(glyph_index);
            return true;
          }
      }

    // Fallback (and multi-character cells): Unicode codepoints of the text.
    if(FT_Select_Charmap(entry.face, FT_ENCODING_UNICODE) != 0)
      {
        return false;
      }

    const std::vector<uint32_t> codepoints = decode_utf8(utf8_text);
    if(codepoints.empty())
      {
        return false;
      }

    for(uint32_t codepoint : codepoints)
      {
        const FT_UInt glyph_index = FT_Get_Char_Index(entry.face, codepoint);
        if(glyph_index == 0)
          {
            return false;
          }
        glyph_indices.push_back(glyph_index);
      }

    return true;
  }

  inline FT_UInt freetype_embedded_font_cache::char_code_to_glyph_index(
      FT_Face face,
      FT_ULong code)
  {
    // The builtin encodings of Type 1 / CFF fonts; Adobe custom comes first
    // because subset TeX fonts expose their own encoding through it.
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

    // Symbolic TrueType fonts (rejected by Blend2D when they lack a Unicode
    // cmap) expose a (3,0) symbol cmap; by convention the byte codes live in
    // the private-use range at 0xF000.
    if(code <= 0xFF and FT_Select_Charmap(face, FT_ENCODING_MS_SYMBOL) == 0)
      {
        const FT_UInt glyph_index = FT_Get_Char_Index(face, 0xF000u + code);
        if(glyph_index != 0)
          {
            return glyph_index;
          }

        return FT_Get_Char_Index(face, code);
      }

    return 0;
  }

  inline const freetype_embedded_font_cache::glyph_entry*
  freetype_embedded_font_cache::get_glyph_entry(face_entry& entry,
                                                FT_UInt glyph_index)
  {
    auto itr = entry.glyphs.find(glyph_index);
    if(itr != entry.glyphs.end())
      {
        return itr->second.failed ? nullptr : &itr->second;
      }

    glyph_entry glyph;

    const FT_Error error = FT_Load_Glyph(entry.face,
                                         glyph_index,
                                         FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP);
    if(error != 0 or entry.face->glyph == nullptr)
      {
        LOG_S(INFO) << "freetype font cache: FT_Load_Glyph failed"
                    << " glyph_index=" << glyph_index
                    << " ft_error=" << error;
        glyph.failed = true;
      }
    else
      {
        // With FT_LOAD_NO_SCALE all metrics are in font units.
        glyph.advance = static_cast<double>(entry.face->glyph->advance.x);

        if(entry.face->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
          {
            glyph.failed = not decompose_outline(entry.face->glyph->outline, glyph.path);
          }
      }

    auto [inserted_itr, inserted] = entry.glyphs.emplace(glyph_index, std::move(glyph));
    return inserted_itr->second.failed ? nullptr : &inserted_itr->second;
  }

  inline bool freetype_embedded_font_cache::decompose_outline(FT_Outline& outline,
                                                              BLPath& path)
  {
    struct decompose_state
    {
      BLPath* path;
      bool has_contour;
    };

    FT_Outline_Funcs funcs;
    funcs.shift = 0;
    funcs.delta = 0;

    funcs.move_to = [](const FT_Vector* to, void* user) -> int
    {
      auto* state = static_cast<decompose_state*>(user);
      if(state->has_contour)
        {
          state->path->close();
        }
      state->has_contour = true;
      state->path->move_to(static_cast<double>(to->x), static_cast<double>(to->y));
      return 0;
    };

    funcs.line_to = [](const FT_Vector* to, void* user) -> int
    {
      auto* state = static_cast<decompose_state*>(user);
      state->path->line_to(static_cast<double>(to->x), static_cast<double>(to->y));
      return 0;
    };

    funcs.conic_to = [](const FT_Vector* control, const FT_Vector* to, void* user) -> int
    {
      auto* state = static_cast<decompose_state*>(user);
      state->path->quad_to(static_cast<double>(control->x), static_cast<double>(control->y),
                           static_cast<double>(to->x), static_cast<double>(to->y));
      return 0;
    };

    funcs.cubic_to = [](const FT_Vector* control1, const FT_Vector* control2,
                        const FT_Vector* to, void* user) -> int
    {
      auto* state = static_cast<decompose_state*>(user);
      state->path->cubic_to(static_cast<double>(control1->x), static_cast<double>(control1->y),
                            static_cast<double>(control2->x), static_cast<double>(control2->y),
                            static_cast<double>(to->x), static_cast<double>(to->y));
      return 0;
    };

    decompose_state state{&path, false};

    const FT_Error error = FT_Outline_Decompose(&outline, &funcs, &state);
    if(error != 0)
      {
        LOG_S(INFO) << "freetype font cache: FT_Outline_Decompose failed with error=" << error;
        return false;
      }

    if(state.has_contour)
      {
        path.close();
      }

    return true;
  }

  inline std::vector<uint32_t> freetype_embedded_font_cache::decode_utf8(
      const std::string& text)
  {
    std::vector<uint32_t> codepoints;

    size_t i = 0;
    while(i < text.size())
      {
        const uint8_t byte = static_cast<uint8_t>(text[i]);

        uint32_t codepoint = 0;
        size_t extra = 0;

        if(byte < 0x80)       { codepoint = byte;        extra = 0; }
        else if(byte < 0xC0)  { return {}; } // stray continuation byte
        else if(byte < 0xE0)  { codepoint = byte & 0x1F; extra = 1; }
        else if(byte < 0xF0)  { codepoint = byte & 0x0F; extra = 2; }
        else if(byte < 0xF8)  { codepoint = byte & 0x07; extra = 3; }
        else                  { return {}; }

        if(i + extra >= text.size())
          {
            return {};
          }

        for(size_t k = 1; k <= extra; k++)
          {
            const uint8_t continuation = static_cast<uint8_t>(text[i + k]);
            if((continuation & 0xC0) != 0x80)
              {
                return {};
              }
            codepoint = (codepoint << 6) | (continuation & 0x3F);
          }

        codepoints.push_back(codepoint);
        i += extra + 1;
      }

    return codepoints;
  }

}

#endif
