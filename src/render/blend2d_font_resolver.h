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
    // system fonts using exact and fuzzy matching. If lookup fails, or font
    // resolving is disabled, the method falls back to a known system font.
    // Returns an invalid BLFontFace when no fallback font can be loaded.
    BLFontFace resolve_font_face(const std::string& font_name,
                                 const std::string& base_font,
                                 bool resolve_fonts,
                                 float font_similarity_cutoff);

  private:

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

    // Normalizes a PDF/system font name into the comparable form used by the
    // resolver index. This strips PDF subset prefixes, replaces hyphens with
    // spaces, splits camel-case family/style names, lowercases ASCII text, and
    // removes common PostScript suffixes such as "PSMT".
    static std::string normalize_font_name(const std::string& name);

    // Splits a normalized font name on whitespace while preserving token order.
    static std::vector<std::string> split_tokens(const std::string& s);

    // Returns true for weight/style descriptors that should not participate
    // in family identity matching. These tokens can still affect fuzzy score,
    // but are ignored when deciding whether two font names are comparable.
    static bool is_style_token(const std::string& tok);

    // Returns only non-style tokens from a tokenized font name. This is used
    // to require family-level equality before accepting a fuzzy style match.
    static std::vector<std::string> significant_tokens(const std::vector<std::string>& toks);

    // Converts a floating similarity cutoff into a stable integer cache key,
    // avoiding direct float keys in unordered_map.
    static int quantized_cutoff(float cutoff);

    // Returns the first known fallback font path that exists on this system.
    // The resolver currently targets macOS font locations used by the rest of
    // this Blend2D renderer path.
    static std::optional<std::string> fallback_font_path();

    // Scans known font directories and builds a normalized-name to file-path
    // index. The first file for a normalized name wins, which keeps matching
    // deterministic across later duplicate family/style files.
    void build_font_index();

    // Resolves a selected PDF font cache key to a system font path. This method
    // handles warming, exact lookup, fuzzy lookup, and match-result caching.
    std::optional<std::string> resolve_font_path(const std::string& cache_key,
                                                 float font_similarity_cutoff);

    // Finds the best indexed font path for a normalized query. Candidates must
    // have the same significant family tokens; among those, the method scores
    // ordered token overlap with a Jaccard-like ratio and prefers the highest
    // score, breaking ties by the smallest token-count delta.
    std::optional<std::string> fuzzy_find_font(const std::string& norm_query,
                                               float font_similarity_cutoff) const;

    // Loads a Blend2D font face from disk and caches the result by file path.
    // Invalid load attempts are cached too, preventing repeated filesystem and
    // Blend2D work for paths that cannot produce a usable face.
    BLFontFace load_font_face(const std::string& path);

    std::once_flag index_once_;
    std::unordered_map<std::string, std::string> font_index_;

    mutable std::shared_mutex match_cache_mutex_;
    std::unordered_map<match_cache_key,
                       std::optional<std::string>,
                       match_cache_key_hash> match_cache_;

    mutable std::shared_mutex face_cache_mutex_;
    std::unordered_map<std::string, BLFontFace> face_cache_;
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

    std::optional<std::string> font_path;
    if (resolve_fonts)
      {
        font_path = resolve_font_path(cache_key, font_similarity_cutoff);
      }

    if (not font_path.has_value() or font_path->empty())
      {
        LOG_S(INFO) << "blend2d font resolver: using fallback font"
                    << " selected_key=`" << cache_key << "`";
        font_path = fallback_font_path();
      }

    if (not font_path.has_value() or font_path->empty())
      {
        LOG_S(INFO) << "blend2d font resolver: no font path available"
                    << " selected_key=`" << cache_key << "`";
        return {};
      }

    LOG_S(INFO) << "blend2d font resolver: loading resolved font"
                << " selected_key=`" << cache_key << "`"
                << " path=`" << *font_path << "`";
    return load_font_face(*font_path);
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

  inline std::string blend2d_font_resolver::normalize_font_name(const std::string& name)
  {
    std::string s = name;
    if (not s.empty() and s[0] == '/') { s = s.substr(1); }

    if (s.size() > 7 and s[6] == '+' and
        std::all_of(s.begin(), s.begin() + 6,
                    [](char c){ return std::isupper(static_cast<unsigned char>(c)); }))
      {
        s = s.substr(7);
      }

    std::replace(s.begin(), s.end(), '-', ' ');

    std::string expanded;
    for (size_t i = 0; i < s.size(); ++i)
      {
        if (i > 0
            and std::isupper(static_cast<unsigned char>(s[i]))
            and std::islower(static_cast<unsigned char>(s[i - 1])))
          {
            expanded += ' ';
          }
        expanded += static_cast<char>(
                                      std::tolower(static_cast<unsigned char>(s[i])));
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

    while (not expanded.empty() and expanded.back() == ' ')
      {
        expanded.pop_back();
      }

    return expanded;
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
    static const std::array<const char*, 11> kStyleTokens = {
      "regular", "normal", "roman", "book", "medium",
      "bold", "italic", "oblique", "light", "thin", "black"
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

  inline std::optional<std::string> blend2d_font_resolver::fallback_font_path()
  {
    namespace fs = std::filesystem;
    for (const auto& fallback : {
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
      })
      {
        if (fs::exists(fallback))
          {
            return std::string(fallback);
          }
      }

    return std::nullopt;
  }

  inline void blend2d_font_resolver::build_font_index()
  {
    namespace fs = std::filesystem;
    const std::vector<std::string> font_dirs = {
      "/System/Library/Fonts",
      "/System/Library/Fonts/Supplemental",
      "/Library/Fonts",
    };

    LOG_S(INFO) << "blend2d font resolver: scanning font directories";
    for (const auto& dir : font_dirs)
      {
        LOG_S(INFO) << "blend2d font resolver: font directory: " << dir;
      }

    for (const auto& dir : font_dirs)
      {
        if (not fs::is_directory(dir))
          {
            LOG_S(INFO) << "blend2d font resolver: skipping missing font directory: " << dir;
            continue;
          }

        for (const auto& entry : fs::directory_iterator(dir))
          {
            const auto& p = entry.path();
            const std::string ext = p.extension().string();
            if (ext != ".ttf" and ext != ".otf" and ext != ".ttc") { continue; }

            const std::string norm = normalize_font_name(p.stem().string());
            if (font_index_.find(norm) == font_index_.end())
              {
                font_index_[norm] = p.string();
                LOG_S(INFO) << "blend2d font resolver: indexed font: "
                            << norm << " -> " << p.string();
              }
          }
      }

    LOG_S(INFO) << "blend2d font resolver: indexed "
                << font_index_.size() << " fonts";
  }

  inline std::optional<std::string> blend2d_font_resolver::resolve_font_path(
                                                                             const std::string& cache_key,
                                                                             float font_similarity_cutoff)
  {
    warm();

    const match_cache_key match_key{
      normalize_font_name(cache_key),
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
                      << " path=`" << (itr->second.has_value() ? *itr->second : std::string())
                      << "`";
          return itr->second;
        }
    }

    LOG_S(INFO) << "blend2d font resolver: match cache miss"
                << " query=`" << match_key.normalized_query << "`"
                << " cutoff_x10000=" << match_key.cutoff_x10000;

    std::optional<std::string> found_path;

    auto exact = font_index_.find(match_key.normalized_query);
    if (exact != font_index_.end())
      {
        found_path = exact->second;
        LOG_S(INFO) << "blend2d font resolver: exact font match"
                    << " query=`" << match_key.normalized_query << "`"
                    << " path=`" << *found_path << "`";
      }
    else
      {
        found_path = fuzzy_find_font(match_key.normalized_query,
                                     font_similarity_cutoff);
        LOG_S(INFO) << "blend2d font resolver: fuzzy font match result"
                    << " query=`" << match_key.normalized_query << "`"
                    << " path=`" << (found_path.has_value() ? *found_path : std::string())
                    << "`";
      }

    {
      std::unique_lock lock(match_cache_mutex_);
      auto [itr, inserted] = match_cache_.emplace(match_key, found_path);
      return itr->second;
    }
  }

  inline std::optional<std::string> blend2d_font_resolver::fuzzy_find_font(
                                                                           const std::string& norm_query,
                                                                           float font_similarity_cutoff) const
  {
    const auto q_toks = split_tokens(norm_query);
    if (q_toks.empty()) { return std::nullopt; }

    const auto q_sig_toks = significant_tokens(q_toks);
    if (q_sig_toks.empty()) { return std::nullopt; }

    std::optional<std::string> best_path;
    float best_jaccard = 0.0f;
    int best_size_delta = INT_MAX;

    for (const auto& [norm_name, path] : font_index_)
      {
        const auto c_toks = split_tokens(norm_name);
        const auto c_sig_toks = significant_tokens(c_toks);
        if (c_sig_toks != q_sig_toks)
          {
            LOG_S(INFO) << "blend2d font resolver: fuzzy candidate"
                        << " query=`" << norm_query << "`"
                        << " candidate=`" << norm_name << "`"
                        << " path=`" << path << "`"
                        << " rejected=significant_tokens_mismatch";
            continue;
          }

        int score = 0;
        const auto max_tokens = std::min(q_toks.size(), c_toks.size());
        for (size_t i = 0; i < max_tokens; ++i)
          {
            if (q_toks[i] == c_toks[i])
              {
                ++score;
              }
          }

        if (score == 0)
          {
            LOG_S(INFO) << "blend2d font resolver: fuzzy candidate"
                        << " query=`" << norm_query << "`"
                        << " candidate=`" << norm_name << "`"
                        << " path=`" << path << "`"
                        << " rejected=no_ordered_token_overlap";
            continue;
          }

        const float jaccard = static_cast<float>(score) /
          static_cast<float>(q_toks.size() + c_toks.size() - score);
        if (jaccard < font_similarity_cutoff)
          {
            LOG_S(INFO) << "blend2d font resolver: fuzzy candidate"
                        << " query=`" << norm_query << "`"
                        << " candidate=`" << norm_name << "`"
                        << " path=`" << path << "`"
                        << " score=" << score
                        << " jaccard=" << jaccard
                        << " cutoff=" << font_similarity_cutoff
                        << " rejected=below_cutoff";
            continue;
          }

        const int delta = std::abs(static_cast<int>(c_toks.size()) -
                                   static_cast<int>(q_toks.size()));
        LOG_S(INFO) << "blend2d font resolver: fuzzy candidate"
                    << " query=`" << norm_query << "`"
                    << " candidate=`" << norm_name << "`"
                    << " path=`" << path << "`"
                    << " score=" << score
                    << " jaccard=" << jaccard
                    << " token_delta=" << delta
                    << " accepted=true";
        if (jaccard > best_jaccard or
            (jaccard == best_jaccard and delta < best_size_delta))
          {
            best_jaccard = jaccard;
            best_size_delta = delta;
            best_path = path;
          }
      }

    LOG_S(INFO) << "blend2d font resolver: fuzzy_find_font"
                << " query=`" << norm_query << "`"
                << " best_jaccard=" << best_jaccard
                << " best_size_delta=" << best_size_delta
                << " path=`" << (best_path.has_value() ? *best_path : std::string())
                << "`";
    return best_path;
  }

  inline BLFontFace blend2d_font_resolver::load_font_face(const std::string& path)
  {
    {
      std::shared_lock lock(face_cache_mutex_);
      auto itr = face_cache_.find(path);
      if (itr != face_cache_.end())
        {
          LOG_S(INFO) << "blend2d font resolver: face cache hit"
                      << " path=`" << path << "`"
                      << " valid=" << (itr->second.is_valid() ? "true" : "false");
          return itr->second;
        }
    }

    BLFontData data;
    BLFontFace face;
    const BLResult data_res = data.create_from_file(
                                                    path.c_str(),
                                                    static_cast<BLFileReadFlags>(BL_FILE_READ_MMAP_ENABLED |
                                                                                 BL_FILE_READ_MMAP_AVOID_SMALL));
    BLResult face_res = BL_ERROR_INVALID_VALUE;
    if (data_res == BL_SUCCESS)
      {
        face_res = face.create_from_data(data, 0);
      }
    LOG_S(INFO) << "blend2d font resolver: loaded font face"
                << " path=`" << path << "`"
                << " data_res=" << data_res
                << " face_res=" << face_res
                << " valid=" << (face.is_valid() ? "true" : "false");

    {
      std::unique_lock lock(face_cache_mutex_);
      auto [itr, inserted] = face_cache_.emplace(path, face);
      return itr->second;
    }
  }
}

#endif
