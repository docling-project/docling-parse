//-*-C++-*-

#ifndef PDF_BLEND2D_FONT_RESOLVER_H
#define PDF_BLEND2D_FONT_RESOLVER_H

#include <blend2d/blend2d.h>

#ifndef LOGURU_WITH_STREAMS
#define LOGURU_WITH_STREAMS 1
#endif
#include <loguru.hpp>

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

    // Resolves the PDF font identifiers to a Blend2D font face. font_name is
    // preferred unless it is empty or "null", otherwise base_font is used.
    // When resolve_fonts is true the selected name is matched against indexed
    // system fonts using deterministic aliases, exact metadata matches, style
    // selection, and finally fuzzy matching. If lookup fails, or resolving is
    // disabled, the method falls back to a known system font.
    // Returns an invalid BLFontFace when no fallback font can be loaded.
    BLFontFace resolve_font_face(const std::string& font_name,
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

    static std::optional<std::string> getenv_string(const char* name);
    static void append_env_path(std::vector<std::filesystem::path>& paths,
                                const char* env_name,
                                const std::filesystem::path& suffix = {});
    static std::vector<std::filesystem::path> system_font_directories();
    static std::vector<std::filesystem::path> fallback_font_candidates();

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
    std::optional<font_face_ref> find_first_existing_fallback() const;
    std::optional<font_face_ref> fuzzy_find_font(const font_request& request,
                                                 float font_similarity_cutoff) const;

    BLFontFace load_font_face(const font_face_ref& ref);

    std::once_flag index_once_;
    std::unordered_map<std::string, std::vector<font_face_ref>> name_index_;
    std::unordered_map<std::string, indexed_font_face> face_metadata_;
    std::vector<std::filesystem::path> fallback_candidates_;

    mutable std::shared_mutex match_cache_mutex_;
    std::unordered_map<match_cache_key,
                       std::optional<font_face_ref>,
                       match_cache_key_hash> match_cache_;

    mutable std::shared_mutex face_cache_mutex_;
    std::unordered_map<font_face_ref, BLFontFace, font_face_ref_hash> face_cache_;
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
    const std::string& cache_key = (not font_name.empty() and font_name != "null")
      ? font_name : base_font;

    LOG_S(INFO) << "blend2d font resolver: resolve_font_face"
                << " font_name=`" << font_name << "`"
                << " base_font=`" << base_font << "`"
                << " selected_key=`" << cache_key << "`"
                << " resolve_fonts=" << (resolve_fonts ? "true" : "false")
                << " similarity_cutoff=" << font_similarity_cutoff;

    std::optional<font_face_ref> font_ref;
    if (resolve_fonts)
      {
        font_ref = resolve_font_ref(cache_key, font_similarity_cutoff);
      }

    if (not font_ref.has_value() or font_ref->path.empty())
      {
        warm();
        LOG_S(WARNING) << "blend2d font resolver: using fallback font"
                       << " selected_key=`" << cache_key << "`";
        font_ref = find_first_existing_fallback();
      }

    if (not font_ref.has_value() or font_ref->path.empty())
      {
        LOG_S(INFO) << "blend2d font resolver: no font path available"
                    << " selected_key=`" << cache_key << "`";
        return {};
      }

    LOG_S(WARNING) << "blend2d font resolver: loading resolved font"
                   << " selected_key=`" << cache_key << "`"
                   << " path=`" << font_ref->path << "`"
                   << " face_index=" << font_ref->face_index;
    return load_font_face(*font_ref);
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

    for (const auto& suf : {" psmt", " ps", " mt"})
      {
        const std::string sfx(suf);
        if (expanded.size() >= sfx.size() and
            expanded.compare(expanded.size() - sfx.size(),
                             sfx.size(), sfx) == 0)
          {
            expanded.resize(expanded.size() - sfx.size());
            break;
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

    return dirs;
  }

  inline std::vector<std::filesystem::path> blend2d_font_resolver::fallback_font_candidates()
  {
    std::vector<std::filesystem::path> paths;

#if defined(_WIN32)
    append_env_path(paths, "WINDIR", std::filesystem::path("Fonts") / "arial.ttf");
    append_env_path(paths, "WINDIR", std::filesystem::path("Fonts") / "times.ttf");
    append_env_path(paths, "WINDIR", std::filesystem::path("Fonts") / "cour.ttf");
#elif defined(__APPLE__)
    paths.emplace_back("/System/Library/Fonts/Helvetica.ttc");
    paths.emplace_back("/System/Library/Fonts/Supplemental/Arial.ttf");
    paths.emplace_back("/Library/Fonts/Arial.ttf");
#else
    paths.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    paths.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf");
    paths.emplace_back("/usr/share/fonts/truetype/freefont/FreeSans.ttf");
#endif

    return paths;
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
    auto set_family = [&request](const std::string& family)
    {
      request.family = normalize_font_name(family);
    };

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
            const std::string ext = normalize_font_name(p.extension().string());
            if (ext == "ttf" or ext == "otf" or ext == "ttc")
              {
                index_font_file(p, discovery_order);
              }

            it.increment(ec);
          }
      }

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
    for (uint32_t face_index = 0; face_index < face_count; ++face_index)
      {
        BLFontFace face;
        const BLResult face_res = face.create_from_data(data, face_index);
        if (face_res != BL_SUCCESS or not face.is_valid())
          {
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

  inline std::optional<blend2d_font_resolver::font_face_ref>
  blend2d_font_resolver::find_first_existing_fallback() const
  {
    namespace fs = std::filesystem;

    for (const auto& fallback : fallback_candidates_)
      {
        if (fs::exists(fallback))
          {
            const std::string norm_path = fallback.string();
            for (const auto& [key, face] : face_metadata_)
              {
                if (face.ref.path == norm_path)
                  {
                    return face.ref;
                  }
              }
            return font_face_ref{norm_path, 0};
          }
      }

    return std::nullopt;
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
}

#endif
