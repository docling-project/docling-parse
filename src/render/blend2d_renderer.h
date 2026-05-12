//-*-C++-*-

#ifndef PDF_BLEND2D_RENDERER_H
#define PDF_BLEND2D_RENDERER_H

#include <render/template_renderer.h>
#include <render/config.h>

#include <blend2d/blend2d.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace pdflib
{
  template<>
  class renderer<BLEND2D>
  {
  public:

    renderer();
    explicit renderer(render_config config);

    void set_size(size_instruction& instr);
    void render_text(text_instruction& instr);
    void render_text_legacy_v2(text_instruction& instr);
    void render_text_legacy(text_instruction& instr);
    void render_widget(text_widget_instruction& instr);
    void render_bitmap(bitmap_instruction& instr);
    void render_shape(shape_instruction& instr);

    // Returns the rendered canvas as RGBA bytes, row-major top-to-bottom.
    // The associated shape is {height, width, 4}.
    std::shared_ptr<std::vector<uint8_t>> get_canvas() const;
    const std::array<int, 3>& get_shape() const { return shape_; }

    // Save the canvas to a file.  The format is inferred from the extension
    // (e.g. ".png", ".bmp").  PNG is recommended; it is built into Blend2D.
    void save(const std::string& path) const;

    // Save the canvas to a temporary PNG and open it with the OS default
    // image viewer (like PIL's Image.show()).
    void show() const;

  private:

    struct bitmap_quad
    {
      double x0, y0; // bottom-left
      double x1, y1; // top-left
      double x2, y2; // top-right
      double x3, y3; // bottom-right
    };

    render_config config_;

    mutable BLImage    image_;  // internal canvas (PRGB32 format)
    std::array<int, 3> shape_;  // {height, width, 4}
    double scale_x_ = 1.0;     // pdf-to-canvas scale along x
    double scale_y_ = 1.0;     // pdf-to-canvas scale along y
    double origin_x_ = 0.0;    // crop_bbox x origin (pdf units)
    double origin_y_ = 0.0;    // crop_bbox y origin (pdf units, y-up)

    // Lazily-built map from normalized font stem (e.g. "times new roman bold")
    // to its absolute file path.
    std::unordered_map<std::string, std::string> font_index_;

    // Cache: normalized PDF font name → best-matched font file path.
    std::unordered_map<std::string, std::string> match_cache_;

    // Cache: cache_key → loaded BLFontFace.
    std::unordered_map<std::string, BLFontFace> font_cache_;

    // Convert PDF coordinates (origin at crop_bbox bottom-left, y-up) to
    // canvas coordinates (origin top-left, y-down), applying scale.
    double canvas_x(double pdf_x) const { return (pdf_x - origin_x_) * scale_x_; }
    double canvas_y(double pdf_y) const
    {
      return static_cast<double>(shape_[0]) - (pdf_y - origin_y_) * scale_y_;
    }

    // Normalize a font name for fuzzy comparison:
    //   1. strip leading '/'
    //   2. strip subset prefix like "ABCDEF+"
    //   3. replace '-' with ' '
    //   4. insert spaces at camelCase boundaries
    //   5. lowercase
    //   6. strip PDF/PS suffixes (psmt, ps, mt)
    static std::string normalize_font_name(const std::string& name)
    {
      std::string s = name;
      if (not s.empty() and s[0] == '/') { s = s.substr(1); }
      // strip 6-letter uppercase subset prefix, e.g. "ABCDEF+"
      if (s.size() > 7 and s[6] == '+' and
          std::all_of(s.begin(), s.begin() + 6,
                      [](char c){ return std::isupper(static_cast<unsigned char>(c)); }))
        {
          s = s.substr(7);
        }
      std::replace(s.begin(), s.end(), '-', ' ');
      // split camelCase: insert space before uppercase that follows lowercase
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
      // strip known PS/PDF suffixes at the end
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
      // trim trailing spaces
      while (not expanded.empty() and expanded.back() == ' ')
        {
          expanded.pop_back();
        }
      return expanded;
    }

    static bool should_fit_glyph_bbox_to_target(const std::string& text)
    {
      if (text.empty()) { return false; }

      for (unsigned char ch : text)
        {
          if (ch >= 0x80)
            {
              return true;
            }
          switch (ch)
            {
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
            case '<':
            case '>':
              return true;
            default:
              break;
            }
          if (std::isalnum(ch) || std::ispunct(ch) || std::isspace(ch))
            {
              continue;
            }
          return true;
        }

      return false;
    }

    // Build font_index_ by scanning standard system font directories.
    void build_font_index()
    {
      if (not font_index_.empty()) { return; }
      namespace fs = std::filesystem;
      const std::vector<std::string> font_dirs = {
        "/System/Library/Fonts",
        "/System/Library/Fonts/Supplemental",
        "/Library/Fonts",
      };
      for (const auto& dir : font_dirs)
        {
          if (not fs::is_directory(dir)) { continue; }
          for (const auto& entry : fs::directory_iterator(dir))
            {
              const auto& p = entry.path();
              const std::string ext = p.extension().string();
              if (ext != ".ttf" and ext != ".otf" and ext != ".ttc") { continue; }
              const std::string stem = p.stem().string();
              const std::string norm = normalize_font_name(stem);
              // first entry wins (earlier dirs take priority)
              if (font_index_.find(norm) == font_index_.end())
                {
                  font_index_[norm] = p.string();
                }
            }
        }
      LOG_S(INFO) << "blend2d: font index built with "
                  << font_index_.size() << " entries";
    }

    // Find the best-matching font file path for the given normalized query.
    // Uses token overlap (Jaccard-style): score = |query_tokens ∩ cand_tokens|.
    // Returns empty string if nothing scores > 0.
    std::string fuzzy_find_font(const std::string& norm_query)
    {
      auto is_style_token = [](const std::string& tok) -> bool
      {
        static const std::array<const char*, 11> kStyleTokens = {
          "regular", "normal", "roman", "book", "medium",
          "bold", "italic", "oblique", "light", "thin", "black"
        };
        return std::find(kStyleTokens.begin(), kStyleTokens.end(), tok) != kStyleTokens.end();
      };
      auto significant_tokens = [&](const std::vector<std::string>& toks) -> std::vector<std::string>
      {
        std::vector<std::string> out;
        for (const auto& tok : toks)
          {
            if (is_style_token(tok))
              {
                continue;
              }
            out.push_back(tok);
          }
        return out;
      };
      auto vectors_equal = [](const std::vector<std::string>& lhs,
                              const std::vector<std::string>& rhs) -> bool
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
      };

      auto split_tokens = [](const std::string& s) -> std::vector<std::string>
      {
        std::vector<std::string> toks;
        std::istringstream iss(s);
        std::string tok;
        while (iss >> tok) { toks.push_back(tok); }
        return toks;
      };

      const auto q_toks = split_tokens(norm_query);
      if (q_toks.empty()) { return {}; }
      const auto q_sig_toks = significant_tokens(q_toks);
      if (q_sig_toks.empty()) { return {}; }

      // Only accept candidates with the exact same significant family/script
      // tokens. This rejects cross-script variants like:
      //   "noto sans" -> "noto sans mongolian"
      //   "noto sans jp" -> "noto sans mongolian"

      // Minimum Jaccard similarity required to accept a fuzzy match.
      // A raw intersection score of 1 on "regular" alone yields J ≈ 0.14
      // (1 shared token out of 7 in the union), which is too low and causes
      // wrong fonts (e.g. NotoSansMongolian for ShinMGoPr6N) to be selected.
      const float kMinJaccard = config_.font_similarity_cutoff;

      std::string best_path;
      float best_jaccard    = 0.0f;
      int best_size_delta   = INT_MAX;

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
          if (jaccard < kMinJaccard) { continue; }
          const int delta = std::abs(static_cast<int>(c_toks.size()) -
                                     static_cast<int>(q_toks.size()));
          if (jaccard > best_jaccard or (jaccard == best_jaccard and delta < best_size_delta))
            {
              best_jaccard    = jaccard;
              best_size_delta = delta;
              best_path       = path;
            }
        }

      return best_path;
    }

    // Return a BLFontFace for the given PDF font names, falling back to a
    // system font if none can be resolved.  Results are cached.
    BLFontFace& resolve_font_face(const std::string& font_name,
                                  const std::string& base_font);

    static bool nearly_equal(double a, double b, double eps = 1e-6)
    {
      return std::abs(a - b) <= eps;
    }

    static bool is_axis_aligned(bitmap_quad const& q, double eps = 1e-6)
    {
      return nearly_equal(q.x0, q.x1, eps) &&
             nearly_equal(q.y1, q.y2, eps) &&
             nearly_equal(q.x2, q.x3, eps) &&
             nearly_equal(q.y0, q.y3, eps);
    }

    static bool is_right_angle_rotation(bitmap_quad const& q, int& quarter_turns, double eps = 1e-6)
    {
      const double ux = q.x2 - q.x1;
      const double uy = q.y2 - q.y1;
      const double vx = q.x0 - q.x1;
      const double vy = q.y0 - q.y1;

      const bool u_horizontal = nearly_equal(uy, 0.0, eps);
      const bool u_vertical   = nearly_equal(ux, 0.0, eps);
      const bool v_horizontal = nearly_equal(vy, 0.0, eps);
      const bool v_vertical   = nearly_equal(vx, 0.0, eps);

      if (not ((u_horizontal and v_vertical) or (u_vertical and v_horizontal)))
        {
          return false;
        }

      if (u_horizontal and v_vertical)
        {
          quarter_turns = (ux >= 0.0 and vy >= 0.0) ? 0 : 2;
          return true;
        }

      if (u_vertical and v_horizontal)
        {
          quarter_turns = (uy >= 0.0 and vx >= 0.0) ? 3 : 1;
          return true;
        }

      return false;
    }

    static BLPath make_quad_path(bitmap_quad const& q)
    {
      BLPath path;
      path.move_to(q.x0, q.y0);
      path.line_to(q.x1, q.y1);
      path.line_to(q.x2, q.y2);
      path.line_to(q.x3, q.y3);
      path.close();
      return path;
    }

    static BLRect axis_aligned_rect(bitmap_quad const& q)
    {
      const double x_min = std::min({q.x0, q.x1, q.x2, q.x3});
      const double x_max = std::max({q.x0, q.x1, q.x2, q.x3});
      const double y_min = std::min({q.y0, q.y1, q.y2, q.y3});
      const double y_max = std::max({q.y0, q.y1, q.y2, q.y3});
      return BLRect(x_min, y_min, x_max - x_min, y_max - y_min);
    }

    void render_bitmap_placeholder(BLContext& ctx, bitmap_quad const& q, bool axis_aligned)
    {
      ctx.set_fill_style(BLRgba32(0x66FFFF00u));
      if (axis_aligned)
        {
          ctx.fill_rect(axis_aligned_rect(q));
        }
      else
        {
          ctx.fill_path(make_quad_path(q));
        }
    }

    void render_bitmap_axis_aligned(BLContext& ctx, BLImage const& src_img, bitmap_quad const& q, int sw, int sh)
    {
      const BLRect dst_rect = axis_aligned_rect(q);
      LOG_S(INFO) << "render_bitmap_axis_aligned"
                  << " quad=[(" << q.x0 << "," << q.y0 << "),("
                  << q.x1 << "," << q.y1 << "),("
                  << q.x2 << "," << q.y2 << "),("
                  << q.x3 << "," << q.y3 << ")]"
                  << " src=" << sw << "x" << sh
                  << " dst_rect=(" << dst_rect.x << ","
                  << dst_rect.y << ","
                  << dst_rect.w << ","
                  << dst_rect.h << ")";
      ctx.blit_image(dst_rect, src_img, BLRectI(0, 0, sw, sh));
    }

    void render_bitmap_affine(BLContext& ctx, BLImage const& src_img, bitmap_quad const& q, int sw, int sh)
    {
      const double m00 = (q.x2 - q.x1) / static_cast<double>(sw);
      const double m01 = (q.y2 - q.y1) / static_cast<double>(sw);
      const double m10 = (q.x0 - q.x1) / static_cast<double>(sh);
      const double m11 = (q.y0 - q.y1) / static_cast<double>(sh);
      const double m20 = q.x1;
      const double m21 = q.y1;

      LOG_S(INFO) << "render_bitmap_affine"
                  << " quad=[(" << q.x0 << "," << q.y0 << "),("
                  << q.x1 << "," << q.y1 << "),("
                  << q.x2 << "," << q.y2 << "),("
                  << q.x3 << "," << q.y3 << ")]"
                  << " src=" << sw << "x" << sh
                  << " ctm=[[" << m00 << "," << m01 << "],["
                  << m10 << "," << m11 << "],["
                  << m20 << "," << m21 << "]]";
      ctx.save();
      ctx.apply_transform(BLMatrix2D(m00, m01, m10, m11, m20, m21));
      ctx.blit_image(BLRect(0, 0, sw, sh), src_img, BLRectI(0, 0, sw, sh));
      ctx.restore();
    }
  };

  // ---------------------------------------------------------------------------
  // Constructor
  // ---------------------------------------------------------------------------

  inline renderer<BLEND2D>::renderer()
    : shape_({0, 0, 4})
  {}

  inline renderer<BLEND2D>::renderer(render_config config)
    : config_(config), shape_({0, 0, 4})
  {}

  // ---------------------------------------------------------------------------
  // set_size
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::set_size(size_instruction& instr)
  {
    const auto& bbox = instr.crop_bbox;
    const int pdf_w  = bbox[2] - bbox[0];
    const int pdf_h  = bbox[3] - bbox[1];

    if (pdf_w <= 0 or pdf_h <= 0) { return; }

    const auto [width, height] = resolve_canvas_size(pdf_w, pdf_h, config_);

    scale_x_ = static_cast<double>(width)  / pdf_w;
    scale_y_ = static_cast<double>(height) / pdf_h;
    origin_x_ = static_cast<double>(bbox[0]);
    origin_y_ = static_cast<double>(bbox[1]);

    shape_ = {height, width, 4};

    LOG_S(INFO) << "set_size:"
                << " crop_bbox=[" << bbox[0] << "," << bbox[1] << "," << bbox[2] << "," << bbox[3] << "]"
                << " pdf_size=" << pdf_w << "x" << pdf_h
                << " canvas=" << width << "x" << height
                << " scale=(" << scale_x_ << "," << scale_y_ << ")";

    image_.create(width, height, BL_FORMAT_PRGB32);

    // Initialise canvas to opaque white.
    BLContext ctx(image_);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
    ctx.fill_all();
    ctx.end();
  }

  // ---------------------------------------------------------------------------
  // resolve_font_face
  //
  // Resolves the best-matching system font file for the given PDF font names
  // and returns a cached BLFontFace.
  //
  // When config_.resolve_fonts is true the full lookup pipeline runs:
  //   1. Build the font index on first call (scan system font dirs).
  //   2. Normalize the PDF font name and check the match_cache_.
  //   3. Try an exact match in the font_index_.
  //   4. Fall back to token-overlap fuzzy matching.
  // When config_.resolve_fonts is false the hardcoded fallback is used directly.
  // ---------------------------------------------------------------------------

  inline BLFontFace& renderer<BLEND2D>::resolve_font_face(
      const std::string& font_name,
      const std::string& base_font)
  {
    // BLFontFace cache key: prefer font_name, fall back to base_font.
    const std::string& cache_key = (not font_name.empty() and font_name != "null")
                                     ? font_name : base_font;

    auto it = font_cache_.find(cache_key);
    if (it != font_cache_.end())
      {
        return it->second;
      }

    namespace fs = std::filesystem;
    std::string found_path;

    if (config_.resolve_fonts)
      {
        build_font_index();

        // Normalize each candidate name and check match_cache_ first.
        const std::string norm_query = normalize_font_name(cache_key);

        auto mc = match_cache_.find(norm_query);
        if (mc != match_cache_.end())
          {
            found_path = mc->second;
          }
        else
          {
            // 1. Exact match in the index.
            auto ei = font_index_.find(norm_query);
            if (ei != font_index_.end())
              {
                found_path = ei->second;
                LOG_S(INFO) << "blend2d: exact font match '"
                            << norm_query << "' → '" << found_path << "'";
              }
            else
              {
                // 2. Fuzzy token-overlap match.
                found_path = fuzzy_find_font(norm_query);
                if (not found_path.empty())
                  {
                    LOG_S(INFO) << "blend2d: fuzzy font match '"
                                << norm_query << "' → '" << found_path << "'";
                  }
              }
            match_cache_[norm_query] = found_path;
          }
      }

    // Hard-coded fallback (always present on macOS).
    if (found_path.empty())
      {
        for (const auto& fallback : {
               "/System/Library/Fonts/Helvetica.ttc",
               "/System/Library/Fonts/Arial.ttf",
               "/Library/Fonts/Arial.ttf",
             })
          {
            if (fs::exists(fallback))
              {
                found_path = fallback;
                break;
              }
          }
        if (config_.resolve_fonts)
          {
            LOG_S(WARNING) << "blend2d: no font match for '"
                           << cache_key << "', using fallback '" << found_path << "'";
          }
      }

    BLFontFace face;
    if (not found_path.empty())
      {
        const BLResult res = face.create_from_file(found_path.c_str());
        if (res != BL_SUCCESS)
          {
            LOG_S(WARNING) << "blend2d: failed to load '" << found_path
                           << "' (BLResult=" << res << ")";
          }
      }
    else
      {
        LOG_S(WARNING) << "blend2d: no font file found for '" << cache_key << "'";
      }

    font_cache_.emplace(cache_key, std::move(face));
    return font_cache_.at(cache_key);
  }

  // ---------------------------------------------------------------------------
  // render_text
  //
  // Renders the UTF-8 text string at the baseline origin derived from the
  // quad corners, using a font resolved from font_name / base_font.
  // Falls back to drawing a thin blue quad outline when no font is available.
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::render_text_legacy(text_instruction& instr)
  {
    LOG_S(INFO) << __FUNCTION__;

    if (shape_[0] == 0 or shape_[1] == 0) { return; }

    // Quad corners in canvas space (scaled + y flipped).
    const double base_x0 = canvas_x(instr.get_base_x0()), base_y0 = canvas_y(instr.get_base_y0());

    const double x0 = canvas_x(instr.get_r_x0()), y0 = canvas_y(instr.get_r_y0());
    const double x1 = canvas_x(instr.get_r_x1()), y1 = canvas_y(instr.get_r_y1());
    const double x2 = canvas_x(instr.get_r_x2()), y2 = canvas_y(instr.get_r_y2());
    const double x3 = canvas_x(instr.get_r_x3()), y3 = canvas_y(instr.get_r_y3());

    // Font size in canvas pixels: scale the PDF quad height by scale_y_.
    const double quad_h = std::abs(y3-y0);
    const double size   = ((quad_h > 0.5) ? quad_h : instr.get_font_size()) * scale_y_;

    BLFontFace& face = resolve_font_face(instr.get_font_name(),
                                         instr.get_base_font());

    LOG_S(INFO) << "face: " << face.is_valid();

    // Build the bounding quad path (reused for optional outline and fallback).
    BLPath bbox_path;
    bbox_path.move_to(x0, y0);
    bbox_path.line_to(x1, y1);
    bbox_path.line_to(x2, y2);
    bbox_path.line_to(x3, y3);
    bbox_path.close();

    BLContext ctx(image_);

    LOG_S(INFO) << "writing text: `" << instr.get_text() << "`, size: " << size << "";
    if (face.is_valid() and size > 0.5)
      {
        BLFont font;
        font.create_from_face(face, static_cast<float>(size));
        ctx.set_fill_style(BLRgba32(0xFF000000u)); // opaque black
        ctx.fill_utf8_text(BLPoint(base_x0, base_y0), font, instr.get_text().c_str());

        if (config_.draw_text_bbox)
          {
            ctx.set_stroke_style(BLRgba32(0xFF1070C0u));
            ctx.set_stroke_width(0.5);
            ctx.stroke_path(bbox_path);
          }
      }
    else
      {
        // No font available — draw the bounding quad so the text region is
        // at least visible regardless of the draw_text_bbox setting.
        LOG_S(WARNING) << "render_text_legacy: no valid font for '"
                       << instr.get_font_name() << "' / '"
                       << instr.get_base_font() << "', drawing outline only";
        ctx.set_stroke_style(BLRgba32(0xFF1070C0u));
        ctx.set_stroke_width(0.5);
        ctx.stroke_path(bbox_path);
      }

    ctx.end();
  }

  // ---------------------------------------------------------------------------
  // render_text_legacy_v2
  //
  // Second-generation text renderer: manually shapes the input string, picks
  // out glyph[0] and calls font.get_glyph_outlines() with a full affine matrix
  // to support rotated text.  Superseded by render_text (v3) which uses the
  // safer fill_utf8_text() high-level API with a context transform instead.
  // Kept for comparison / reference.
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::render_text_legacy_v2(text_instruction& instr)
  {
    LOG_S(INFO) << __FUNCTION__;

    if (shape_[0] == 0 or shape_[1] == 0) { return; }

    // Baseline origin and quad corners in canvas space (scaled + y flipped).
    const double bx = canvas_x(instr.get_base_x0());
    const double by = canvas_y(instr.get_base_y0());
    const double x0 = canvas_x(instr.get_r_x0()), y0 = canvas_y(instr.get_r_y0());
    const double x1 = canvas_x(instr.get_r_x1()), y1 = canvas_y(instr.get_r_y1());
    const double x2 = canvas_x(instr.get_r_x2()), y2 = canvas_y(instr.get_r_y2());
    const double x3 = canvas_x(instr.get_r_x3()), y3 = canvas_y(instr.get_r_y3());

    // Cell height vector in canvas: from descender-left (x0,y0) to ascender-left (x3,y3).
    const double hx = x3 - x0, hy = y3 - y0;
    const double quad_h = std::sqrt(hx * hx + hy * hy);

    // Em size in canvas pixels.
    // The PDF cell spans (ascent_norm - descent_norm) per-1000 em units = quad_h px,
    // so 1 em = 1000 * quad_h / (ascent_norm - descent_norm) px.
    const double a_norm   = instr.get_font_ascent_norm();
    const double d_norm   = instr.get_font_descent_norm();
    const double cell_span = a_norm - d_norm; // per-1000 em units
    const double em_size  = (cell_span > 1.0) ? (1000.0 * quad_h / cell_span) : quad_h;
    const double size     = (em_size > 0.5) ? em_size : instr.get_font_size() * scale_y_;

    // Guard: degenerate cell — quad_h is too small to compute a valid direction
    // vector.  Dividing by quad_h would produce NaN/Inf in the affine matrix,
    // which causes a SIGBUS when Blend2D's JIT code applies the transform.
    // Skip the glyph-outline path for such cells.
    const bool degenerate_cell = (quad_h < 0.5);

    // Build the bounding quad path (reused for optional outline / fallback).
    BLPath bbox_path;
    bbox_path.move_to(x0, y0);
    bbox_path.line_to(x1, y1);
    bbox_path.line_to(x2, y2);
    bbox_path.line_to(x3, y3);
    bbox_path.close();

    LOG_S(INFO) << "text=`" << instr.get_text() << "`"
                << " base=(" << bx << "," << by << ")"
                << " quad_h=" << quad_h
                << " a_norm=" << a_norm << " d_norm=" << d_norm
                << " cell_span=" << cell_span
                << " em_size=" << em_size << " size=" << size;

    BLFontFace& face = resolve_font_face(instr.get_font_name(),
                                         instr.get_base_font());
    LOG_S(INFO) << "face valid=" << face.is_valid()
                << " font_name=`" << instr.get_font_name() << "`"
                << " base_font=`" << instr.get_base_font() << "`";

    BLContext ctx(image_);

    if (face.is_valid() and size > 0.5)
      {
        if (config_.render_text and not degenerate_cell)
          {
            BLFont font;
            font.create_from_face(face, static_cast<float>(size));

            // Shape the single character to get its glyph ID.
            BLGlyphBuffer gb;
            gb.set_utf8_text(instr.get_text().c_str());
            font.shape(gb);

            LOG_S(INFO) << "glyph buffer empty=" << gb.is_empty();

            if (!gb.is_empty())
              {
                const BLGlyphId glyph_id = gb.glyph_run().glyph_data_as<uint32_t>()[0];
                LOG_S(INFO) << "glyph_id=" << glyph_id;

                // Build affine: glyph pixel space (y-down, baseline at origin)
                //               → canvas space (y-down, baseline at (bx, by)).
                //
                // get_glyph_outlines returns coordinates in Blend2D's y-down space:
                //   glyph +y = downward (towards descender)
                //   glyph -y = upward  (towards ascender)
                //
                // The cell height vector (x0→x3) points from descender to ascender in canvas,
                // i.e. it corresponds to the glyph -y direction.
                // Therefore: glyph +y maps to the NEGATIVE of the cell height direction.
                //
                //   BLMatrix2D: out.x = gx*m00 + gy*m10 + m20
                //               out.y = gx*m01 + gy*m11 + m21
                const double up_x  =  hx / quad_h,  up_y  =  hy / quad_h;  // canvas "up" direction
                const double adv_x = -up_y,          adv_y =  up_x;         // advance dir (90° CCW of up)
                const double dn_x  = -up_x,          dn_y  = -up_y;         // glyph +y → downward in canvas
                const BLMatrix2D m(adv_x, adv_y,
                                   dn_x,  dn_y,
                                   bx,    by);

                BLPath glyph_path;
                font.get_glyph_outlines(glyph_id, m, glyph_path);

                LOG_S(INFO) << "glyph_path empty=" << glyph_path.is_empty()
                            << " transform=[[" << adv_x << "," << adv_y << "],[" << dn_x << "," << dn_y << "],[" << bx << "," << by << "]]";

                if (!glyph_path.is_empty())
                  {
                    ctx.set_fill_style(BLRgba32(0xFF000000u)); // opaque black
                    ctx.fill_path(glyph_path);
                    LOG_S(INFO) << "filled glyph path";
                  }
                else
                  {
                    LOG_S(WARNING) << "glyph_path is empty — nothing drawn for `" << instr.get_text() << "`";
                  }
              }
          }

        if (config_.draw_text_bbox)
          {
            ctx.set_stroke_style(BLRgba32(0xFF1070C0u));
            ctx.set_stroke_width(0.5);
            ctx.stroke_path(bbox_path);
          }
      }
    else
      {
        // No font available — draw the bounding quad outline.
        LOG_S(WARNING) << "render_text: no valid font for '"
                       << instr.get_font_name() << "' / '"
                       << instr.get_base_font() << "', drawing outline only";
        ctx.set_stroke_style(BLRgba32(0xFF1070C0u));
        ctx.set_stroke_width(0.5);
        ctx.stroke_path(bbox_path);
      }

    ctx.end();
  }

  // ---------------------------------------------------------------------------
  // render_text (v3)
  //
  // Third-generation text renderer.  Applies a full affine context transform
  // to handle rotated / skewed text, then renders the complete string via
  // fill_utf8_text() — Blend2D's stable high-level text API.
  //
  // Compared to render_text_legacy_v2 this avoids get_glyph_outlines(), which
  // can cause SIGBUS on ARM64 macOS when certain system fonts (CFF/OTF) are
  // resolved for PDFs with non-standard crop-box origins.  It also correctly
  // renders multi-character text cells (legacy_v2 only drew glyph[0]).
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::render_text(text_instruction& instr)
  {
    LOG_S(INFO) << __FUNCTION__;

    if (shape_[0] == 0 or shape_[1] == 0) { return; }

    // Baseline origin and quad corners in canvas space (scaled + y-flipped).
    const double bx = canvas_x(instr.get_base_x0());
    const double by = canvas_y(instr.get_base_y0());
    const double x0 = canvas_x(instr.get_r_x0()), y0 = canvas_y(instr.get_r_y0());
    const double x1 = canvas_x(instr.get_r_x1()), y1 = canvas_y(instr.get_r_y1());
    const double x2 = canvas_x(instr.get_r_x2()), y2 = canvas_y(instr.get_r_y2());
    const double x3 = canvas_x(instr.get_r_x3()), y3 = canvas_y(instr.get_r_y3());

    // Cell height vector in canvas: from descender-left (x0,y0) to ascender-left (x3,y3).
    const double hx = x3 - x0, hy = y3 - y0;
    const double quad_h = std::sqrt(hx * hx + hy * hy);

    // Em size in canvas pixels.
    const double a_norm    = instr.get_font_ascent_norm();
    const double d_norm    = instr.get_font_descent_norm();
    const double cell_span = a_norm - d_norm; // per-1000 em units
    const double em_size   = (cell_span > 1.0) ? (1000.0 * quad_h / cell_span) : quad_h;
    const double size      = (em_size > 0.5) ? em_size : instr.get_font_size() * scale_y_;

    // Degenerate cell: quad_h too small to build a valid direction vector.
    // Dividing by quad_h would produce NaN/Inf in the affine matrix.
    if (quad_h < 0.5) { return; }

    // Build the bounding quad path (for optional bbox outline / fallback).
    BLPath bbox_path;
    bbox_path.move_to(x0, y0);
    bbox_path.line_to(x1, y1);
    bbox_path.line_to(x2, y2);
    bbox_path.line_to(x3, y3);
    bbox_path.close();

    LOG_S(INFO) << "text=`" << instr.get_text() << "`"
                << " base=(" << bx << "," << by << ")"
                << " quad_h=" << quad_h
                << " a_norm=" << a_norm << " d_norm=" << d_norm
                << " cell_span=" << cell_span
                << " em_size=" << em_size << " size=" << size;

    BLFontFace& face = resolve_font_face(instr.get_font_name(),
                                         instr.get_base_font());
    LOG_S(INFO) << "face valid=" << face.is_valid()
                << " font_name=`" << instr.get_font_name() << "`"
                << " base_font=`" << instr.get_base_font() << "`";

    LOG_S(INFO) << "render_text: before BLContext construction";
    BLContext ctx(image_);
    LOG_S(INFO) << "render_text: after BLContext construction";

    auto draw_bbox_fallback = [&]()
    {
      ctx.set_stroke_style(BLRgba32(0xFF1070C0u));
      ctx.set_stroke_width(0.5);
      ctx.stroke_path(bbox_path);
    };

    auto draw_basepoint = [&]()
    {
      if (not config_.draw_text_basepoint) { return; }
      ctx.set_fill_style(BLRgba32(0xFFFF0000u));
      ctx.fill_circle(BLCircle(bx, by, 2.0));
    };

    if (face.is_valid() and size > 0.5)
      {
        if (config_.render_text)
          {
            LOG_S(INFO) << "render_text: before BLFont construction";
            BLFont font;
            LOG_S(INFO) << "render_text: before create_from_face size=" << size;
            const BLResult font_res = font.create_from_face(face, static_cast<float>(size));
            LOG_S(INFO) << "render_text: after create_from_face res=" << font_res;
            if (font_res != BL_SUCCESS)
              {
                LOG_S(WARNING) << "render_text: create_from_face failed"
                               << " (BLResult=" << font_res << ")"
                               << " font_name=`" << instr.get_font_name() << "`"
                               << " base_font=`" << instr.get_base_font() << "`";
                draw_bbox_fallback();
                ctx.end();
                return;
              }

            // Build affine: text space (origin = baseline, y-down) → canvas space.
            //
            //   up   = (hx, hy) / quad_h  — canvas direction toward ascenders
            //   adv  = perpendicular (90° CCW of up) — advance direction
            //   dn   = -up                — y-down in glyph/text space
            //
            // BLMatrix2D: out.x = gx*m00 + gy*m10 + m20
            //             out.y = gx*m01 + gy*m11 + m21
            const double up_x  =  hx / quad_h,  up_y  =  hy / quad_h;
            const double adv_x = -up_y,          adv_y =  up_x;
            const double dn_x  = -up_x,          dn_y  = -up_y;
            const BLMatrix2D ctm(adv_x, adv_y,
                                 dn_x,  dn_y,
                                 bx,    by);

            LOG_S(INFO) << "render_text: before ctx.save";
            ctx.save();
            LOG_S(INFO) << "render_text: before apply_transform";
            const BLResult transform_res = ctx.apply_transform(ctm);
            LOG_S(INFO) << "render_text: after apply_transform res=" << transform_res;
            if (transform_res != BL_SUCCESS)
              {
                ctx.restore();
                LOG_S(WARNING) << "render_text: apply_transform failed"
                               << " (BLResult=" << transform_res << ")";
                draw_bbox_fallback();
                ctx.end();
                return;
              }
            LOG_S(INFO) << "render_text: before set_fill_style";
            ctx.set_fill_style(BLRgba32(0xFF000000u)); // opaque black
            LOG_S(INFO) << "render_text: before fill_utf8_text";
            BLGlyphBuffer gb;
            gb.set_utf8_text(instr.get_text().c_str());
            const BLResult shape_res = font.shape(gb);
            LOG_S(INFO) << "render_text: after shape res=" << shape_res
                        << " empty=" << gb.is_empty();
            if (shape_res != BL_SUCCESS || gb.is_empty())
              {
                LOG_S(WARNING) << "render_text: shaping failed or produced no glyphs"
                               << " (BLResult=" << shape_res << ")"
                               << " text=`" << instr.get_text() << "`"
                               << " font_name=`" << instr.get_font_name() << "`"
                               << " base_font=`" << instr.get_base_font() << "`";
                ctx.restore();
                draw_bbox_fallback();
                ctx.end();
                return;
              }
            const auto* placement_data = gb.placement_data();
            if (placement_data == nullptr)
              {
                LOG_S(WARNING) << "render_text: glyph placement data is null, using fallback";
                ctx.restore();
                draw_bbox_fallback();
                ctx.end();
                return;
              }
            BLPoint draw_origin(0.0, 0.0);
            double bbox_fit_scale = 1.0;
            if (instr.has_glyph_bbox())
              {
                const BLGlyphId glyph_id = gb.glyph_run().glyph_data_as<uint32_t>()[0];
                BLPath glyph_path;
                const BLMatrix2D identity(1.0, 0.0,
                                          0.0, 1.0,
                                          0.0, 0.0);
                const BLResult outline_res =
                  font.get_glyph_outlines(glyph_id, identity, glyph_path);
                LOG_S(INFO) << "render_text: glyph outline res=" << outline_res
                            << " glyph_path empty=" << glyph_path.is_empty();
                if (outline_res == BL_SUCCESS && !glyph_path.is_empty())
                  {
                    BLBox rendered_box;
                    const BLResult bbox_res = glyph_path.get_bounding_box(&rendered_box);
                    LOG_S(INFO) << "render_text: glyph outline bbox res=" << bbox_res;
                    if (bbox_res != BL_SUCCESS)
                      {
                        LOG_S(WARNING) << "render_text: glyph outline bbox failed"
                                       << " (BLResult=" << bbox_res << ")";
                      }
                    else
                      {
                        const double target_x0 = instr.get_g_x0() / 1000.0 * size;
                        const double target_y0 = -instr.get_g_y1() / 1000.0 * size;
                        const double target_x1 = instr.get_g_x1() / 1000.0 * size;
                        const double target_y1 = -instr.get_g_y0() / 1000.0 * size;

                        const double rendered_x0 = rendered_box.x0;
                        const double rendered_y0 = rendered_box.y0;
                        const double rendered_x1 = rendered_box.x1;
                        const double rendered_y1 = rendered_box.y1;

                        const double target_w = target_x1 - target_x0;
                        const double target_h = target_y1 - target_y0;
                        const double rendered_w = rendered_x1 - rendered_x0;
                        const double rendered_h = rendered_y1 - rendered_y0;
                        const double base_to_top = std::abs(target_y0);
                        const bool baseline_near_top =
                          target_h > 0.0 && (base_to_top / target_h) < 0.25;

                        if (config_.fit_glyph_bbox_to_target
                            && should_fit_glyph_bbox_to_target(instr.get_text())
                            && target_w > 0.0
                            && target_h > 0.0
                            && rendered_w > 0.0
                            && rendered_h > 0.0)
                          {
                            const double width_scale = target_w / rendered_w;
                            const double height_scale = target_h / rendered_h;
                            const bool width_limited = width_scale <= height_scale;
                            bbox_fit_scale = std::min(width_scale, height_scale);
                            const double target_center_y = 0.5 * (target_y0 + target_y1);
                            const double rendered_center_y = 0.5 * (rendered_y0 + rendered_y1);

                            draw_origin.x = target_x0 - bbox_fit_scale * rendered_x0;
                            if (width_limited)
                              {
                                draw_origin.y =
                                  target_center_y - bbox_fit_scale * rendered_center_y;
                              }
                            else
                              {
                                draw_origin.y =
                                  target_y0 - bbox_fit_scale * rendered_y0;
                              }
                            LOG_S(INFO) << "render_text: fitting rendered bbox to target bbox"
                                        << " scale=" << bbox_fit_scale
                                        << " width_limited=" << width_limited
                                        << " draw_origin=(" << draw_origin.x
                                        << "," << draw_origin.y << ")";
                          }
                        else if (baseline_near_top)
                          {
                            draw_origin.y = target_y0 - rendered_y0;
                            LOG_S(INFO) << "render_text: aligning rendered top to target top"
                                        << " target_y0=" << target_y0
                                        << " rendered_y0=" << rendered_y0
                                        << " draw_origin.y=" << draw_origin.y;
                          }

                        LOG_S(INFO) << "render_text: target bbox=["
                                    << target_x0 << ", " << target_y0 << ", "
                                    << target_x1 << ", " << target_y1 << "]"
                                    << " rendered bbox=["
                                    << rendered_x0 << ", " << rendered_y0 << ", "
                                    << rendered_x1 << ", " << rendered_y1 << "]"
                                    << " bbox_fit_scale=" << bbox_fit_scale
                                    << " baseline_near_top=" << baseline_near_top;
                      }
                  }
              }

            if (bbox_fit_scale != 1.0)
              {
                const BLResult translate_res = ctx.translate(draw_origin.x, draw_origin.y);
                if (translate_res != BL_SUCCESS)
                  {
                    LOG_S(WARNING) << "render_text: translate failed"
                                   << " (BLResult=" << translate_res << ")";
                    ctx.restore();
                    draw_bbox_fallback();
                    ctx.end();
                    return;
                  }
                const BLResult scale_res = ctx.scale(bbox_fit_scale);
                if (scale_res != BL_SUCCESS)
                  {
                    LOG_S(WARNING) << "render_text: scale failed"
                                   << " (BLResult=" << scale_res << ")";
                    ctx.restore();
                    draw_bbox_fallback();
                    ctx.end();
                    return;
                  }
                draw_origin.reset(0.0, 0.0);
              }
            const BLResult text_res =
              ctx.fill_utf8_text(draw_origin, font, instr.get_text().c_str());
            LOG_S(INFO) << "render_text: after fill_utf8_text res=" << text_res;
            LOG_S(INFO) << "render_text: before ctx.restore";
            ctx.restore();

            if (text_res != BL_SUCCESS)
              {
                LOG_S(WARNING) << "render_text: fill_utf8_text failed"
                               << " (BLResult=" << text_res << ")"
                               << " text=`" << instr.get_text() << "`";
                draw_bbox_fallback();
                ctx.end();
                return;
              }

            LOG_S(INFO) << "rendered `" << instr.get_text() << "`"
                        << " ctm=[[" << adv_x << "," << adv_y << "],[" << dn_x << "," << dn_y << "],[" << bx << "," << by << "]]";
          }

        draw_basepoint();

        if (config_.draw_text_bbox)
          {
            ctx.set_stroke_style(BLRgba32(0xFF1070C0u));
            ctx.set_stroke_width(0.5);
            ctx.stroke_path(bbox_path);
          }
      }
    else
      {
        // No valid font — draw the bounding quad outline.
        draw_basepoint();
        LOG_S(WARNING) << "render_text: no valid font for '"
                       << instr.get_font_name() << "' / '"
                       << instr.get_base_font() << "', drawing outline only";
        ctx.set_stroke_style(BLRgba32(0xFF1070C0u));
        ctx.set_stroke_width(0.5);
        ctx.stroke_path(bbox_path);
      }

    ctx.end();
  }

  // ---------------------------------------------------------------------------
  // render_bitmap
  //
  // Converts the raw pixel data into a BLImage and blits it into the
  // axis-aligned bounding box of the destination quad (y-flipped).
  // For non-axis-aligned quads a full affine transform would be needed.
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::render_bitmap(bitmap_instruction& instr)
  {
    LOG_S(INFO) << __FUNCTION__ << " for xobject_key=" << instr.get_key();

    if (shape_[0] == 0 or shape_[1] == 0)
      {
        LOG_S(WARNING) << __FUNCTION__ << ": canvas not initialised, skipping";
        return;
      }

    bitmap_quad q = {
      canvas_x(instr.get_r_x0()), canvas_y(instr.get_r_y0()),
      canvas_x(instr.get_r_x1()), canvas_y(instr.get_r_y1()),
      canvas_x(instr.get_r_x2()), canvas_y(instr.get_r_y2()),
      canvas_x(instr.get_r_x3()), canvas_y(instr.get_r_y3())
    };

    const double quad_top_w = std::hypot(q.x2 - q.x1, q.y2 - q.y1);
    const double quad_left_h = std::hypot(q.x0 - q.x1, q.y0 - q.y1);
    if (quad_top_w <= 0.0 or quad_left_h <= 0.0)
      {
        LOG_S(WARNING) << __FUNCTION__ << ": degenerate destination quad, skipping";
        return;
      }

    const auto& src_data  = instr.get_data();
    const auto& alpha_data = instr.get_alpha_data();
    const auto& src_shape = instr.get_shape(); // {height, width, channels}
    const int sh = src_shape[0];
    const int sw = src_shape[1];
    const int sc = src_shape[2];
    const bool image_mask = instr.is_image_mask();
    const auto fmt = instr.get_pixel_format();
    const auto fill_rgb = instr.get_rgb_filling();

    BLContext ctx(image_);
    const bool axis_aligned = is_axis_aligned(q);
    int quarter_turns = -1;
    const bool right_angle = is_right_angle_rotation(q, quarter_turns);

    LOG_S(INFO) << "render_bitmap: quad=[("
                << q.x0 << "," << q.y0 << "),("
                << q.x1 << "," << q.y1 << "),("
                << q.x2 << "," << q.y2 << "),("
                << q.x3 << "," << q.y3 << ")]"
                << " top_vec=(" << (q.x2 - q.x1) << "," << (q.y2 - q.y1) << ")"
                << " left_vec=(" << (q.x0 - q.x1) << "," << (q.y0 - q.y1) << ")"
                << " quad_top_w=" << quad_top_w
                << " quad_left_h=" << quad_left_h
                << " axis_aligned=" << (axis_aligned ? "true" : "false")
                << " right_angle=" << (right_angle ? "true" : "false")
                << " quarter_turns=" << quarter_turns
                << " src=" << sw << "x" << sh << "x" << sc
                << " fmt=" << static_cast<int>(fmt)
                << " image_mask=" << (image_mask ? "true" : "false");

    if ((not instr.has_data()) or sh <= 0 or sw <= 0 or sc < 1)
      {
        LOG_S(WARNING) << "render_bitmap: no pixel data for xobject_key="
                       << instr.get_key()
                       << " shape=" << sh << "x" << sw << "x" << sc
                       << " has_data=" << (instr.has_data() ? "true" : "false")
                       << " — drawing semi-transparent yellow placeholder";
        render_bitmap_placeholder(ctx, q, axis_aligned);
        ctx.end();
        return;
      }

    // Guard: pixel buffer must be large enough for the declared shape.
    const size_t expected_bytes = static_cast<size_t>(sh) * sw * sc;
    if (src_data->size() < expected_bytes)
      {
        LOG_S(WARNING) << __FUNCTION__ << ": pixel buffer too small ("
                       << src_data->size() << " < " << expected_bytes
                       << ") for shape " << sh << "x" << sw << "x" << sc
                       << " — drawing placeholder";
        render_bitmap_placeholder(ctx, q, axis_aligned);
        ctx.end();
        return;
      }

    const size_t expected_alpha_bytes = static_cast<size_t>(sh) * sw;
    const bool use_soft_mask_alpha =
      instr.has_alpha_data()
      and not image_mask
      and alpha_data->size() >= expected_alpha_bytes;
    LOG_S(INFO) << "render_bitmap: alpha_state"
                << " has_alpha=" << (instr.has_alpha_data() ? "true" : "false")
                << " use_soft_mask_alpha=" << (use_soft_mask_alpha ? "true" : "false")
                << " alpha_bytes=" << (alpha_data ? alpha_data->size() : 0);
    if(instr.has_alpha_data() and not use_soft_mask_alpha)
      {
        LOG_S(WARNING) << __FUNCTION__ << ": alpha buffer too small ("
                       << alpha_data->size() << " < " << expected_alpha_bytes
                       << ") for xobject_key=" << instr.get_key()
                       << ", ignoring SMask";
      }

    // Build a BLImage (PRGB32) from the raw channel data.
    BLImage src_img;
    src_img.create(sw, sh, BL_FORMAT_PRGB32);

    {
      BLImageData img_data;
      src_img.make_mutable(&img_data);
      auto* base = static_cast<uint8_t*>(img_data.pixel_data);
      const intptr_t stride = img_data.stride;

      if (fmt == PIXEL_FORMAT_CMYK && src_data->size() >= static_cast<size_t>(sc))
        {
          const uint8_t c = src_data->at(0);
          const uint8_t m = (sc >= 2) ? src_data->at(1) : 0;
          const uint8_t y = (sc >= 3) ? src_data->at(2) : 0;
          const uint8_t k = (sc >= 4) ? src_data->at(3) : 0;
          const uint8_t r = static_cast<uint8_t>(((255u - c) * (255u - k)) / 255u);
          const uint8_t g = static_cast<uint8_t>(((255u - m) * (255u - k)) / 255u);
          const uint8_t b = static_cast<uint8_t>(((255u - y) * (255u - k)) / 255u);
          LOG_S(INFO) << "render_bitmap: cmyk_sample[0]"
                      << " raw=(" << static_cast<int>(c) << ","
                      << static_cast<int>(m) << ","
                      << static_cast<int>(y) << ","
                      << static_cast<int>(k) << ")"
                      << " rgb=(" << static_cast<int>(r) << ","
                      << static_cast<int>(g) << ","
                      << static_cast<int>(b) << ")";
        }

      for (int row = 0; row < sh; ++row)
        {
          auto* row_ptr = reinterpret_cast<uint32_t*>(base + row * stride);
          for (int col = 0; col < sw; ++col)
            {
              const int idx = (row * sw + col) * sc;
              uint8_t r = src_data->at(idx);
              uint8_t g = (sc >= 2) ? src_data->at(idx + 1) : r;
              uint8_t b = (sc >= 3) ? src_data->at(idx + 2) : r;
              uint8_t a = 0xFFu;

              if (image_mask)
                {
                  a = static_cast<uint8_t>(0xFFu - src_data->at(idx));
                  r = static_cast<uint8_t>(fill_rgb[0]);
                  g = static_cast<uint8_t>(fill_rgb[1]);
                  b = static_cast<uint8_t>(fill_rgb[2]);
                }
              else if (fmt == PIXEL_FORMAT_CMYK and sc >= 4)
                {
                  const uint8_t c = src_data->at(idx + 0);
                  const uint8_t m = src_data->at(idx + 1);
                  const uint8_t y = src_data->at(idx + 2);
                  const uint8_t k = src_data->at(idx + 3);

                  if(instr.get_cmyk_convention() == CMYK_CONVENTION_PROCESS)
                    {
                      r = static_cast<uint8_t>(((255u - c) * (255u - k)) / 255u);
                      g = static_cast<uint8_t>(((255u - m) * (255u - k)) / 255u);
                      b = static_cast<uint8_t>(((255u - y) * (255u - k)) / 255u);
                    }
                  else
                    {
                      r = static_cast<uint8_t>((static_cast<unsigned int>(c) * k) / 255u);
                      g = static_cast<uint8_t>((static_cast<unsigned int>(m) * k) / 255u);
                      b = static_cast<uint8_t>((static_cast<unsigned int>(y) * k) / 255u);
                    }
                }
              else if (fmt == PIXEL_FORMAT_GRAY)
                {
                  g = r;
                  b = r;
                }
              if (use_soft_mask_alpha and not image_mask)
                {
                  a = alpha_data->at(static_cast<size_t>(row) * sw + col);
                }

              // Store as premultiplied ARGB (required by BL_FORMAT_PRGB32).
              const uint32_t pm_r = static_cast<uint32_t>(r) * a / 255u;
              const uint32_t pm_g = static_cast<uint32_t>(g) * a / 255u;
              const uint32_t pm_b = static_cast<uint32_t>(b) * a / 255u;
              row_ptr[col] = (static_cast<uint32_t>(a) << 24)
                | (pm_r                     << 16)
                | (pm_g                     <<  8)
                |  pm_b;
            }
        }
    }

    const bool can_use_axis_aligned_fast_path =
      axis_aligned and right_angle and quarter_turns == 0;

    if (can_use_axis_aligned_fast_path)
      {
        LOG_S(INFO) << "render_bitmap: selecting axis-aligned path";
        render_bitmap_axis_aligned(ctx, src_img, q, sw, sh);
      }
    else
      {
        LOG_S(INFO) << "render_bitmap: selecting affine path"
                    << " (right_angle=" << (right_angle ? "true" : "false")
                    << ", quarter_turns=" << quarter_turns << ")";
        render_bitmap_affine(ctx, src_img, q, sw, sh);
      }
    ctx.end();
  }

  // ---------------------------------------------------------------------------
  // render_widget
  //
  // Draws the widget's rotated bounding quad as a semi-transparent light-blue
  // filled polygon.  The text value is not rendered.
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::render_widget(text_widget_instruction& instr)
  {
    LOG_S(INFO) << __FUNCTION__ << "  text='" << instr.get_text() << "'";

    if (shape_[0] == 0 or shape_[1] == 0) { return; }

    BLPath path;
    path.move_to(canvas_x(instr.get_r_x0()), canvas_y(instr.get_r_y0()));
    path.line_to(canvas_x(instr.get_r_x1()), canvas_y(instr.get_r_y1()));
    path.line_to(canvas_x(instr.get_r_x2()), canvas_y(instr.get_r_y2()));
    path.line_to(canvas_x(instr.get_r_x3()), canvas_y(instr.get_r_y3()));
    path.close();

    BLContext ctx(image_);
    ctx.set_fill_style(BLRgba32(0x660099FFu));   // A=40%, light blue
    ctx.fill_path(path);
    ctx.set_stroke_style(BLRgba32(0xFF0099FFu));  // A=100%, blue border
    ctx.set_stroke_width(1);
    ctx.stroke_path(path);
    ctx.end();
  }

  // ---------------------------------------------------------------------------
  // render_shape
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::render_shape(shape_instruction& instr)
  {
    // LOG_S(INFO) << __FUNCTION__;

    if (shape_[0] == 0 or shape_[1] == 0) { return; }
    if (instr.size() < 2) { return; }

    const auto& xs = instr.get_x();
    const auto& ys = instr.get_y();

    BLPath path;
    path.move_to(canvas_x(xs[0]), canvas_y(ys[0]));
    for (size_t i = 1; i < instr.size(); ++i)
      {
        path.line_to(canvas_x(xs[i]), canvas_y(ys[i]));
      }

    /*
    if (instr.get_closing_type() == CLOSED)
      {
        path.close();
      }
    */
    
    const auto& rgb = instr.get_rgb_stroking();
    const uint32_t stroke_color =
      (0xFFu                          << 24) |
      (static_cast<uint32_t>(rgb[0])  << 16) |
      (static_cast<uint32_t>(rgb[1])  <<  8) |
       static_cast<uint32_t>(rgb[2]);

    BLContext ctx(image_);
    ctx.set_stroke_style(BLRgba32(stroke_color));
    ctx.set_stroke_width(1);
    ctx.stroke_path(path);
    ctx.end();
  }

  // ---------------------------------------------------------------------------
  // get_canvas
  //
  // Extracts the internal PRGB32 canvas as straight RGBA bytes.
  // Since the canvas is always opaque (alpha == 255 everywhere), no
  // un-premultiplication is necessary.
  // ---------------------------------------------------------------------------

  inline std::shared_ptr<std::vector<uint8_t>> renderer<BLEND2D>::get_canvas() const
  {

    const int h = shape_[0];
    const int w = shape_[1];
    if (h == 0 or w == 0)
      {
        return std::make_shared<std::vector<uint8_t>>();
      }

    BLImageData img_data;
    image_.get_data(&img_data);

    auto result = std::make_shared<std::vector<uint8_t>>(
                                                         static_cast<std::size_t>(h) * w * 4);

    const auto* base = static_cast<const uint8_t*>(img_data.pixel_data);
    const intptr_t stride = img_data.stride;

    for (int row = 0; row < h; ++row)
      {
        const auto* src_row =
          reinterpret_cast<const uint32_t*>(base + row * stride);
        uint8_t* dst_row = result->data() + row * w * 4;

        for (int col = 0; col < w; ++col)
          {
            // BL_FORMAT_PRGB32 value = A<<24 | R<<16 | G<<8 | B  (little-endian)
            const uint32_t px = src_row[col];
            dst_row[col * 4 + 0] = static_cast<uint8_t>((px >> 16) & 0xFFu); // R
            dst_row[col * 4 + 1] = static_cast<uint8_t>((px >>  8) & 0xFFu); // G
            dst_row[col * 4 + 2] = static_cast<uint8_t>((px >>  0) & 0xFFu); // B
            dst_row[col * 4 + 3] = static_cast<uint8_t>((px >> 24) & 0xFFu); // A
          }
      }

    return result;
  }

  // ---------------------------------------------------------------------------
  // save
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::save(const std::string& path) const
  {

    if (shape_[0] == 0 or shape_[1] == 0)
      {
        throw std::runtime_error("renderer<BLEND2D>::save: canvas is empty");
      }

    const BLResult err = image_.write_to_file(path.c_str());
    if (err != BL_SUCCESS)
      {
        throw std::runtime_error(
                                 "renderer<BLEND2D>::save: failed to write '" + path + "' "
                                 "(BLResult=" + std::to_string(err) + ")");
      }
  }

  // ---------------------------------------------------------------------------
  // show
  //
  // Writes the canvas to a temporary PNG file and opens it with the platform's
  // default image viewer, mirroring the behaviour of PIL's Image.show().
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::show() const
  {
    namespace fs = std::filesystem;

    const std::string tmp =
      (fs::temp_directory_path() / "blend2d_renderer_preview.png").string();

    save(tmp);

    const std::string cmd =
#if defined(_WIN32)
      "start \"\" \"" + tmp + "\"";
#elif defined(__APPLE__)
    "open " + tmp;
#else
    "xdg-open " + tmp + " &";
#endif

    std::system(cmd.c_str()); // NOLINT(cert-env33-c)
  }

} // namespace pdflib

#endif // PDF_BLEND2D_RENDERER_H
