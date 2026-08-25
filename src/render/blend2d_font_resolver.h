//-*-C++-*-

#ifndef PDF_BLEND2D_FONT_RESOLVER_H
#define PDF_BLEND2D_FONT_RESOLVER_H

#include <blend2d/blend2d.h>

#ifndef LOGURU_WITH_STREAMS
#define LOGURU_WITH_STREAMS 1
#endif
#include <loguru.hpp>

#include <resources.h>

#include <render/freetype_font_cache.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace pdflib
{
  class blend2d_font_resolver
  {
  public:
    // Constructs an empty resolver. The font index is built lazily by warm()
    // or by the first resolving call, so construction is cheap and does not
    // touch the filesystem.
    blend2d_font_resolver();

    // Returns the process-wide resolver used by default renderer instances.
    // The shared resolver is warmed before publication so common rendering
    // paths can reuse the same font index and loaded BLFontFace cache.
    static std::shared_ptr<blend2d_font_resolver> default_resolver();

    // Builds the system font index once. This method is safe to call multiple
    // times and from multiple threads; std::call_once guarantees that the
    // directory scan runs at most once per resolver instance.
    void warm();

    // Resolves the PDF font identifiers to a Blend2D font face. base_font is
    // preferred, being the PostScript name of the typeface; font_name carries
    // the font dictionary's /Name, a resource label, and is used only when
    // there is no base font.
    // When resolve_fonts is true the selected name is matched against indexed
    // system fonts using deterministic aliases, exact metadata matches, style
    // selection, and finally fuzzy matching. If lookup fails, or resolving is
    // disabled, the method falls back to a known system font.
    // Returns an invalid BLFontFace when no fallback font can be loaded.

    // A face the resolver selected, together with the file it came from.
    //
    // `face` being invalid is not a dead end. Our pinned Blend2D cannot open
    // CFF2 outlines -- it answers BL_ERROR_FONT_CFF_INVALID_DATA -- which is
    // what every Noto CJK face looks like to it on a distribution that ships
    // only the variable-font build, leaving such a host with no CJK-capable
    // face at all and Hangul drawn as .notdef boxes. FreeType reads those
    // files, so `path` is still filled in and the renderer draws the cell
    // through freetype_font_cache instead.
    struct resolved_face
    {
      BLFontFace face;
      std::string path;
      uint32_t face_index = 0;

      // Blend2D can draw this face itself (shaping included).
      bool is_valid() const { return face.is_valid(); }

      // Some backend can draw it: Blend2D, or FreeType from the file.
      bool is_drawable() const { return face.is_valid() or not path.empty(); }
    };

    // Chooses a face by the SCRIPT of the text, for runs the name-based
    // resolution cannot draw: an Arabic or CJK run through a Latin fallback
    // shapes to .notdef boxes. Returns an invalid face for Latin-only text.
    BLFontFace resolve_face_for_text(const std::string& utf8_text);
    resolved_face resolve_font_for_text(const std::string& utf8_text);

    // True when every glyph of a shaped run is .notdef.
    static bool shaped_run_all_notdef(const BLGlyphBuffer& gb);

    BLFontFace resolve_font_face(const std::string& font_name,
                                 const std::string& base_font,
                                 bool resolve_fonts,
                                 float font_similarity_cutoff);

    resolved_face resolve_font(const std::string& font_name,
                               const std::string& base_font,
                               bool resolve_fonts,
                               float font_similarity_cutoff);

  private:

    struct font_face_ref
    {
      std::string path;
      uint32_t face_index = 0;

      bool operator==(const font_face_ref& other) const;
    };

    struct font_face_ref_hash
    {
      std::size_t operator()(const font_face_ref& ref) const;
    };

    struct indexed_font_face
    {
      font_face_ref ref;
      std::string family_name;
      std::string full_name;
      std::string subfamily_name;
      std::string post_script_name;
      uint32_t weight = BL_FONT_WEIGHT_NORMAL;
      uint32_t style = BL_FONT_STYLE_NORMAL;
      size_t discovery_order = 0;
    };

    struct font_request
    {
      std::string original_name;
      std::string normalized_name;
      std::string family;
      bool bold = false;
      bool italic = false;
      bool symbolic = false;
      bool standard_14 = false;
    };

    struct match_cache_key
    {
      std::string normalized_query;
      int cutoff_x10000 = 0;

      bool operator==(const match_cache_key& other) const;
    };

    struct match_cache_key_hash
    {
      std::size_t operator()(const match_cache_key& key) const;
    };

    // Strips leading PDF name slash and six-letter subset prefixes such as
    // ABCDEF+Times-Roman.
    static std::string strip_subset_prefix(const std::string& name);
    static std::string select_font_query(const std::string& font_name,
                                         const std::string& base_font);

    // Normalizes a PDF/system font name into the comparable form used by the
    // resolver index. This strips PDF subset prefixes, replaces punctuation
    // with spaces, splits camel-case family/style names, lowercases ASCII text,
    // removes common PostScript suffixes such as "PSMT", and collapses spaces.
    static std::string normalize_font_name(const std::string& name);

    // Splits a normalized font name on whitespace while preserving token order.
    static std::vector<std::string> split_tokens(const std::string& s);

    // Returns true for weight/style descriptors that should not participate
    // in family identity matching. These tokens can still affect style
    // selection, but are ignored for family-level matching.
    static bool is_style_token(const std::string& tok);

    // Returns only non-style tokens from a tokenized font name.
    static std::vector<std::string> significant_tokens(const std::vector<std::string>& toks);

    static int quantized_cutoff(float cutoff);

    // Numeric weight (100..900) and slant a font FILENAME advertises, for
    // ranking files the Blend2D index does not hold -- the ones it refused to
    // open. Returns 400 when the stem says nothing.
    static int stem_weight(const std::string& stem);
    static bool stem_is_italic(const std::string& stem);

    static std::optional<std::string> getenv_string(const char* name);
    static void append_env_path(std::vector<std::filesystem::path>& paths,
                                const char* env_name,
                                const std::filesystem::path& suffix = {});
    static std::vector<std::filesystem::path> system_font_directories();
    static std::vector<std::filesystem::path> fallback_font_candidates();
    static bool is_font_file(const std::filesystem::path& p);
    static void scan_for_fallback_fonts(std::vector<std::filesystem::path>& paths);
    // Latin fallbacks cannot draw CJK (.notdef / "tofu"); detect CJK requests
    // so the resolver can prefer a CJK-capable face.
    static bool is_cjk_font_request(const std::string& name);
    // Reads the built index, so it must run after build_font_index() has
    // filled it: on macOS the CJK faces are only recognisable by the family
    // name their `name` table carries, never by their filename.
    std::vector<std::filesystem::path> cjk_fallback_candidates() const;

    static std::vector<std::filesystem::path> arabic_fallback_candidates();

    // Maps text to a coarse script bucket ("arabic", "cjk", "hebrew", ...);
    // false for Latin-only text.
    static bool text_script_key(const std::string& utf8_text, std::string& script_key);

    // A "last resort" face carries a glyph for every code point, all of them
    // placeholder boxes. It answers any coverage probe perfectly and draws
    // nothing legible, so it must never win one.
    static bool is_last_resort_face(const std::string& name);

    // How many glyphs of `utf8_text` the face actually has, and how many the
    // text shapes to. Zero when the face cannot shape the text at all.
    static std::size_t face_coverage(BLFontFace& face,
                                     const std::string& utf8_text,
                                     std::size_t& total);

    // Same question for a candidate the resolver has not committed to yet.
    // Falls back to FreeType's cmap when Blend2D could not open the file, so
    // a face it refuses is still ranked instead of dropping out of the
    // running unseen -- which is how a CFF2-only host ends up reporting that
    // nothing covers the script.
    std::size_t candidate_coverage(const resolved_face& candidate,
                                   const std::string& utf8_text,
                                   std::size_t& total);

    // Whether `face` has a glyph for every character of `utf8_text`.
    static bool face_covers_text(BLFontFace& face, const std::string& utf8_text);


    static std::string bl_string_to_std(const BLString& s);
    static std::string font_ref_key(const font_face_ref& ref);

    static bool has_prefix(const std::string& s, const std::string& prefix);
    static bool contains_token(const std::vector<std::string>& toks,
                               const std::string& token);
    static std::vector<std::string> compatible_family_names(const std::string& normalized_family);
    static font_request parse_font_request(const std::string& name);
    static bool lookup_alias(font_request& request);
    static bool is_tex_font_request(font_request& request);

    void build_font_index();
    void index_font_file(const std::filesystem::path& path, size_t& discovery_order);
    void index_font_face(const indexed_font_face& face);

    std::optional<font_face_ref> resolve_font_ref(const std::string& cache_key,
                                                  float font_similarity_cutoff);
    std::optional<font_face_ref> exact_find_font(const font_request& request) const;
    std::optional<font_face_ref> find_by_family_candidates(
      const std::vector<font_face_ref>& refs,
      const font_request& request) const;
    // Walks `candidates` and returns the first one some backend can actually
    // draw. Testing fs::exists() alone was not enough: when the file it
    // returned failed to load, the caller handed the page an invalid face and
    // never looked at the next candidate, so one unloadable font took the
    // whole page down to bbox outlines.
    resolved_face find_first_loadable_fallback(
      const std::vector<std::filesystem::path>& candidates,
      const char* kind);

    // The indexed ref for a font file (lowest face index, so the choice is
    // deterministic), or face 0 when the file is not in the index.
    font_face_ref indexed_ref_for_path(const std::string& path) const;
    std::optional<font_face_ref> fuzzy_find_font(const font_request& request,
                                                 float font_similarity_cutoff) const;

    BLFontFace load_font_face(const font_face_ref& ref);

    // load_font_face() plus the file it came from. When Blend2D refuses the
    // file the path survives only if FreeType can open it; otherwise the
    // result is not drawable and the caller moves on.
    resolved_face load_resolved_face(const font_face_ref& ref);

    std::once_flag index_once_;
    std::unordered_map<std::string, std::vector<font_face_ref>> name_index_;
    std::unordered_map<std::string, indexed_font_face> face_metadata_;
    std::vector<std::filesystem::path> fallback_candidates_;
    std::vector<std::filesystem::path> cjk_candidates_;
    std::vector<std::filesystem::path> arabic_candidates_;
    mutable std::shared_mutex script_cache_mutex_;
    // Per script, the faces already found to cover text of that script, keyed
    // by path so the same one is not kept twice. One face per script is not
    // enough: whether a face covers a run is a property of the run, not of the
    // script.
    std::unordered_map<std::string, std::vector<resolved_face>> script_face_cache_;
    // Probe rounds that ended without a fully covering face, per script. The
    // probe is bounded work, but a host with nothing for the script would pay
    // it again on every run.
    std::unordered_map<std::string, std::size_t> script_probe_rounds_;

    mutable std::shared_mutex match_cache_mutex_;
    std::unordered_map<match_cache_key,
                       std::optional<font_face_ref>,
                       match_cache_key_hash> match_cache_;

    mutable std::shared_mutex face_cache_mutex_;
    std::unordered_map<font_face_ref, BLFontFace, font_face_ref_hash> face_cache_;

    // Draws (and probes) the system font files Blend2D cannot open. Shared
    // process-wide: system faces are the same for every page.
    std::shared_ptr<freetype_font_cache> freetype_cache_ =
      freetype_font_cache::default_cache();
  };

  inline blend2d_font_resolver::blend2d_font_resolver() = default;

  inline std::shared_ptr<blend2d_font_resolver> blend2d_font_resolver::default_resolver()
  {
    static std::shared_ptr<blend2d_font_resolver> resolver = []()
    {
      auto shared = std::make_shared<blend2d_font_resolver>();
      shared->warm();
      return shared;
    }();

    return resolver;
  }

  inline void blend2d_font_resolver::warm()
  {
    std::call_once(index_once_, [this]() { build_font_index(); });
  }

  inline BLFontFace blend2d_font_resolver::resolve_font_face(
                                                             const std::string& font_name,
                                                             const std::string& base_font,
                                                             bool resolve_fonts,
                                                             float font_similarity_cutoff)
  {
    return resolve_font(font_name, base_font, resolve_fonts,
                        font_similarity_cutoff).face;
  }

  inline blend2d_font_resolver::resolved_face blend2d_font_resolver::resolve_font(
                                                             const std::string& font_name,
                                                             const std::string& base_font,
                                                             bool resolve_fonts,
                                                             float font_similarity_cutoff)
  {
    const std::string cache_key = select_font_query(font_name, base_font);

    LOG_S(INFO) << "blend2d font resolver: resolve_font"
                << " font_name=`" << font_name << "`"
                << " base_font=`" << base_font << "`"
                << " selected_key=`" << cache_key << "`"
                << " resolve_fonts=" << (resolve_fonts ? "true" : "false")
                << " similarity_cutoff=" << font_similarity_cutoff;

    if (resolve_fonts)
      {
        const std::optional<font_face_ref> font_ref =
          resolve_font_ref(cache_key, font_similarity_cutoff);

        if (font_ref.has_value() and not font_ref->path.empty())
          {
            resolved_face resolved = load_resolved_face(*font_ref);
            if (resolved.is_drawable())
              {
                LOG_S(INFO) << "blend2d font resolver: loading resolved font"
                            << " selected_key=`" << cache_key << "`"
                            << " path=`" << resolved.path << "`"
                            << " face_index=" << resolved.face_index
                            << " blend2d=" << (resolved.is_valid() ? "true" : "false");
                return resolved;
              }

            // The name matched a file no backend can open. Falling through to
            // the fallbacks beats handing the page an invalid face.
            LOG_S(WARNING) << "blend2d font resolver: name-matched font is unusable"
                           << " selected_key=`" << cache_key << "`"
                           << " path=`" << font_ref->path << "`";
          }
      }

    warm();

    // Prefer a CJK-capable face before the Latin fallback (.notdef boxes).
    if (is_cjk_font_request(cache_key) or is_cjk_font_request(font_name)
        or is_cjk_font_request(base_font))
      {
        resolved_face resolved = find_first_loadable_fallback(cjk_candidates_, "CJK");
        if (resolved.is_drawable())
          {
            LOG_S(INFO) << "blend2d font resolver: using CJK fallback font"
                        << " selected_key=`" << cache_key << "`"
                        << " path=`" << resolved.path << "`"
                        << " blend2d=" << (resolved.is_valid() ? "true" : "false");
            return resolved;
          }

        LOG_S(WARNING) << "blend2d font resolver: CJK request but no CJK-capable"
                       << " font installed; glyphs will render as .notdef boxes."
                       << " Install e.g. fonts-noto-cjk (Debian/Ubuntu) or"
                       << " google-noto-sans-cjk-fonts (Fedora/RHEL), or point"
                       << " DOCLING_PARSE_CJK_FALLBACK_FONT at a CJK font file."
                       << " selected_key=`" << cache_key << "`";
      }

    LOG_S(INFO) << "blend2d font resolver: using fallback font"
                << " selected_key=`" << cache_key << "`";

    resolved_face resolved = find_first_loadable_fallback(fallback_candidates_, "Latin");
    if (not resolved.is_drawable())
      {
        LOG_S(WARNING) << "blend2d font resolver: no usable font on this host"
                       << " selected_key=`" << cache_key << "`";
      }

    return resolved;
  }

  inline bool blend2d_font_resolver::font_face_ref::operator==(
                                                               const font_face_ref& other) const
  {
    return path == other.path and face_index == other.face_index;
  }

  inline std::size_t blend2d_font_resolver::font_face_ref_hash::operator()(
                                                                           const font_face_ref& ref) const
  {
    const std::size_t h1 = std::hash<std::string>{}(ref.path);
    const std::size_t h2 = std::hash<uint32_t>{}(ref.face_index);
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
  }

  inline bool blend2d_font_resolver::match_cache_key::operator==(
                                                                 const match_cache_key& other) const
  {
    return normalized_query == other.normalized_query and
      cutoff_x10000 == other.cutoff_x10000;
  }

  inline std::size_t blend2d_font_resolver::match_cache_key_hash::operator()(
                                                                             const match_cache_key& key) const
  {
    const std::size_t h1 = std::hash<std::string>{}(key.normalized_query);
    const std::size_t h2 = std::hash<int>{}(key.cutoff_x10000);
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
  }

  inline std::string blend2d_font_resolver::strip_subset_prefix(const std::string& name)
  {
    std::string s = name;
    if (not s.empty() and s[0] == '/') { s = s.substr(1); }

    if (s.size() > 7 and s[6] == '+' and
        std::all_of(s.begin(), s.begin() + 6,
                    [](char c){ return std::isupper(static_cast<unsigned char>(c)); }))
      {
        s = s.substr(7);
      }

    return s;
  }

  inline std::string blend2d_font_resolver::select_font_query(
                                                              const std::string& font_name,
                                                              const std::string& base_font)
  {
    // /BaseFont is the PostScript name of the typeface. The other name is
    // whatever the font dictionary's /Name says, which is the label the
    // resource dictionary references the font by and is deprecated as of PDF
    // 1.7 (32000-1, Table 111) -- producers put arbitrary strings there.
    // Preferring it loses the typeface: an arXiv stamp declared
    // `/BaseFont /Times-Roman /Name /arXivStAmP` matched nothing under its
    // label and fell through to the generic sans fallback, so the stamp came
    // out in the wrong face while every other renderer set it in a serif.
    //
    // Only `F<digits>` used to be recognised as a label, which is the common
    // shape but not a rule any producer follows.
    const bool has_base_font = not base_font.empty() and base_font != "null";
    if (has_base_font) { return base_font; }

    if (not font_name.empty() and font_name != "null") { return font_name; }
    return base_font;
  }

  inline std::string blend2d_font_resolver::normalize_font_name(const std::string& name)
  {
    std::string s = strip_subset_prefix(name);

    std::string expanded;
    expanded.reserve(s.size() * 2);
    char prev = '\0';
    for (char raw : s)
      {
        const unsigned char ch = static_cast<unsigned char>(raw);
        if (raw == '_' or raw == '-' or raw == '/' or raw == '\\' or raw == '+')
          {
            expanded += ' ';
            prev = ' ';
            continue;
          }

        if (std::isalnum(ch))
          {
            if (not expanded.empty()
                and std::isupper(ch)
                and std::islower(static_cast<unsigned char>(prev)))
              {
                expanded += ' ';
              }
            if (not expanded.empty()
                and std::isdigit(ch)
                and std::isalpha(static_cast<unsigned char>(prev)))
              {
                expanded += ' ';
              }
            if (not expanded.empty()
                and std::isalpha(ch)
                and std::isdigit(static_cast<unsigned char>(prev)))
              {
                expanded += ' ';
              }
            expanded += static_cast<char>(std::tolower(ch));
            prev = raw;
          }
        else
          {
            expanded += ' ';
            prev = ' ';
          }
      }

    // "PS" and "MT" are Monotype's PostScript vendor tags, and they sit around
    // the style rather than only after it: TimesNewRomanPSMT normalises to
    // "times new roman", but TimesNewRomanPS-BoldMT put a "ps" in the middle
    // and only the trailing "mt" was ever removed. The leftover
    // "times new roman ps bold" matched no alias, so the regular weight of a
    // Times document resolved to a serif while its bold and italic fell
    // through to the generic sans -- one paragraph set in three faces.
    //
    // Dropping the tags as whole tokens wherever they appear keeps the family
    // and the style together. They are only ever vendor tags, never a family
    // of their own, and a name that is nothing else is left alone.
    {
      std::vector<std::string> kept;
      std::istringstream stream(expanded);
      std::string token;
      while (stream >> token)
        {
          if (token != "ps" and token != "mt" and token != "psmt")
            {
              kept.push_back(token);
            }
        }

      if (not kept.empty())
        {
          std::ostringstream rebuilt;
          for (size_t l = 0; l < kept.size(); l++)
            {
              if (l > 0) { rebuilt << ' '; }
              rebuilt << kept[l];
            }
          expanded = rebuilt.str();
        }
    }

    std::string collapsed;
    bool pending_space = false;
    for (char c : expanded)
      {
        if (std::isspace(static_cast<unsigned char>(c)))
          {
            pending_space = not collapsed.empty();
            continue;
          }
        if (pending_space)
          {
            collapsed += ' ';
            pending_space = false;
          }
        collapsed += c;
      }

    return collapsed;
  }

  inline std::vector<std::string> blend2d_font_resolver::split_tokens(const std::string& s)
  {
    std::vector<std::string> toks;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) { toks.push_back(tok); }
    return toks;
  }

  inline bool blend2d_font_resolver::is_style_token(const std::string& tok)
  {
    static const std::array<const char*, 19> kStyleTokens = {
      "regular", "normal", "roman", "book", "medium", "medi",
      "bold", "italic", "ital", "oblique", "obli", "light", "thin",
      "black", "heavy", "semibold", "demibold", "regu", "plain"
    };
    return std::find(kStyleTokens.begin(), kStyleTokens.end(), tok) != kStyleTokens.end();
  }

  inline std::vector<std::string> blend2d_font_resolver::significant_tokens(
                                                                            const std::vector<std::string>& toks)
  {
    std::vector<std::string> out;
    for (const auto& tok : toks)
      {
        if (not is_style_token(tok))
          {
            out.push_back(tok);
          }
      }
    return out;
  }

  inline int blend2d_font_resolver::quantized_cutoff(float cutoff)
  {
    return static_cast<int>(std::lround(cutoff * 10000.0f));
  }

  inline std::optional<std::string> blend2d_font_resolver::getenv_string(const char* name)
  {
    const char* value = std::getenv(name);
    if (value == nullptr or value[0] == '\0') { return std::nullopt; }
    return std::string(value);
  }

  inline void blend2d_font_resolver::append_env_path(
                                                     std::vector<std::filesystem::path>& paths,
                                                     const char* env_name,
                                                     const std::filesystem::path& suffix)
  {
    auto value = getenv_string(env_name);
    if (not value.has_value()) { return; }
    std::filesystem::path path(*value);
    if (not suffix.empty()) { path /= suffix; }
    paths.push_back(path);
  }

  inline std::vector<std::filesystem::path> blend2d_font_resolver::system_font_directories()
  {
    std::vector<std::filesystem::path> dirs;

#if defined(_WIN32)
    append_env_path(dirs, "WINDIR", "Fonts");
    // SystemRoot covers stripped environments where WINDIR is unset.
    append_env_path(dirs, "SystemRoot", "Fonts");
    append_env_path(dirs, "LOCALAPPDATA", std::filesystem::path("Microsoft") / "Windows" / "Fonts");
#elif defined(__APPLE__)
    dirs.emplace_back("/System/Library/Fonts");
    dirs.emplace_back("/System/Library/Fonts/Supplemental");
    dirs.emplace_back("/Library/Fonts");
    append_env_path(dirs, "HOME", std::filesystem::path("Library") / "Fonts");
#else
    dirs.emplace_back("/usr/share/fonts");
    dirs.emplace_back("/usr/local/share/fonts");
    append_env_path(dirs, "XDG_DATA_HOME", "fonts");
    append_env_path(dirs, "HOME", std::filesystem::path(".local") / "share" / "fonts");
    append_env_path(dirs, "HOME", ".fonts");
#endif

    // Package-shipped fonts last so a real system font of the same name wins,
    // while still allowing NAME matching (e.g. Times -> Liberation Serif).
    {
      std::error_code ec;
      const std::filesystem::path bundled =
        resource_utils::get_resources_dir(false) / "fonts" / "fallback";
      if (std::filesystem::is_directory(bundled, ec)) { dirs.push_back(bundled); }
    }

    return dirs;
  }

  inline std::vector<std::filesystem::path> blend2d_font_resolver::fallback_font_candidates()
  {
    // Portable fallback order: env override, package-shipped fonts, well-known
    // paths per OS/distro, then a bounded scan of system_font_directories().
    // Hardcoding only Debian layout left RHEL-family hosts with outline boxes.
    namespace fs = std::filesystem;
    std::vector<fs::path> paths;

    if (auto override_path = getenv_string("DOCLING_PARSE_FALLBACK_FONT"))
      {
        paths.emplace_back(*override_path);
      }

    // Bundled fonts: sans before serif before mono (not alphabetical), so a
    // failed name match does not substitute LiberationMono for proportional text.
    {
      std::error_code ec;
      const fs::path bundled =
        resource_utils::get_resources_dir(false) / "fonts" / "fallback";
      if (fs::is_directory(bundled, ec))
        {
          std::vector<fs::path> found;
          for (const auto& entry : fs::directory_iterator(bundled, ec))
            {
              if (is_font_file(entry.path())) { found.push_back(entry.path()); }
            }
          std::sort(found.begin(), found.end());

          auto rank = [](const fs::path& p)
          {
            std::string s = p.stem().string();
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (s.find("mono") != std::string::npos) { return 2; }
            if (s.find("serif") != std::string::npos) { return 1; }
            return 0;  // sans / everything else
          };
          std::stable_sort(found.begin(), found.end(),
                           [&rank](const fs::path& a, const fs::path& b)
                           { return rank(a) < rank(b); });
          paths.insert(paths.end(), found.begin(), found.end());
        }
    }

#if defined(_WIN32)
    for (const char* env : {"WINDIR", "SystemRoot"})
      {
        for (const char* name : {"arial.ttf", "segoeui.ttf", "calibri.ttf",
                                 "tahoma.ttf", "verdana.ttf", "times.ttf", "cour.ttf"})
          {
            append_env_path(paths, env, fs::path("Fonts") / name);
          }
      }
#elif defined(__APPLE__)
    paths.emplace_back("/System/Library/Fonts/Helvetica.ttc");
    paths.emplace_back("/System/Library/Fonts/Supplemental/Arial.ttf");
    paths.emplace_back("/Library/Fonts/Arial.ttf");
    paths.emplace_back("/System/Library/Fonts/SFNS.ttf");
#else
    // Debian / Ubuntu
    paths.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    paths.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf");
    paths.emplace_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    paths.emplace_back("/usr/share/fonts/truetype/freefont/FreeSans.ttf");
    paths.emplace_back("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf");
    // RHEL / Fedora / CentOS (fonts live under per-package directories)
    paths.emplace_back("/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf");
    paths.emplace_back("/usr/share/fonts/dejavu/DejaVuSans.ttf");
    paths.emplace_back("/usr/share/fonts/liberation-fonts/LiberationSans-Regular.ttf");
    paths.emplace_back("/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf");
    paths.emplace_back("/usr/share/fonts/google-noto/NotoSans-Regular.ttf");
    // Arch
    paths.emplace_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
    paths.emplace_back("/usr/share/fonts/TTF/LiberationSans-Regular.ttf");
    // openSUSE
    paths.emplace_back("/usr/share/fonts/truetype/DejaVuSans.ttf");
    // Alpine
    paths.emplace_back("/usr/share/fonts/ttf-dejavu/DejaVuSans.ttf");
#endif

    // Bounded scan for unusual layouts; last resort is any font file found.
    scan_for_fallback_fonts(paths);

    return paths;
  }

  inline bool blend2d_font_resolver::is_cjk_font_request(const std::string& name)
  {
    // Non-ASCII names (e.g. Shift-JIS MS PGothic) or known CJK family tokens.
    for (unsigned char c : name)
      {
        if (c >= 0x80) { return true; }
      }

    std::string s;
    s.reserve(name.size());
    for (char c : name)
      {
        if (std::isalnum(static_cast<unsigned char>(c)))
          {
            s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
          }
      }

    static const std::array<const char*, 24> tokens = {
      "mincho", "gothic", "meiryo", "yugothic", "yumincho", "hiragino",
      "simsun", "simhei", "simkai", "nsimsun", "fangsong", "kaiti", "songti",
      "heiti", "msyahei", "msyh", "msung", "mingliu", "pmingliu", "batang",
      "gulim", "dotum", "malgun", "nanum",
    };
    for (const char* t : tokens)
      {
        if (s.find(t) != std::string::npos) { return true; }
      }
    return false;
  }


  inline std::vector<std::filesystem::path>
  blend2d_font_resolver::arabic_fallback_candidates()
  {
    namespace fs = std::filesystem;
    std::vector<fs::path> paths;

    if (auto override_path = getenv_string("DOCLING_PARSE_ARABIC_FALLBACK_FONT"))
      {
        paths.emplace_back(*override_path);
      }

    // Filename stems of faces that carry Arabic, best first. The generic
    // faces at the end matter more here than in the CJK list: DejaVu and
    // Arial both cover Arabic, so most hosts can draw it even with no
    // purpose-built Arabic font installed.
    static const std::array<const char*, 18> wanted = {
      // Linux distributions
      "notonaskharabic", "notosansarabic", "notokufiarabic",
      "amiri", "scheherazade", "kacst", "droidsansarabic",
      // Windows (%WINDIR%\Fonts)
      "arabtype",    // Arabic Typesetting
      "trado",       // Traditional Arabic
      "majalla",     // Sakkal Majalla
      "andlso",      // Andalus
      "simpo",       // Simplified Arabic
      // macOS
      "geezapro", "albayan", "baghdad", "damascus",
      // broad faces that happen to include Arabic
      "dejavusans", "arial",
    };

    constexpr std::size_t max_entries = 20000;
    std::size_t seen = 0;
    std::vector<fs::path> ranked(wanted.size());

    for (const auto& dir : system_font_directories())
      {
        std::error_code ec;
        if (not fs::is_directory(dir, ec)) { continue; }
        fs::recursive_directory_iterator it(
          dir, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; it != end and seen < max_entries; it.increment(ec))
          {
            if (ec) { break; }
            ++seen;
            const fs::path& p = it->path();
            if (not is_font_file(p)) { continue; }
            std::string stem = p.stem().string();
            std::transform(stem.begin(), stem.end(), stem.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            for (std::size_t i = 0; i < wanted.size(); i++)
              {
                if (ranked[i].empty() and stem.find(wanted[i]) != std::string::npos)
                  {
                    ranked[i] = p;
                  }
              }
          }
        if (seen >= max_entries) { break; }
      }

    for (const auto& p : ranked)
      {
        if (not p.empty()) { paths.push_back(p); }
      }
    return paths;
  }


  inline bool blend2d_font_resolver::text_script_key(const std::string& utf8_text,
                                                     std::string& script_key)
  {
    // Minimal UTF-8 decode: only the codepoint ranges matter, and the text is
    // already known-good UTF-8 by the time it reaches the renderer.
    const unsigned char* s = reinterpret_cast<const unsigned char*>(utf8_text.data());
    const std::size_t n = utf8_text.size();

    for (std::size_t i = 0; i < n;)
      {
        uint32_t cp = 0;
        std::size_t len = 1;

        if (s[i] < 0x80)            { cp = s[i]; len = 1; }
        else if ((s[i] & 0xE0) == 0xC0) { cp = s[i] & 0x1F; len = 2; }
        else if ((s[i] & 0xF0) == 0xE0) { cp = s[i] & 0x0F; len = 3; }
        else if ((s[i] & 0xF8) == 0xF0) { cp = s[i] & 0x07; len = 4; }
        else { i += 1; continue; }

        if (i + len > n) { break; }
        for (std::size_t k = 1; k < len; ++k)
          {
            cp = (cp << 6) | (s[i + k] & 0x3F);
          }
        i += len;

        if (cp < 0x0250) { continue; }  // Latin, punctuation, digits

        if ((cp >= 0x0600 and cp <= 0x06FF) or (cp >= 0x0750 and cp <= 0x077F) or
            (cp >= 0xFB50 and cp <= 0xFDFF) or (cp >= 0xFE70 and cp <= 0xFEFF))
          { script_key = "arabic"; return true; }

        if ((cp >= 0x2E80 and cp <= 0x9FFF) or (cp >= 0x3040 and cp <= 0x30FF) or
            (cp >= 0xAC00 and cp <= 0xD7AF) or (cp >= 0xF900 and cp <= 0xFAFF) or
            (cp >= 0x20000 and cp <= 0x2FA1F))
          { script_key = "cjk"; return true; }

        if (cp >= 0x0590 and cp <= 0x05FF) { script_key = "hebrew"; return true; }
        if (cp >= 0x0900 and cp <= 0x097F) { script_key = "devanagari"; return true; }
        if (cp >= 0x0E00 and cp <= 0x0E7F) { script_key = "thai"; return true; }
        if (cp >= 0x0400 and cp <= 0x04FF) { script_key = "cyrillic"; return true; }
        if (cp >= 0x0370 and cp <= 0x03FF) { script_key = "greek"; return true; }

        script_key = "other";
        return true;
      }

    return false;
  }


  inline bool blend2d_font_resolver::shaped_run_all_notdef(const BLGlyphBuffer& gb)
  {
    const std::size_t count = gb.size();
    const uint32_t* ids = gb.glyph_run().glyph_data_as<uint32_t>();
    if (count == 0 or ids == nullptr) { return true; }
    for (std::size_t i = 0; i < count; ++i)
      {
        if (ids[i] != 0) { return false; }
      }
    return true;
  }


  inline bool blend2d_font_resolver::is_last_resort_face(const std::string& name)
  {
    std::string normalized = normalize_font_name(name);
    normalized.erase(std::remove(normalized.begin(), normalized.end(), ' '),
                     normalized.end());

    return normalized.find("lastresort") != std::string::npos;
  }

  inline std::size_t blend2d_font_resolver::face_coverage(BLFontFace& face,
                                                          const std::string& utf8_text,
                                                          std::size_t& total)
  {
    total = 0;
    if (not face.is_valid()) { return 0; }

    BLFont probe;
    if (probe.create_from_face(face, 16.0f) != BL_SUCCESS) { return 0; }

    BLGlyphBuffer gb;
    gb.set_utf8_text(utf8_text.c_str());
    if (probe.shape(gb) != BL_SUCCESS or gb.is_empty()) { return 0; }

    const std::size_t count = gb.size();
    const uint32_t* ids = gb.glyph_run().glyph_data_as<uint32_t>();
    if (ids == nullptr) { return 0; }

    total = count;

    std::size_t covered = 0;
    for (std::size_t i = 0; i < count; ++i)
      {
        if (ids[i] != 0) { ++covered; }
      }

    return covered;
  }

  inline bool blend2d_font_resolver::face_covers_text(BLFontFace& face,
                                                      const std::string& utf8_text)
  {
    // Covering *some* of the text is not covering it. A face holding a
    // fraction of a script's repertoire shapes the common characters and
    // leaves .notdef for the rest, and accepting it on that basis put boxes
    // through a page of simplified Chinese wherever the face fell short.
    std::size_t total = 0;
    const std::size_t covered = face_coverage(face, utf8_text, total);

    return total > 0 and covered == total;
  }


  inline BLFontFace blend2d_font_resolver::resolve_face_for_text(const std::string& utf8_text)
  {
    return resolve_font_for_text(utf8_text).face;
  }

  inline blend2d_font_resolver::resolved_face
  blend2d_font_resolver::resolve_font_for_text(const std::string& utf8_text)
  {
    std::string script;
    if (not text_script_key(utf8_text, script))
      {
        return {};  // Latin-only: the name-based path is correct
      }

    warm();

    // How many fruitless probe rounds are worth spending on one script before
    // settling for the best face found so far.
    constexpr std::size_t max_fruitless_rounds = 8;

    resolved_face first_known;
    bool have_first_known = false;
    {
      std::vector<resolved_face> known;
      {
        std::shared_lock<std::shared_mutex> lock(script_cache_mutex_);

        auto it = script_face_cache_.find(script);
        if (it != script_face_cache_.end()) { known = it->second; }

        auto rounds = script_probe_rounds_.find(script);
        if (not known.empty()) { first_known = known.front(); have_first_known = true; }

        if (known.empty() and rounds != script_probe_rounds_.end() and
            rounds->second >= max_fruitless_rounds)
          {
            return {};
          }
      }

      // Probing calls into FreeType, which takes its own lock; doing it while
      // holding the script cache lock invites a lock-order problem for no
      // gain, so the candidates are copied out first.
      for (const auto& entry : known)
        {
          std::size_t total = 0;
          const std::size_t covered = candidate_coverage(entry, utf8_text, total);
          if (total > 0 and covered == total) { return entry; }
        }

      {
        std::shared_lock<std::shared_mutex> lock(script_cache_mutex_);
        auto rounds = script_probe_rounds_.find(script);
        if (rounds != script_probe_rounds_.end() and
            rounds->second >= max_fruitless_rounds)
          {
            // Nothing installed covers this script fully; stop looking and
            // draw what the host can.
            return have_first_known ? first_known : resolved_face{};
          }
      }
    }

    // Script-specific faces first, then every indexed face in discovery order,
    // so a host with nothing purpose-built still finds a broad face that
    // happens to cover the script.
    std::vector<font_face_ref> refs;
    const std::vector<std::filesystem::path>& preferred =
      (script == "cjk") ? cjk_candidates_
                        : (script == "arabic" ? arabic_candidates_ : fallback_candidates_);

    for (const auto& p : preferred)
      {
        std::error_code ec;
        if (std::filesystem::exists(p, ec) and not is_last_resort_face(p.stem().string()))
          {
            refs.push_back(indexed_ref_for_path(p.string()));
          }
      }

    std::vector<const indexed_font_face*> indexed;
    indexed.reserve(face_metadata_.size());
    for (const auto& kv : face_metadata_) { indexed.push_back(&kv.second); }
    std::sort(indexed.begin(), indexed.end(),
              [](const indexed_font_face* a, const indexed_font_face* b)
              { return a->discovery_order < b->discovery_order; });
    for (const auto* meta : indexed)
      {
        if (is_last_resort_face(meta->family_name) or
            is_last_resort_face(std::filesystem::path(meta->ref.path).stem().string()))
          {
            continue;
          }

        refs.push_back(meta->ref);
      }

    // Probing means shaping, so bound the work; the answer is cached per script.
    constexpr std::size_t max_probes = 80;

    resolved_face chosen;
    bool full_cover = false;
    resolved_face best_partial;
    std::size_t best_covered = 0;
    std::size_t probes = 0;
    for (const auto& ref : refs)
      {
        if (probes++ >= max_probes) { break; }

        resolved_face candidate = load_resolved_face(ref);
        if (not candidate.is_drawable()) { continue; }

        std::size_t total = 0;
        const std::size_t covered = candidate_coverage(candidate, utf8_text, total);

        if (total > 0 and covered == total)
          {
            chosen = candidate;
            full_cover = true;
            LOG_S(INFO) << "blend2d font resolver: script '" << script
                        << "' resolved to " << ref.path
                        << " (blend2d=" << (candidate.is_valid() ? "true" : "false") << ")"
                        << " after " << probes << " probe(s)";
            break;
          }

        // Remember the closest miss: half a run of glyphs beats a full run of
        // boxes when the host has nothing better.
        if (covered > best_covered)
          {
            best_covered = covered;
            best_partial = candidate;
          }
      }

    if (not full_cover)
      {
        chosen = best_partial.is_drawable() ? best_partial
               : (have_first_known ? first_known : resolved_face{});

        LOG_S(WARNING) << "blend2d font resolver: no installed face covers all of"
                       << " `" << utf8_text << "` (script '" << script << "');"
                       << " the characters it lacks will render as .notdef boxes";
      }

    {
      // Keep the faces that actually cover something; a near miss is only
      // worth keeping when there is nothing else to fall back on.
      constexpr std::size_t max_cached_faces = 8;

      std::unique_lock<std::shared_mutex> lock(script_cache_mutex_);

      auto& faces = script_face_cache_[script];
      const bool worth_caching =
        chosen.is_drawable() and
        (full_cover or faces.empty()) and faces.size() < max_cached_faces;

      if (worth_caching)
        {
          const bool known =
            std::any_of(faces.begin(), faces.end(),
                        [&](const resolved_face& entry)
                        { return entry.path == chosen.path and
                                 entry.face_index == chosen.face_index; });

          if (not known) { faces.push_back(chosen); }
        }

      if (not full_cover)
        {
          script_probe_rounds_[script] += 1;
        }
    }

    return chosen;
  }

  inline std::vector<std::filesystem::path>
  blend2d_font_resolver::cjk_fallback_candidates() const
  {
    namespace fs = std::filesystem;
    std::vector<fs::path> paths;

    if (auto override_path = getenv_string("DOCLING_PARSE_CJK_FALLBACK_FONT"))
      {
        paths.emplace_back(*override_path);
      }

    // Faces with CJK coverage, best first. Each entry is matched against both
    // the lowercased filename stem and the lowercased family name from the
    // font's `name` table: matching filenames alone reported "no CJK font
    // installed" on macOS, which carries a dozen of them but names the files
    // in Japanese -- and in NFD at that, so even the Japanese spelling of the
    // name does not compare equal to the one written here.
    static const std::array<const char*, 25> wanted = {
      "notosanscjk", "notoserifcjk", "notosanscjkjp", "notosanscjksc",
      "sourcehansans", "sourcehanserif",
      // macOS
      "hiraginosans", "hiraginokakugothic", "hiraginomincho",
      "hiraginomarugothic", "pingfang", "applesdgothicneo", "stheiti",
      "songti", "applegothic",
      // Windows
      "yugoth", "msgothic", "msmincho", "meiryo", "microsoftyahei", "malgun",
      // Linux distributions
      "droidsansfallback", "wqy-zenhei", "wqy-microhei", "arphic",
    };

    std::vector<fs::path> ranked(wanted.size());
    std::vector<int> ranked_penalty(wanted.size(), INT_MAX);

    // Ranking is not "first file the scan reached", which on a host with the
    // static Noto packages installed meant NotoSansCJK-Thin.ttc -- body text
    // drawn in Thin, measurably further from the groundtruth than Regular.
    // Lower is better: distance from regular weight, then upright before
    // italic, and last a large penalty for a file Blend2D could not open, so
    // a face it can shape always wins over one only FreeType can draw.
    constexpr int slanted_penalty = 1000;
    constexpr int unloadable_penalty = 5000;

    auto consider = [&](const std::string& text, const fs::path& p, int penalty)
    {
      // The family names carry spaces the entries above do not; comparing the
      // compacted forms lets one entry cover "Hiragino Sans" and a
      // "HiraginoSans" filename alike.
      std::string key;
      for (unsigned char c : text)
        {
          if (std::isspace(c) or c == '-' or c == '_') { continue; }
          key += static_cast<char>(std::tolower(c));
        }

      for (std::size_t i = 0; i < wanted.size(); i++)
        {
          std::string needle;
          for (const char* q = wanted[i]; *q != '\0'; q++)
            {
              if (*q == '-') { continue; }
              needle += *q;
            }

          if (key.find(needle) != std::string::npos and penalty < ranked_penalty[i])
            {
              ranked[i] = p;
              ranked_penalty[i] = penalty;
            }
        }
    };

    // Files Blend2D could open: it read their weight and style out of the
    // font, and on macOS the family name from the `name` table is the only
    // thing that identifies a CJK face at all -- the filenames are Japanese,
    // in NFD, so even the Japanese spelling does not compare equal.
    std::unordered_map<std::string, int> indexed_penalty;
    for (const auto& kv : face_metadata_)
      {
        const indexed_font_face& meta = kv.second;
        const bool slanted = meta.style == BL_FONT_STYLE_ITALIC or
                             meta.style == BL_FONT_STYLE_OBLIQUE;
        const int penalty =
          std::abs(static_cast<int>(meta.weight) -
                   static_cast<int>(BL_FONT_WEIGHT_NORMAL)) +
          (slanted ? slanted_penalty : 0);

        auto [itr, inserted] = indexed_penalty.emplace(meta.ref.path, penalty);
        if (not inserted) { itr->second = std::min(itr->second, penalty); }
      }

    for (const auto& kv : face_metadata_)
      {
        const fs::path p(kv.second.ref.path);
        const int penalty = indexed_penalty[kv.second.ref.path];

        consider(p.stem().string(), p, penalty);
        consider(kv.second.family_name, p, penalty);
      }

    // Files Blend2D refused, found by scanning the font directories the way
    // the Arabic and Latin lists already do. Ranking only what the Blend2D
    // index holds is what made a CFF2-only host report that no CJK font is
    // installed: the faces are there, they just never reached the list.
    constexpr std::size_t max_entries = 20000;
    std::size_t seen = 0;

    for (const auto& dir : system_font_directories())
      {
        std::error_code ec;
        if (not fs::is_directory(dir, ec)) { continue; }
        fs::recursive_directory_iterator it(
          dir, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; it != end and seen < max_entries; it.increment(ec))
          {
            if (ec) { break; }
            ++seen;
            const fs::path& p = it->path();
            if (not is_font_file(p)) { continue; }
            if (indexed_penalty.count(p.string()) > 0) { continue; }

            const std::string stem = p.stem().string();
            const int penalty =
              std::abs(stem_weight(stem) - 400) +
              (stem_is_italic(stem) ? slanted_penalty : 0) +
              unloadable_penalty;

            consider(stem, p, penalty);
          }
        if (seen >= max_entries) { break; }
      }

    for (const auto& p : ranked)
      {
        if (not p.empty()) { paths.push_back(p); }
      }
    return paths;
  }

  inline int blend2d_font_resolver::stem_weight(const std::string& stem)
  {
    std::string key;
    for (unsigned char c : stem)
      {
        if (std::isalnum(c)) { key += static_cast<char>(std::tolower(c)); }
      }

    // Longest spellings first: "extralight" also contains "light", and
    // "semibold"/"extrabold" also contain "bold".
    static const std::array<std::pair<const char*, int>, 16> weights = {{
      {"extralight", 200}, {"ultralight", 200}, {"semilight", 300},
      {"demilight", 350}, {"extrabold", 800}, {"ultrabold", 800},
      {"semibold", 600}, {"demibold", 600}, {"thin", 100}, {"light", 300},
      {"regular", 400}, {"normal", 400}, {"book", 400}, {"medium", 500},
      {"bold", 700}, {"black", 900},
    }};

    for (const auto& [name, weight] : weights)
      {
        if (key.find(name) != std::string::npos) { return weight; }
      }

    // "heavy" last: it is also a family name ("Heavy Data"), so it only wins
    // when nothing more specific matched.
    if (key.find("heavy") != std::string::npos) { return 900; }

    return 400;
  }

  inline bool blend2d_font_resolver::stem_is_italic(const std::string& stem)
  {
    std::string key;
    for (unsigned char c : stem)
      {
        if (std::isalnum(c)) { key += static_cast<char>(std::tolower(c)); }
      }

    return key.find("italic") != std::string::npos or
           key.find("oblique") != std::string::npos;
  }

  inline bool blend2d_font_resolver::is_font_file(const std::filesystem::path& p)
  {
    const std::string ext = normalize_font_name(p.extension().string());
    return ext == "ttf" or ext == "otf" or ext == "ttc";
  }

  inline void blend2d_font_resolver::scan_for_fallback_fonts(
                                                             std::vector<std::filesystem::path>& paths)
  {
    namespace fs = std::filesystem;

    // Ubiquitous faces, best first; match against lowercased filename stems.
    static const std::array<const char*, 10> preferred = {
      "liberationsans-regular", "dejavusans", "notosans-regular",
      "freesans", "arial", "helvetica", "cantarell-regular",
      "opensans-regular", "roboto-regular", "ubuntu-r",
    };

    constexpr std::size_t max_entries = 20000;
    std::size_t seen = 0;

    std::vector<fs::path> ranked(preferred.size());
    fs::path any_font;

    for (const auto& dir : system_font_directories())
      {
        std::error_code ec;
        if (not fs::is_directory(dir, ec)) { continue; }
        fs::recursive_directory_iterator it(
          dir, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; it != end and seen < max_entries; it.increment(ec))
          {
            if (ec) { break; }
            ++seen;
            const fs::path& p = it->path();
            if (not is_font_file(p)) { continue; }
            if (any_font.empty()) { any_font = p; }
            std::string stem = p.stem().string();
            std::transform(stem.begin(), stem.end(), stem.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            for (std::size_t i = 0; i < preferred.size(); i++)
              {
                if (ranked[i].empty() and stem == preferred[i]) { ranked[i] = p; }
              }
          }
        if (seen >= max_entries) { break; }
      }

    for (const auto& p : ranked)
      {
        if (not p.empty()) { paths.push_back(p); }
      }
    if (not any_font.empty()) { paths.push_back(any_font); }
  }

  inline std::string blend2d_font_resolver::bl_string_to_std(const BLString& s)
  {
    return std::string(s.data(), s.size());
  }

  inline std::string blend2d_font_resolver::font_ref_key(const font_face_ref& ref)
  {
    return ref.path + "#" + std::to_string(ref.face_index);
  }

  inline bool blend2d_font_resolver::has_prefix(const std::string& s,
                                                const std::string& prefix)
  {
    return s.size() >= prefix.size() and
      std::equal(prefix.begin(), prefix.end(), s.begin());
  }

  inline bool blend2d_font_resolver::contains_token(const std::vector<std::string>& toks,
                                                    const std::string& token)
  {
    return std::find(toks.begin(), toks.end(), token) != toks.end();
  }

  inline std::vector<std::string> blend2d_font_resolver::compatible_family_names(
                                                                                 const std::string& normalized_family)
  {
    std::vector<std::string> names = {normalized_family};

    auto append = [&names](const char* name)
    {
      const std::string norm = normalize_font_name(name);
      if (std::find(names.begin(), names.end(), norm) == names.end())
        {
          names.push_back(norm);
        }
    };

    if (normalized_family == "helvetica")
      {
        append("Arial");
        append("Liberation Sans");
        append("Nimbus Sans");
        append("Nimbus Sans L");
        append("DejaVu Sans");
      }
    else if (normalized_family == "times")
      {
        append("Times New Roman");
        append("Liberation Serif");
        append("Nimbus Roman");
        append("Nimbus Roman No9 L");
        append("DejaVu Serif");
      }
    else if (normalized_family == "courier")
      {
        append("Courier New");
        append("Liberation Mono");
        append("Nimbus Mono");
        append("Nimbus Mono PS");
        append("DejaVu Sans Mono");
      }
    else if (normalized_family == "symbol")
      {
        append("STIX");
        append("STIXGeneral");
        append("Standard Symbols L");
      }
    else if (normalized_family == "zapf dingbats")
      {
        append("Dingbats");
        append("Wingdings");
        append("D050000L");
      }
    else if (normalized_family == "latin modern math")
      {
        append("Latin Modern Math");
        append("STIX Math");
        append("STIXGeneral");
        append("Symbol");
      }
    else if (normalized_family == "latin modern roman")
      {
        append("Latin Modern Roman");
        append("Latin Modern");
        append("CMU Serif");
        append("STIXGeneral");
      }
    else if (normalized_family == "latin modern sans")
      {
        append("Latin Modern Sans");
        append("CMU Sans Serif");
        append("DejaVu Sans");
      }
    else if (normalized_family == "latin modern mono")
      {
        append("Latin Modern Mono");
        append("CMU Typewriter Text");
        append("DejaVu Sans Mono");
      }

    return names;
  }

  inline blend2d_font_resolver::font_request
  blend2d_font_resolver::parse_font_request(const std::string& name)
  {
    font_request request;
    request.original_name = strip_subset_prefix(name);
    request.normalized_name = normalize_font_name(request.original_name);

    const auto toks = split_tokens(request.normalized_name);
    request.bold =
      contains_token(toks, "bold") or contains_token(toks, "medi") or
      contains_token(toks, "semibold") or contains_token(toks, "demibold") or
      contains_token(toks, "black") or contains_token(toks, "heavy");
    request.italic =
      contains_token(toks, "italic") or contains_token(toks, "ital") or
      contains_token(toks, "oblique") or contains_token(toks, "obli");
    request.symbolic =
      contains_token(toks, "symbol") or contains_token(toks, "dingbats") or
      contains_token(toks, "cmsy") or contains_token(toks, "cmex");

    const auto sig = significant_tokens(toks);
    std::ostringstream family;
    for (size_t i = 0; i < sig.size(); ++i)
      {
        if (i > 0) { family << ' '; }
        family << sig[i];
      }
    request.family = family.str();
    if (request.family.empty()) { request.family = request.normalized_name; }

    lookup_alias(request);
    is_tex_font_request(request);

    return request;
  }

  inline bool blend2d_font_resolver::lookup_alias(font_request& request)
  {
    const std::string n = request.normalized_name;
    std::string compact;
    for (char c : n)
      {
        if (c != ' ') { compact += c; }
      }

    auto set_family = [&request](const std::string& family)
    {
      request.family = normalize_font_name(family);
    };
    auto set_standard_family = [&request](const std::string& family)
    {
      request.normalized_name = normalize_font_name(family);
      request.family = request.normalized_name;
      request.standard_14 = true;
    };

    if (compact == "arial" or compact == "arialbold" or
        compact == "arialitalic" or compact == "arialbolditalic")
      {
        set_standard_family("Helvetica");
        request.bold = request.bold or compact.find("bold") != std::string::npos;
        request.italic = request.italic or compact.find("italic") != std::string::npos;
        return true;
      }
    if (compact == "couriernew" or compact == "couriernewbold" or
        compact == "couriernewitalic" or compact == "couriernewbolditalic")
      {
        set_standard_family("Courier");
        request.bold = request.bold or compact.find("bold") != std::string::npos;
        request.italic = request.italic or compact.find("italic") != std::string::npos;
        return true;
      }
    if (compact == "timesnewroman" or compact == "timesnewromanbold" or
        compact == "timesnewromanitalic" or compact == "timesnewromanbolditalic")
      {
        set_standard_family("Times");
        request.bold = request.bold or compact.find("bold") != std::string::npos;
        request.italic = request.italic or compact.find("italic") != std::string::npos;
        return true;
      }

    if (n == "times" or n == "times roman")
      {
        set_family("Times");
        request.standard_14 = true;
        return true;
      }
    if (n == "times bold")
      {
        set_family("Times");
        request.bold = true;
        request.standard_14 = true;
        return true;
      }
    if (n == "times italic")
      {
        set_family("Times");
        request.italic = true;
        request.standard_14 = true;
        return true;
      }
    if (n == "times bold italic")
      {
        set_family("Times");
        request.bold = true;
        request.italic = true;
        request.standard_14 = true;
        return true;
      }

    if (n == "helvetica")
      {
        set_family("Helvetica");
        request.standard_14 = true;
        return true;
      }
    if (n == "helvetica bold")
      {
        set_family("Helvetica");
        request.bold = true;
        request.standard_14 = true;
        return true;
      }
    if (n == "helvetica oblique")
      {
        set_family("Helvetica");
        request.italic = true;
        request.standard_14 = true;
        return true;
      }
    if (n == "helvetica bold oblique")
      {
        set_family("Helvetica");
        request.bold = true;
        request.italic = true;
        request.standard_14 = true;
        return true;
      }

    if (n == "courier")
      {
        set_family("Courier");
        request.standard_14 = true;
        return true;
      }
    if (n == "courier bold")
      {
        set_family("Courier");
        request.bold = true;
        request.standard_14 = true;
        return true;
      }
    if (n == "courier oblique")
      {
        set_family("Courier");
        request.italic = true;
        request.standard_14 = true;
        return true;
      }
    if (n == "courier bold oblique")
      {
        set_family("Courier");
        request.bold = true;
        request.italic = true;
        request.standard_14 = true;
        return true;
      }

    if (n == "symbol")
      {
        set_family("Symbol");
        request.symbolic = true;
        request.standard_14 = true;
        return true;
      }
    if (n == "zapf dingbats" or n == "zapfdingbats")
      {
        set_family("Zapf Dingbats");
        request.symbolic = true;
        request.standard_14 = true;
        return true;
      }

    if (has_prefix(n, "nimbus rom no 9 l") or has_prefix(n, "nimbusromno9l"))
      {
        set_family("Times");
        request.bold = request.bold or n.find("medi") != std::string::npos;
        request.italic = request.italic or n.find("ital") != std::string::npos;
        return true;
      }
    if (has_prefix(n, "nimbus san l") or has_prefix(n, "nimbussanl"))
      {
        set_family("Helvetica");
        request.bold = request.bold or n.find("bold") != std::string::npos;
        request.italic = request.italic or n.find("obli") != std::string::npos;
        return true;
      }
    if (has_prefix(n, "nimbus mono ps") or has_prefix(n, "nimbusmonops"))
      {
        set_family("Courier");
        request.bold = request.bold or n.find("bold") != std::string::npos;
        request.italic = request.italic or n.find("obli") != std::string::npos;
        return true;
      }
    if (has_prefix(n, "urw gothic") or has_prefix(n, "urwgothic"))
      {
        set_family("Avant Garde");
        request.bold = request.bold or n.find("bold") != std::string::npos;
        request.italic = request.italic or n.find("obli") != std::string::npos;
        return true;
      }

    return false;
  }

  inline bool blend2d_font_resolver::is_tex_font_request(font_request& request)
  {
    const std::string compact = [&request]()
    {
      std::string out;
      for (char c : request.normalized_name)
        {
          if (c != ' ') { out += c; }
        }
      return out;
    }();

    if (has_prefix(compact, "cmr") or has_prefix(compact, "cmmib") or
        has_prefix(compact, "cmmi") or has_prefix(compact, "lmroman"))
      {
        request.family = normalize_font_name("Latin Modern Roman");
        return true;
      }
    if (has_prefix(compact, "cmss") or has_prefix(compact, "lmsans"))
      {
        request.family = normalize_font_name("Latin Modern Sans");
        return true;
      }
    if (has_prefix(compact, "cmtt") or has_prefix(compact, "lmmono"))
      {
        request.family = normalize_font_name("Latin Modern Mono");
        return true;
      }
    if (has_prefix(compact, "cmsy") or has_prefix(compact, "cmex"))
      {
        request.family = normalize_font_name("Latin Modern Math");
        request.symbolic = true;
        return true;
      }
    if (has_prefix(compact, "texgyre"))
      {
        request.family = normalize_font_name("TeX Gyre");
        return true;
      }

    return false;
  }

  inline void blend2d_font_resolver::build_font_index()
  {
    namespace fs = std::filesystem;
    const std::vector<fs::path> font_dirs = system_font_directories();
    fallback_candidates_ = fallback_font_candidates();
    arabic_candidates_ = arabic_fallback_candidates();

    LOG_S(INFO) << "blend2d font resolver: scanning font directories";
    for (const auto& dir : font_dirs)
      {
        LOG_S(INFO) << "blend2d font resolver: font directory: " << dir.string();
      }

    size_t discovery_order = 0;
    for (const auto& dir : font_dirs)
      {
        if (not fs::is_directory(dir))
          {
            LOG_S(INFO) << "blend2d font resolver: skipping missing font directory: "
                        << dir.string();
            continue;
          }

        std::error_code ec;
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        while (it != end)
          {
            if (ec)
              {
                LOG_S(WARNING) << "blend2d font resolver: directory iteration warning"
                               << " dir=`" << dir.string() << "`"
                               << " error=`" << ec.message() << "`";
                ec.clear();
              }

            const auto p = it->path();
            if (is_font_file(p))
              {
                index_font_file(p, discovery_order);
              }

            it.increment(ec);
          }
      }

    // Needs the finished index: the CJK faces are picked by family name.
    cjk_candidates_ = cjk_fallback_candidates();

    LOG_S(INFO) << "blend2d font resolver: indexed "
                << face_metadata_.size() << " font faces and "
                << name_index_.size() << " names";
  }

  inline void blend2d_font_resolver::index_font_file(const std::filesystem::path& path,
                                                     size_t& discovery_order)
  {
    BLFontData data;
    const BLResult data_res = data.create_from_file(
                                                    path.string().c_str(),
                                                    static_cast<BLFileReadFlags>(BL_FILE_READ_MMAP_ENABLED |
                                                                                 BL_FILE_READ_MMAP_AVOID_SMALL));
    if (data_res != BL_SUCCESS)
      {
        LOG_S(INFO) << "blend2d font resolver: failed to read font data"
                    << " path=`" << path.string() << "`"
                    << " data_res=" << data_res;
        return;
      }

    const uint32_t face_count = data.face_count();
    bool reported_rejection = false;
    for (uint32_t face_index = 0; face_index < face_count; ++face_index)
      {
        BLFontFace face;
        const BLResult face_res = face.create_from_data(data, face_index);
        if (face_res != BL_SUCCESS or not face.is_valid())
          {
            // Once per file, not once per face: a CJK collection holds five
            // and they all fail together. This used to be INFO, so the only
            // thing a user saw was "no installed face covers ..." -- which
            // points at the host's font set rather than at our loader, and
            // sent the investigation the wrong way for a day.
            if (not reported_rejection)
              {
                reported_rejection = true;
                LOG_S(WARNING) << "blend2d font resolver: Blend2D cannot read this font"
                               << " file; it will only be usable through FreeType"
                               << " (no shaping)."
                               << " path=`" << path.string() << "`"
                               << " face_res=" << face_res
                               << (face_res == BL_ERROR_FONT_CFF_INVALID_DATA
                                     ? " (CFF2/variable outlines -- install the"
                                       " static build of this font, e.g."
                                       " fonts-noto-cjk or"
                                       " google-noto-sans-cjk-fonts, or point"
                                       " DOCLING_PARSE_CJK_FALLBACK_FONT at one)"
                                     : "");
              }
            LOG_S(INFO) << "blend2d font resolver: failed to inspect font face"
                        << " path=`" << path.string() << "`"
                        << " face_index=" << face_index
                        << " face_res=" << face_res;
            continue;
          }

        indexed_font_face indexed;
        indexed.ref = {path.string(), face_index};
        indexed.family_name = bl_string_to_std(face.family_name());
        indexed.full_name = bl_string_to_std(face.full_name());
        indexed.subfamily_name = bl_string_to_std(face.subfamily_name());
        indexed.post_script_name = bl_string_to_std(face.post_script_name());
        indexed.weight = face.weight();
        indexed.style = face.style();
        indexed.discovery_order = discovery_order++;
        index_font_face(indexed);
      }
  }

  inline void blend2d_font_resolver::index_font_face(const indexed_font_face& face)
  {
    const std::string ref_key = font_ref_key(face.ref);
    if (face_metadata_.find(ref_key) != face_metadata_.end()) { return; }
    face_metadata_.emplace(ref_key, face);

    auto add_name = [this, &face](const std::string& name)
    {
      const std::string norm = normalize_font_name(name);
      if (norm.empty()) { return; }
      auto& refs = name_index_[norm];
      if (std::find(refs.begin(), refs.end(), face.ref) == refs.end())
        {
          refs.push_back(face.ref);
        }
    };

    add_name(face.family_name);
    add_name(face.full_name);
    add_name(face.subfamily_name);
    add_name(face.post_script_name);
    if (not face.family_name.empty() and not face.subfamily_name.empty())
      {
        add_name(face.family_name + " " + face.subfamily_name);
        add_name(face.family_name + "-" + face.subfamily_name);
      }

    LOG_S(INFO) << "blend2d font resolver: indexed font face"
                << " family=`" << face.family_name << "`"
                << " full=`" << face.full_name << "`"
                << " post_script=`" << face.post_script_name << "`"
                << " path=`" << face.ref.path << "`"
                << " face_index=" << face.ref.face_index
                << " weight=" << face.weight
                << " style=" << face.style;
  }

  inline std::optional<blend2d_font_resolver::font_face_ref>
  blend2d_font_resolver::resolve_font_ref(const std::string& cache_key,
                                          float font_similarity_cutoff)
  {
    warm();

    const font_request request = parse_font_request(cache_key);
    const match_cache_key match_key{
      request.normalized_name + "|" + request.family + "|" +
        (request.bold ? "b" : "r") + (request.italic ? "i" : "n") +
        (request.symbolic ? "s" : "t"),
      quantized_cutoff(font_similarity_cutoff)
    };

    {
      std::shared_lock lock(match_cache_mutex_);
      auto itr = match_cache_.find(match_key);
      if (itr != match_cache_.end())
        {
          LOG_S(INFO) << "blend2d font resolver: match cache hit"
                      << " query=`" << match_key.normalized_query << "`"
                      << " cutoff_x10000=" << match_key.cutoff_x10000
                      << " path=`" << (itr->second.has_value() ? itr->second->path : std::string())
                      << "`"
                      << " face_index=" << (itr->second.has_value() ? itr->second->face_index : 0);
          return itr->second;
        }
    }

    LOG_S(INFO) << "blend2d font resolver: match cache miss"
                << " query=`" << match_key.normalized_query << "`"
                << " cutoff_x10000=" << match_key.cutoff_x10000;

    std::optional<font_face_ref> found_ref = exact_find_font(request);

    if (not found_ref.has_value() and request.symbolic)
      {
        for (const auto& fallback_family : {"Latin Modern Math", "STIX", "Symbol"})
          {
            font_request fallback_request = request;
            fallback_request.family = normalize_font_name(fallback_family);
            found_ref = exact_find_font(fallback_request);
            if (found_ref.has_value()) { break; }
          }
      }

    if (not found_ref.has_value() and not request.standard_14)
      {
        found_ref = fuzzy_find_font(request, font_similarity_cutoff);
        LOG_S(INFO) << "blend2d font resolver: fuzzy font match result"
                    << " query=`" << request.normalized_name << "`"
                    << " path=`" << (found_ref.has_value() ? found_ref->path : std::string())
                    << "`";
      }

    {
      std::unique_lock lock(match_cache_mutex_);
      auto [itr, inserted] = match_cache_.emplace(match_key, found_ref);
      return itr->second;
    }
  }

  inline std::optional<blend2d_font_resolver::font_face_ref>
  blend2d_font_resolver::exact_find_font(const font_request& request) const
  {
    std::vector<std::string> names = {request.normalized_name};
    const auto compatible = compatible_family_names(request.family);
    names.insert(names.end(), compatible.begin(), compatible.end());

    for (const auto& name : names)
      {
        if (name.empty()) { continue; }
        auto itr = name_index_.find(name);
        if (itr == name_index_.end()) { continue; }
        auto selected = find_by_family_candidates(itr->second, request);
        if (selected.has_value())
          {
            LOG_S(INFO) << "blend2d font resolver: exact font match"
                        << " query=`" << request.normalized_name << "`"
                        << " family=`" << request.family << "`"
                        << " path=`" << selected->path << "`"
                        << " face_index=" << selected->face_index;
            return selected;
          }
      }

    return std::nullopt;
  }

  inline std::optional<blend2d_font_resolver::font_face_ref>
  blend2d_font_resolver::find_by_family_candidates(
                                                   const std::vector<font_face_ref>& refs,
                                                   const font_request& request) const
  {
    std::optional<font_face_ref> best;
    int best_score = INT_MIN;
    size_t best_order = SIZE_MAX;

    for (const auto& ref : refs)
      {
        auto meta_itr = face_metadata_.find(font_ref_key(ref));
        if (meta_itr == face_metadata_.end()) { continue; }
        const indexed_font_face& face = meta_itr->second;

        const bool face_bold = face.weight >= BL_FONT_WEIGHT_SEMI_BOLD;
        const bool face_italic = face.style == BL_FONT_STYLE_ITALIC or
          face.style == BL_FONT_STYLE_OBLIQUE;

        int score = 0;
        score += (face_italic == request.italic) ? 1000 : -1000;
        score += (face_bold == request.bold) ? 500 : -250;
        score -= std::abs(static_cast<int>(face.weight) -
                          static_cast<int>(request.bold ? BL_FONT_WEIGHT_BOLD
                                                        : BL_FONT_WEIGHT_NORMAL));
        if (normalize_font_name(face.family_name) == request.family) { score += 200; }
        if (normalize_font_name(face.post_script_name) == request.normalized_name) { score += 300; }
        if (normalize_font_name(face.full_name) == request.normalized_name) { score += 250; }

        if (score > best_score or
            (score == best_score and face.discovery_order < best_order))
          {
            best_score = score;
            best_order = face.discovery_order;
            best = ref;
          }
      }

    return best;
  }

  inline blend2d_font_resolver::font_face_ref
  blend2d_font_resolver::indexed_ref_for_path(const std::string& path) const
  {
    // The lowest indexed face, not the first one the hash map happens to
    // reach: a collection carries one face per region (Noto CJK is JP, KR,
    // SC, TC, HK) and picking a different one per process is a rendering
    // difference nobody asked for.
    font_face_ref ref{path, 0};
    bool found = false;

    for (const auto& [key, face] : face_metadata_)
      {
        if (face.ref.path != path) { continue; }
        if (not found or face.ref.face_index < ref.face_index)
          {
            ref = face.ref;
            found = true;
          }
      }

    return ref;
  }

  inline blend2d_font_resolver::resolved_face
  blend2d_font_resolver::find_first_loadable_fallback(
    const std::vector<std::filesystem::path>& candidates,
    const char* kind)
  {
    namespace fs = std::filesystem;

    for (const auto& fallback : candidates)
      {
        std::error_code ec;
        if (not fs::exists(fallback, ec)) { continue; }

        resolved_face resolved =
          load_resolved_face(indexed_ref_for_path(fallback.string()));
        if (resolved.is_drawable()) { return resolved; }

        LOG_S(INFO) << "blend2d font resolver: " << kind
                    << " fallback candidate cannot be opened by any backend,"
                    << " trying the next one"
                    << " path=`" << fallback.string() << "`";
      }

    return {};
  }

  inline std::optional<blend2d_font_resolver::font_face_ref>
  blend2d_font_resolver::fuzzy_find_font(const font_request& request,
                                         float font_similarity_cutoff) const
  {
    const auto q_toks = split_tokens(request.family);
    if (q_toks.empty()) { return std::nullopt; }

    std::optional<font_face_ref> best_ref;
    float best_jaccard = 0.0f;
    int best_size_delta = INT_MAX;

    for (const auto& [norm_name, refs] : name_index_)
      {
        const auto c_toks = split_tokens(norm_name);
        const auto c_sig_toks = significant_tokens(c_toks);
        if (c_sig_toks.empty()) { continue; }

        int score = 0;
        for (const auto& tok : q_toks)
          {
            if (std::find(c_sig_toks.begin(), c_sig_toks.end(), tok) != c_sig_toks.end())
              {
                ++score;
              }
          }

        if (score == 0) { continue; }

        const float jaccard = static_cast<float>(score) /
          static_cast<float>(q_toks.size() + c_sig_toks.size() - score);
        if (jaccard < font_similarity_cutoff) { continue; }

        font_request candidate_request = request;
        candidate_request.family = norm_name;
        const auto selected = find_by_family_candidates(refs, candidate_request);
        if (not selected.has_value()) { continue; }

        const int delta = std::abs(static_cast<int>(c_sig_toks.size()) -
                                   static_cast<int>(q_toks.size()));
        if (jaccard > best_jaccard or
            (jaccard == best_jaccard and delta < best_size_delta))
          {
            best_jaccard = jaccard;
            best_size_delta = delta;
            best_ref = selected;
          }
      }

    LOG_S(INFO) << "blend2d font resolver: fuzzy_find_font"
                << " query=`" << request.normalized_name << "`"
                << " family=`" << request.family << "`"
                << " best_jaccard=" << best_jaccard
                << " best_size_delta=" << best_size_delta
                << " path=`" << (best_ref.has_value() ? best_ref->path : std::string())
                << "`";
    return best_ref;
  }

  inline BLFontFace blend2d_font_resolver::load_font_face(const font_face_ref& ref)
  {
    {
      std::shared_lock lock(face_cache_mutex_);
      auto itr = face_cache_.find(ref);
      if (itr != face_cache_.end())
        {
          LOG_S(INFO) << "blend2d font resolver: face cache hit"
                      << " path=`" << ref.path << "`"
                      << " face_index=" << ref.face_index
                      << " valid=" << (itr->second.is_valid() ? "true" : "false");
          return itr->second;
        }
    }

    BLFontData data;
    BLFontFace face;
    const BLResult data_res = data.create_from_file(
                                                    ref.path.c_str(),
                                                    static_cast<BLFileReadFlags>(BL_FILE_READ_MMAP_ENABLED |
                                                                                 BL_FILE_READ_MMAP_AVOID_SMALL));
    BLResult face_res = BL_ERROR_INVALID_VALUE;
    if (data_res == BL_SUCCESS)
      {
        face_res = face.create_from_data(data, ref.face_index);
      }
    LOG_S(INFO) << "blend2d font resolver: loaded font face"
                << " path=`" << ref.path << "`"
                << " face_index=" << ref.face_index
                << " data_res=" << data_res
                << " face_res=" << face_res
                << " valid=" << (face.is_valid() ? "true" : "false");

    {
      std::unique_lock lock(face_cache_mutex_);
      auto [itr, inserted] = face_cache_.emplace(ref, face);
      return itr->second;
    }
  }

  inline blend2d_font_resolver::resolved_face
  blend2d_font_resolver::load_resolved_face(const font_face_ref& ref)
  {
    resolved_face resolved;
    resolved.path = ref.path;
    resolved.face_index = ref.face_index;
    resolved.face = load_font_face(ref);

    if (not resolved.face.is_valid())
      {
        // Blend2D refused the file. Keep it only if the other backend can
        // read it; an unreadable path is worse than none, because the caller
        // would stop looking at the candidates behind it.
        if (freetype_cache_ == nullptr or
            not freetype_cache_->can_open_file(ref.path, ref.face_index))
          {
            resolved.path.clear();
          }
      }

    return resolved;
  }

  inline std::size_t blend2d_font_resolver::candidate_coverage(
    const resolved_face& candidate,
    const std::string& utf8_text,
    std::size_t& total)
  {
    if (candidate.face.is_valid())
      {
        BLFontFace face = candidate.face;
        return face_coverage(face, utf8_text, total);
      }

    total = 0;
    if (candidate.path.empty() or freetype_cache_ == nullptr) { return 0; }

    return freetype_cache_->file_face_coverage(candidate.path,
                                               candidate.face_index,
                                               utf8_text, total);
  }
}

#endif
