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
    blend2d_font_resolver();

    static std::shared_ptr<blend2d_font_resolver> default_resolver();

    void warm();

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

    static std::string normalize_font_name(const std::string& name);
    static std::vector<std::string> split_tokens(const std::string& s);
    static bool is_style_token(const std::string& tok);
    static std::vector<std::string> significant_tokens(const std::vector<std::string>& toks);
    static bool vectors_equal(const std::vector<std::string>& lhs,
                              const std::vector<std::string>& rhs);
    static int quantized_cutoff(float cutoff);
    static std::optional<std::string> fallback_font_path();

    void build_font_index();
    std::optional<std::string> resolve_font_path(const std::string& cache_key,
                                                 float font_similarity_cutoff);
    std::optional<std::string> fuzzy_find_font(const std::string& norm_query,
                                               float font_similarity_cutoff) const;
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

    std::optional<std::string> font_path;
    if (resolve_fonts)
      {
        font_path = resolve_font_path(cache_key, font_similarity_cutoff);
      }

    if (not font_path.has_value() or font_path->empty())
      {
        font_path = fallback_font_path();
      }

    if (not font_path.has_value() or font_path->empty())
      {
        return {};
      }

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

  inline bool blend2d_font_resolver::vectors_equal(const std::vector<std::string>& lhs,
                                                   const std::vector<std::string>& rhs)
  {
    if (lhs.size() != rhs.size()) { return false; }
    for (size_t i = 0; i < lhs.size(); ++i)
      {
        if (lhs[i] != rhs[i])
          {
            return false;
          }
      }
    return true;
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
          return itr->second;
        }
    }

    std::optional<std::string> found_path;

    auto exact = font_index_.find(match_key.normalized_query);
    if (exact != font_index_.end())
      {
        found_path = exact->second;
      }
    else
      {
        found_path = fuzzy_find_font(match_key.normalized_query,
                                     font_similarity_cutoff);
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
        if (not vectors_equal(c_sig_toks, q_sig_toks)) { continue; }

        int score = 0;
        const auto max_tokens = std::min(q_toks.size(), c_toks.size());
        for (size_t i = 0; i < max_tokens; ++i)
          {
            if (q_toks[i] == c_toks[i])
              {
                ++score;
              }
          }

        if (score == 0) { continue; }

        const float jaccard = static_cast<float>(score) /
          static_cast<float>(q_toks.size() + c_toks.size() - score);
        if (jaccard < font_similarity_cutoff) { continue; }

        const int delta = std::abs(static_cast<int>(c_toks.size()) -
                                   static_cast<int>(q_toks.size()));
        if (jaccard > best_jaccard or
            (jaccard == best_jaccard and delta < best_size_delta))
          {
            best_jaccard = jaccard;
            best_size_delta = delta;
            best_path = path;
          }
      }

    return best_path;
  }

  inline BLFontFace blend2d_font_resolver::load_font_face(const std::string& path)
  {
    {
      std::shared_lock lock(face_cache_mutex_);
      auto itr = face_cache_.find(path);
      if (itr != face_cache_.end())
        {
          return itr->second;
        }
    }

    BLFontData data;
    BLFontFace face;
    const BLResult data_res = data.create_from_file(
                                                    path.c_str(),
                                                    static_cast<BLFileReadFlags>(BL_FILE_READ_MMAP_ENABLED |
                                                                                 BL_FILE_READ_MMAP_AVOID_SMALL));
    if (data_res == BL_SUCCESS)
      {
        face.create_from_data(data, 0);
      }

    {
      std::unique_lock lock(face_cache_mutex_);
      auto [itr, inserted] = face_cache_.emplace(path, face);
      return itr->second;
    }
  }
}

#endif
