//-*-C++-*-

#ifndef PDF_BLEND2D_RENDERER_H
#define PDF_BLEND2D_RENDERER_H

#include <render/template_renderer.h>
#include <render/config.h>
#include <render/blend2d_font_resolver.h>
#include <render/blend2d_embedded_font_cache.h>
#include <render/freetype_embedded_font_cache.h>

#include <blend2d/blend2d.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace pdflib
{
  template<>
  class renderer<BLEND2D>
  {
  public:

    // Creates a renderer with the default render_config and the shared default
    // Blend2D font resolver. A canvas is not allocated until set_size() receives
    // the page size instruction.
    renderer();

    // Creates a renderer with caller-provided rendering options and the shared
    // default font resolver. The config controls canvas sizing, text/bbox debug
    // drawing, font lookup, and glyph bbox fitting behavior.
    explicit renderer(render_config config);

    // Creates a renderer with caller-provided rendering options and an explicit
    // font resolver. Passing nullptr falls back to the shared default resolver.
    // This constructor is useful for threaded rendering, where a warmed resolver
    // can be shared across page renderers.
    explicit renderer(render_config config,
                      std::shared_ptr<blend2d_font_resolver> font_resolver);

    // Same as above, additionally sharing an embedded font cache (SFNT
    // programs loaded natively by Blend2D) and a FreeType cache (Type 1 /
    // bare CFF programs rendered as outline paths), so embedded font programs
    // are loaded once per document instead of once per page renderer.
    // Passing nullptr for either creates a private cache.
    explicit renderer(render_config config,
                      std::shared_ptr<blend2d_font_resolver> font_resolver,
                      std::shared_ptr<blend2d_embedded_font_cache> embedded_font_cache,
                      std::shared_ptr<freetype_embedded_font_cache> freetype_font_cache = nullptr);

    // Initializes the page canvas from the PDF crop box and render_config. This
    // computes the PDF-to-canvas scale/origin, creates a PRGB32 Blend2D image,
    // starts the page context, and fills the canvas with opaque white.
    void set_size(size_instruction& instr);

    // Renders one text cell into the page canvas. The method converts the PDF
    // baseline and glyph quad into canvas coordinates, resolves the requested
    // font, applies an affine transform for rotated/skewed text, optionally
    // adjusts glyph placement from glyph bbox metadata, and draws the UTF-8 text
    // through Blend2D's high-level text API. When rendering fails it draws the
    // text bbox fallback so the cell remains visible.
    void render_text(text_instruction& instr);

    // Draws a text widget annotation as a translucent filled quadrilateral with
    // a blue outline. This currently visualizes the widget bounds only; it does
    // not render the widget's text value.
    void render_widget(text_widget_instruction& instr);

    // Renders one bitmap/image XObject. The method validates the image buffers,
    // converts the source pixels and optional soft mask into a PRGB32 BLImage,
    // applies supported rectangular clipping, and blits either with the
    // axis-aligned fast path or a full affine transform for rotated/skewed quads.
    void render_bitmap(bitmap_instruction& instr);

    // Strokes a parsed vector shape/polyline in canvas coordinates using the
    // instruction's stroking color. Filled paths and close-path semantics are
    // not currently implemented here.
    void render_shape(shape_instruction& instr);

    // Returns the rendered canvas as RGBA bytes, row-major top-to-bottom.
    // The associated shape is {height, width, 4}.
    std::shared_ptr<std::vector<uint8_t>> get_canvas() const;

    // Returns the current canvas shape as {height, width, channels}. Before
    // set_size() this is {0, 0, 4}.
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

    struct text_geometry
    {
      double bx = 0.0;
      double by = 0.0;
      bitmap_quad bbox{};
      double hx = 0.0;
      double hy = 0.0;
      double quad_h = 0.0;
      double size = 0.0;
    };

    struct text_draw_adjustment
    {
      BLPoint draw_origin = BLPoint(0.0, 0.0);
      double bbox_fit_scale = 1.0;
      bool has_render_bbox = false;
      BLBox render_bbox{};
    };

    render_config config_;

    mutable BLImage    image_;  // internal canvas (PRGB32 format)
    mutable BLContext  context_;
    mutable bool       context_active_ = false;
    std::array<int, 3> shape_;  // {height, width, 4}
    double scale_x_ = 1.0;     // pdf-to-canvas scale along x
    double scale_y_ = 1.0;     // pdf-to-canvas scale along y
    double origin_x_ = 0.0;    // crop_bbox x origin (pdf units)
    double origin_y_ = 0.0;    // crop_bbox y origin (pdf units, y-up)

    std::shared_ptr<blend2d_font_resolver> font_resolver_;
    std::shared_ptr<blend2d_embedded_font_cache> embedded_font_cache_;
    std::shared_ptr<freetype_embedded_font_cache> freetype_font_cache_;
    std::unordered_map<std::string, BLFontFace> local_font_cache_;

    // Returns the active Blend2D context for the page, starting it lazily if
    // necessary. Throws when called before a non-empty canvas has been created.
    BLContext& page_context();

    // Ends the active Blend2D context if one is open. This flushes pending
    // drawing operations before canvas extraction or file output.
    void finish_page_context() const;

    // Convert PDF coordinates (origin at crop_bbox bottom-left, y-up) to
    // canvas coordinates (origin top-left, y-down), applying scale.
    double canvas_x(double pdf_x) const { return (pdf_x - origin_x_) * scale_x_; }
    double canvas_y(double pdf_y) const
    {
      return static_cast<double>(shape_[0]) - (pdf_y - origin_y_) * scale_y_;
    }

    // Decides whether a text cell should use glyph-bbox fitting when glyph bbox
    // metadata is available. ASCII alphanumeric/punctuation/space text usually
    // renders acceptably with normal font metrics, while non-ASCII and bracket-
    // like glyphs are more likely to need bbox fitting to match PDF extraction
    // geometry.
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

    // Return a BLFontFace for the given PDF font names, falling back to a
    // system font if none can be resolved.  Results are cached.
    BLFontFace resolve_font_face(const std::string& font_name,
                                 const std::string& base_font);

    // Returns true when every glyph in the shaped buffer is glyph id 0
    // (.notdef). Shaping against an embedded subset font succeeds even when
    // the font has no glyph for the codepoint, so this — not a shaping error —
    // signals that the embedded face cannot draw the cell.
    static bool glyph_run_all_notdef(const BLGlyphBuffer& gb)
    {
      const size_t count = gb.size();
      const uint32_t* glyph_ids = gb.glyph_run().glyph_data_as<uint32_t>();
      if (count == 0 or glyph_ids == nullptr) { return true; }

      for (size_t i = 0; i < count; ++i)
        {
          if (glyph_ids[i] != 0)
            {
              return false;
            }
        }

      return true;
    }

    // Draws a text cell whose embedded font program Blend2D cannot load
    // (Type 1, bare CFF) by filling FreeType-decomposed outline paths.
    // Returns true when the cell was fully handled (including the optional
    // basepoint/bbox debug drawing); false means the caller should continue
    // with the system font path.
    bool render_text_freetype(text_instruction& instr,
                              const text_geometry& geom,
                              const BLPath& bbox_path);

    // Last-chance glyph-identity mapping when Unicode shaping against an
    // embedded (Blend2D-loaded) face produced only .notdef: CID fonts with an
    // identity CIDToGIDMap use the character code as glyph index directly;
    // symbolic fonts are retried through the 0xF000 private-use convention.
    // On success gb holds a positioned glyph run ready for fill_glyph_run.
    static bool recover_embedded_glyphs(const BLFont& font,
                                        text_instruction& instr,
                                        BLGlyphBuffer& gb)
    {
      const int64_t char_code = instr.get_char_code();
      if (char_code < 0 or not instr.has_embedded_font()) { return false; }

      const auto& blob = instr.get_embedded_font();

      if (blob->get_is_cid_font() and blob->get_cid_to_gid_identity())
        {
          const uint32_t glyph_id = static_cast<uint32_t>(char_code);
          gb.set_glyphs(&glyph_id, 1);
          // shape() rejects glyph content, so position directly.
          if (font.position_glyphs(gb) == BL_SUCCESS and
              not gb.is_empty() and
              gb.placement_data() != nullptr)
            {
              LOG_S(INFO) << "render_text: recovered glyph via CID identity"
                          << " cid=" << char_code
                          << " font_name=`" << instr.get_font_name() << "`";
              return true;
            }
        }

      if (char_code <= 0xFF)
        {
          const uint32_t codepoint = 0xF000u + static_cast<uint32_t>(char_code);
          gb.set_utf32_text(&codepoint, 1);
          if (font.shape(gb) == BL_SUCCESS and
              not gb.is_empty() and
              gb.placement_data() != nullptr and
              not glyph_run_all_notdef(gb))
            {
              LOG_S(INFO) << "render_text: recovered glyph via symbol cmap"
                          << " char_code=" << char_code
                          << " font_name=`" << instr.get_font_name() << "`";
              return true;
            }
        }

      return false;
    }

    // Computes the canvas-space baseline, bounding quad, text cell height
    // vector, and Blend2D font size for one text instruction.
    text_geometry make_text_geometry(text_instruction& instr) const;

    // Builds the affine transform that maps Blend2D text coordinates into the
    // target PDF text cell in canvas space.
    static BLMatrix2D make_text_transform(const text_geometry& geom);

    // Applies the same text-space to canvas-space transform used for drawing to
    // a single point.
    static BLPoint transform_text_point(const text_geometry& geom,
                                        const BLPoint& p);

    // Converts a text-space bounding box into a canvas-space quad.
    static bitmap_quad transform_text_box(const text_geometry& geom,
                                          const BLBox& box);

    // Emits the concise per-text log requested for comparing the target text
    // rectangle with the actual rendered glyph rectangle.
    static void log_text_render_rect(const std::string& text,
                                     const bitmap_quad& target_rect,
                                     const bitmap_quad& render_rect);

    // Draws the standard thin blue text bbox outline used both for explicit
    // debug output and as a fallback when text rendering fails.
    static void stroke_text_bbox(BLContext& ctx, const BLPath& bbox_path);

    // Draws the optional red baseline origin marker for text placement
    // debugging.
    void draw_text_basepoint(BLContext& ctx, const text_geometry& geom) const;

    // Computes optional glyph bbox origin/scale adjustment. The result is the
    // local text-space origin and scale to apply before fill_utf8_text().
    text_draw_adjustment calculate_glyph_bbox_adjustment(
      BLFont& font,
      BLGlyphBuffer& gb,
      text_instruction& instr,
      double size) const;

    // Compares floating-point canvas coordinates with a small tolerance. Used
    // by geometry classification helpers to avoid treating tiny conversion
    // differences as rotations or non-rectangular clips.
    static bool nearly_equal(double a, double b, double eps = 1e-6)
    {
      return std::abs(a - b) <= eps;
    }

    // Returns true when the bitmap quad edges are parallel to the canvas axes.
    // This identifies the simplest destination geometry, but does not by itself
    // prove the source image orientation is unrotated.
    static bool is_axis_aligned(bitmap_quad const& q, double eps = 1e-6)
    {
      return nearly_equal(q.x0, q.x1, eps) &&
             nearly_equal(q.y1, q.y2, eps) &&
             nearly_equal(q.x2, q.x3, eps) &&
             nearly_equal(q.y0, q.y3, eps);
    }

    // Detects bitmap quads that represent a multiple-of-90-degree rotation and
    // writes the detected number of quarter turns. The renderer currently uses
    // this with is_axis_aligned() to choose the unrotated fast path; other
    // rotations go through affine rendering.
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

    // Builds a closed Blend2D path from a four-corner bitmap/text/widget quad in
    // canvas coordinates.
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

    // Returns the axis-aligned bounding rectangle that encloses all four quad
    // corners. Used by fast-path bitmap blits, placeholders, and clip checks.
    static BLRect axis_aligned_rect(bitmap_quad const& q)
    {
      const double x_min = std::min({q.x0, q.x1, q.x2, q.x3});
      const double x_max = std::max({q.x0, q.x1, q.x2, q.x3});
      const double y_min = std::min({q.y0, q.y1, q.y2, q.y3});
      const double y_max = std::max({q.y0, q.y1, q.y2, q.y3});
      return BLRect(x_min, y_min, x_max - x_min, y_max - y_min);
    }

    // Returns true when two canvas rectangles overlap with positive area.
    // Touching edges are treated as non-intersecting.
    static bool rects_intersect(const BLRect& a, const BLRect& b)
    {
      return a.x < b.x + b.w and
             b.x < a.x + a.w and
             a.y < b.y + b.h and
             b.y < a.y + a.h;
    }

    enum bitmap_clip_result
    {
      BITMAP_CLIP_NONE,
      BITMAP_CLIP_APPLIED,
      BITMAP_CLIP_EMPTY,
    };

    // Converts a parsed rectangular clip path into a canvas-space BLRect when
    // all clip vertices lie on the rectangle edges. Non-rectangular, degenerate,
    // or unsupported clip paths return false so callers can skip them without
    // corrupting the Blend2D clip state.
    bool get_axis_aligned_clip_rect(const clip_path_instruction& clip,
                                    BLRect& rect) const
    {
      if(clip.get_shape_type() != RECTANGLE or clip.size() < 4)
        {
          return false;
        }

      double x_min = std::numeric_limits<double>::infinity();
      double y_min = std::numeric_limits<double>::infinity();
      double x_max = -std::numeric_limits<double>::infinity();
      double y_max = -std::numeric_limits<double>::infinity();

      const auto& xs = clip.get_x();
      const auto& ys = clip.get_y();
      const size_t n = clip.size();
      for(size_t i = 0; i < n; i++)
        {
          const double x = canvas_x(xs[i]);
          const double y = canvas_y(ys[i]);
          x_min = std::min(x_min, x);
          y_min = std::min(y_min, y);
          x_max = std::max(x_max, x);
          y_max = std::max(y_max, y);
        }

      static constexpr double min_canvas_clip_extent = 1e-3;
      if(x_max - x_min <= min_canvas_clip_extent or
         y_max - y_min <= min_canvas_clip_extent)
        {
          return false;
        }

      for(size_t i = 0; i < n; i++)
        {
          const double x = canvas_x(xs[i]);
          const double y = canvas_y(ys[i]);
          const bool on_vertical_edge =
            nearly_equal(x, x_min, 1e-4) or nearly_equal(x, x_max, 1e-4);
          const bool on_horizontal_edge =
            nearly_equal(y, y_min, 1e-4) or nearly_equal(y, y_max, 1e-4);

          if(not (on_vertical_edge and on_horizontal_edge))
            {
              return false;
            }
        }

      rect = BLRect(x_min, y_min, x_max - x_min, y_max - y_min);
      return true;
    }

    // Applies the bitmap instruction's clip paths to the Blend2D context. Only
    // axis-aligned rectangular clips are supported. The return value tells the
    // caller whether no clip was present, a clip was applied, or the destination
    // is fully outside the clip and bitmap rendering can be skipped.
    bitmap_clip_result apply_bitmap_clip_state(
      BLContext& ctx,
      const clip_state_instruction& clip_state,
      const BLRect& dst_rect) const
    {
      if(not clip_state.has_clip())
        {
          return BITMAP_CLIP_NONE;
        }

      bool applied_clip = false;
      for(const auto& clip_path : clip_state.get_paths())
        {
          BLRect clip_rect;
          if(get_axis_aligned_clip_rect(clip_path, clip_rect))
            {
              if(not rects_intersect(clip_rect, dst_rect))
                {
                  LOG_S(INFO) << "render_bitmap: empty image clip"
                              << " clip=(" << clip_rect.x << ", " << clip_rect.y
                              << ", " << clip_rect.w << ", " << clip_rect.h << ")"
                              << " dst=(" << dst_rect.x << ", " << dst_rect.y
                              << ", " << dst_rect.w << ", " << dst_rect.h << ")";
                  return BITMAP_CLIP_EMPTY;
                }

              ctx.clip_to_rect(clip_rect);
              applied_clip = true;
            }
          else
            {
              LOG_S(WARNING) << "render_bitmap: skipping unsupported non-rectangular clip path";
            }
        }

      return applied_clip ? BITMAP_CLIP_APPLIED : BITMAP_CLIP_NONE;
    }

    // Draws a semi-transparent yellow placeholder over the bitmap destination
    // quad. This makes missing or invalid image data visible in debug renders.
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

    // Converts the bitmap instruction's source channels and optional soft mask
    // into the premultiplied PRGB32 Blend2D image format expected by blit_image().
    BLImage build_bitmap_image(bitmap_instruction& instr,
                               int sw,
                               int sh,
                               int sc,
                               bool use_soft_mask_alpha) const;

    // Blits an unrotated, axis-aligned source image into the destination
    // rectangle. This is the simple fast path used when the quad has no rotation
    // or skew relative to the canvas.
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

    // Blits a source image through an affine transform derived from the
    // destination quad. This handles rotated and skewed image placement by
    // mapping source image coordinates into canvas coordinates.
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
    : shape_({0, 0, 4}),
      font_resolver_(blend2d_font_resolver::default_resolver()),
      embedded_font_cache_(std::make_shared<blend2d_embedded_font_cache>()),
      freetype_font_cache_(std::make_shared<freetype_embedded_font_cache>())
  {}

  inline renderer<BLEND2D>::renderer(render_config config)
    : config_(config),
      shape_({0, 0, 4}),
      font_resolver_(blend2d_font_resolver::default_resolver()),
      embedded_font_cache_(std::make_shared<blend2d_embedded_font_cache>()),
      freetype_font_cache_(std::make_shared<freetype_embedded_font_cache>())
  {}

  inline renderer<BLEND2D>::renderer(render_config config,
                                     std::shared_ptr<blend2d_font_resolver> font_resolver)
    : config_(config),
      shape_({0, 0, 4}),
      font_resolver_(font_resolver ? std::move(font_resolver)
                                   : blend2d_font_resolver::default_resolver()),
      embedded_font_cache_(std::make_shared<blend2d_embedded_font_cache>()),
      freetype_font_cache_(std::make_shared<freetype_embedded_font_cache>())
  {}

  inline renderer<BLEND2D>::renderer(render_config config,
                                     std::shared_ptr<blend2d_font_resolver> font_resolver,
                                     std::shared_ptr<blend2d_embedded_font_cache> embedded_font_cache,
                                     std::shared_ptr<freetype_embedded_font_cache> freetype_font_cache)
    : config_(config),
      shape_({0, 0, 4}),
      font_resolver_(font_resolver ? std::move(font_resolver)
                                   : blend2d_font_resolver::default_resolver()),
      embedded_font_cache_(embedded_font_cache
                             ? std::move(embedded_font_cache)
                             : std::make_shared<blend2d_embedded_font_cache>()),
      freetype_font_cache_(freetype_font_cache
                             ? std::move(freetype_font_cache)
                             : std::make_shared<freetype_embedded_font_cache>())
  {}

  inline BLContext& renderer<BLEND2D>::page_context()
  {
    if (context_active_)
      {
        return context_;
      }

    if (shape_[0] == 0 or shape_[1] == 0)
      {
        throw std::runtime_error("renderer<BLEND2D>::page_context: canvas is empty");
      }

    const BLResult err = context_.begin(image_);
    if (err != BL_SUCCESS)
      {
        throw std::runtime_error(
          "renderer<BLEND2D>::page_context: failed to begin Blend2D context "
          "(BLResult=" + std::to_string(err) + ")");
      }

    context_active_ = true;
    return context_;
  }

  inline void renderer<BLEND2D>::finish_page_context() const
  {
    if (not context_active_)
      {
        return;
      }

    const BLResult err = context_.end();
    context_active_ = false;
    if (err != BL_SUCCESS)
      {
        throw std::runtime_error(
          "renderer<BLEND2D>::finish_page_context: failed to end Blend2D context "
          "(BLResult=" + std::to_string(err) + ")");
      }
  }

  // ---------------------------------------------------------------------------
  // set_size
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::set_size(size_instruction& instr)
  {
    finish_page_context();

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
    const BLResult ctx_res = context_.begin(image_);
    if (ctx_res != BL_SUCCESS)
      {
        throw std::runtime_error(
          "renderer<BLEND2D>::set_size: failed to begin Blend2D context "
          "(BLResult=" + std::to_string(ctx_res) + ")");
      }
    context_active_ = true;

    context_.set_comp_op(BL_COMP_OP_SRC_COPY);
    context_.set_fill_style(BLRgba32(0xFFFFFFFFu));
    context_.fill_all();
    context_.set_comp_op(BL_COMP_OP_SRC_OVER);
  }

  // ---------------------------------------------------------------------------
  // resolve_font_face
  //
  // Resolves a BLFontFace through the shared resolver and keeps only a small
  // per-page alias cache in the renderer hot path.
  // ---------------------------------------------------------------------------

  inline BLFontFace renderer<BLEND2D>::resolve_font_face(
      const std::string& font_name,
      const std::string& base_font)
  {
    const std::string& cache_key = (not font_name.empty() and font_name != "null")
                                     ? font_name : base_font;

    auto itr = local_font_cache_.find(cache_key);
    if (itr != local_font_cache_.end())
      {
        LOG_S(INFO) << "render_text: local font cache hit"
                    << " font_name=`" << font_name << "`"
                    << " base_font=`" << base_font << "`"
                    << " cache_key=`" << cache_key << "`"
                    << " valid=" << (itr->second.is_valid() ? "true" : "false");
        return itr->second;
      }

    LOG_S(INFO) << "render_text: local font cache miss"
                << " font_name=`" << font_name << "`"
                << " base_font=`" << base_font << "`"
                << " cache_key=`" << cache_key << "`";
    BLFontFace face = font_resolver_->resolve_font_face(cache_key,
                                                        base_font,
                                                        config_.resolve_fonts,
                                                        config_.font_similarity_cutoff);
    auto [inserted_itr, inserted] = local_font_cache_.emplace(cache_key, face);
    LOG_S(INFO) << "render_text: resolved font face"
                << " font_name=`" << font_name << "`"
                << " base_font=`" << base_font << "`"
                << " cache_key=`" << cache_key << "`"
                << " valid=" << (inserted_itr->second.is_valid() ? "true" : "false");
    return inserted_itr->second;
  }

  inline renderer<BLEND2D>::text_geometry renderer<BLEND2D>::make_text_geometry(
      text_instruction& instr) const
  {
    text_geometry geom;
    geom.bx = canvas_x(instr.get_base_x0());
    geom.by = canvas_y(instr.get_base_y0());
    geom.bbox = {
      canvas_x(instr.get_r_x0()), canvas_y(instr.get_r_y0()),
      canvas_x(instr.get_r_x1()), canvas_y(instr.get_r_y1()),
      canvas_x(instr.get_r_x2()), canvas_y(instr.get_r_y2()),
      canvas_x(instr.get_r_x3()), canvas_y(instr.get_r_y3())
    };

    geom.hx = geom.bbox.x3 - geom.bbox.x0;
    geom.hy = geom.bbox.y3 - geom.bbox.y0;
    geom.quad_h = std::sqrt(geom.hx * geom.hx + geom.hy * geom.hy);

    const double a_norm = instr.get_font_ascent_norm();
    const double d_norm = instr.get_font_descent_norm();
    const double cell_span = a_norm - d_norm; // per-1000 em units
    const double em_size =
      (cell_span > 1.0) ? (1000.0 * geom.quad_h / cell_span) : geom.quad_h;
    geom.size =
      (em_size > 0.5) ? em_size : instr.get_font_size() * scale_y_;

    return geom;
  }

  inline BLMatrix2D renderer<BLEND2D>::make_text_transform(
      const text_geometry& geom)
  {
    // Build affine: text space (origin = baseline, y-down) -> canvas space.
    //
    //   up   = (hx, hy) / quad_h  - canvas direction toward ascenders
    //   adv  = perpendicular (90 deg CCW of up) - advance direction
    //   dn   = -up                - y-down in glyph/text space
    //
    // BLMatrix2D: out.x = gx*m00 + gy*m10 + m20
    //             out.y = gx*m01 + gy*m11 + m21
    const double up_x  =  geom.hx / geom.quad_h;
    const double up_y  =  geom.hy / geom.quad_h;
    const double adv_x = -up_y;
    const double adv_y =  up_x;
    const double dn_x  = -up_x;
    const double dn_y  = -up_y;

    return BLMatrix2D(adv_x,  adv_y,
                      dn_x,   dn_y,
                      geom.bx, geom.by);
  }

  inline BLPoint renderer<BLEND2D>::transform_text_point(
      const text_geometry& geom,
      const BLPoint& p)
  {
    const double up_x  =  geom.hx / geom.quad_h;
    const double up_y  =  geom.hy / geom.quad_h;
    const double adv_x = -up_y;
    const double adv_y =  up_x;
    const double dn_x  = -up_x;
    const double dn_y  = -up_y;

    return BLPoint(p.x * adv_x + p.y * dn_x + geom.bx,
                   p.x * adv_y + p.y * dn_y + geom.by);
  }

  inline renderer<BLEND2D>::bitmap_quad renderer<BLEND2D>::transform_text_box(
      const text_geometry& geom,
      const BLBox& box)
  {
    const BLPoint p0 = transform_text_point(geom, BLPoint(box.x0, box.y1));
    const BLPoint p1 = transform_text_point(geom, BLPoint(box.x0, box.y0));
    const BLPoint p2 = transform_text_point(geom, BLPoint(box.x1, box.y0));
    const BLPoint p3 = transform_text_point(geom, BLPoint(box.x1, box.y1));
    return {p0.x, p0.y, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y};
  }

  inline void renderer<BLEND2D>::log_text_render_rect(
      const std::string& text,
      const bitmap_quad& target_rect,
      const bitmap_quad& render_rect)
  {
    LOG_S(INFO) << "render_text: text: `" << text << "`"
                << ", target-rect: [("
                << target_rect.x0 << ", " << target_rect.y0 << "), ("
                << target_rect.x1 << ", " << target_rect.y1 << "), ("
                << target_rect.x2 << ", " << target_rect.y2 << "), ("
                << target_rect.x3 << ", " << target_rect.y3 << ")]"
                << ", render-rect: [("
                << render_rect.x0 << ", " << render_rect.y0 << "), ("
                << render_rect.x1 << ", " << render_rect.y1 << "), ("
                << render_rect.x2 << ", " << render_rect.y2 << "), ("
                << render_rect.x3 << ", " << render_rect.y3 << ")]";
  }

  inline void renderer<BLEND2D>::stroke_text_bbox(BLContext& ctx,
                                                  const BLPath& bbox_path)
  {
    ctx.set_stroke_style(BLRgba32(0xFF1070C0u));
    ctx.set_stroke_width(0.5);
    ctx.stroke_path(bbox_path);
  }

  inline void renderer<BLEND2D>::draw_text_basepoint(
      BLContext& ctx,
      const text_geometry& geom) const
  {
    if (not config_.draw_text_basepoint) { return; }
    ctx.set_fill_style(BLRgba32(0xFFFF0000u));
    ctx.fill_circle(BLCircle(geom.bx, geom.by, 2.0));
  }

  inline renderer<BLEND2D>::text_draw_adjustment
  renderer<BLEND2D>::calculate_glyph_bbox_adjustment(
      BLFont& font,
      BLGlyphBuffer& gb,
      text_instruction& instr,
      double size) const
  {
    text_draw_adjustment adjustment;

    const BLGlyphId glyph_id = gb.glyph_run().glyph_data_as<uint32_t>()[0];
    BLPath glyph_path;
    const BLMatrix2D identity(1.0, 0.0,
                              0.0, 1.0,
                              0.0, 0.0);
    const BLResult outline_res =
      font.get_glyph_outlines(glyph_id, identity, glyph_path);
    //LOG_S(INFO) << "render_text: glyph outline res=" << outline_res
    //<< " glyph_path empty=" << glyph_path.is_empty();
    if (outline_res != BL_SUCCESS || glyph_path.is_empty())
      {
        return adjustment;
      }

    BLBox rendered_box;
    const BLResult bbox_res = glyph_path.get_bounding_box(&rendered_box);
    //LOG_S(INFO) << "render_text: glyph outline bbox res=" << bbox_res;

    if (bbox_res != BL_SUCCESS)
      {
        LOG_S(WARNING) << "render_text: glyph outline bbox failed"
                       << " (BLResult=" << bbox_res << ")";
        return adjustment;
      }

    const double target_x0 = instr.get_g_x0() / 1000.0 * size;
    const double target_y0 = -instr.get_g_y1() / 1000.0 * size;
    const double target_x1 = instr.get_g_x1() / 1000.0 * size;
    const double target_y1 = -instr.get_g_y0() / 1000.0 * size;

    const double rendered_x0 = rendered_box.x0;
    const double rendered_y0 = rendered_box.y0;
    const double rendered_x1 = rendered_box.x1;
    const double rendered_y1 = rendered_box.y1;
    adjustment.has_render_bbox = true;
    adjustment.render_bbox = rendered_box;

    const double target_w = target_x1 - target_x0;
    const double target_h = target_y1 - target_y0;
    const double rendered_w = rendered_x1 - rendered_x0;
    const double rendered_h = rendered_y1 - rendered_y0;
    bool baseline_near_top = false;

    if (instr.has_glyph_bbox())
      {
        const double base_to_top = std::abs(target_y0);
        baseline_near_top =
          target_h > 0.0 && (base_to_top / target_h) < 0.25;
      }

    if (instr.has_glyph_bbox()
        && config_.fit_glyph_bbox_to_target
        && should_fit_glyph_bbox_to_target(instr.get_text())
        && target_w > 0.0
        && target_h > 0.0
        && rendered_w > 0.0
        && rendered_h > 0.0)
      {
        const double width_scale = target_w / rendered_w;
        const double height_scale = target_h / rendered_h;
        const bool width_limited = width_scale <= height_scale;
        adjustment.bbox_fit_scale = std::min(width_scale, height_scale);
        const double target_center_y = 0.5 * (target_y0 + target_y1);
        const double rendered_center_y = 0.5 * (rendered_y0 + rendered_y1);

        adjustment.draw_origin.x =
          target_x0 - adjustment.bbox_fit_scale * rendered_x0;
        if (width_limited)
          {
            adjustment.draw_origin.y =
              target_center_y - adjustment.bbox_fit_scale * rendered_center_y;
          }
        else
          {
            adjustment.draw_origin.y =
              target_y0 - adjustment.bbox_fit_scale * rendered_y0;
          }
        LOG_S(INFO) << "render_text: fitting rendered bbox to target bbox"
                    << " scale=" << adjustment.bbox_fit_scale
                    << " width_limited=" << width_limited
                    << " draw_origin=(" << adjustment.draw_origin.x
                    << "," << adjustment.draw_origin.y << ")";
      }
    else if (baseline_near_top)
      {
        adjustment.draw_origin.y = target_y0 - rendered_y0;
        LOG_S(INFO) << "render_text: aligning rendered top to target top"
                    << " target_y0=" << target_y0
                    << " rendered_y0=" << rendered_y0
                    << " draw_origin.y=" << adjustment.draw_origin.y;
      }

    return adjustment;
  }

  inline BLImage renderer<BLEND2D>::build_bitmap_image(
      bitmap_instruction& instr,
      int sw,
      int sh,
      int sc,
      bool use_soft_mask_alpha) const
  {
    const auto& src_data = instr.get_data();
    const auto& alpha_data = instr.get_alpha_data();
    const bool image_mask = instr.is_image_mask();
    const auto fmt = instr.get_pixel_format();
    const auto fill_rgb = instr.get_rgb_filling();

    BLImage src_img;
    src_img.create(sw, sh, BL_FORMAT_PRGB32);

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

    return src_img;
  }

  // ---------------------------------------------------------------------------
  // render_text_freetype
  //
  // Fallback path for embedded font programs Blend2D cannot load (Type 1,
  // bare CFF): FreeType maps the cell's character code through the font's
  // builtin encoding and decomposes the glyph outlines into a BLPath, which
  // is filled under the same text transform as the regular path.
  // ---------------------------------------------------------------------------

  inline bool renderer<BLEND2D>::render_text_freetype(text_instruction& instr,
                                                      const text_geometry& geom,
                                                      const BLPath& bbox_path)
  {
    if (freetype_font_cache_ == nullptr or not freetype_font_cache_->available())
      {
        return false;
      }

    // With text rendering disabled the regular path handles the debug-only
    // drawing (basepoint/bbox) through the system-resolved face.
    if (not config_.render_text) { return false; }

    if (geom.size <= 0.5) { return false; }

    BLPath text_path;
    if (not freetype_font_cache_->build_text_path(instr.get_embedded_font(),
                                                  instr.get_text(),
                                                  instr.get_char_code(),
                                                  instr.get_glyph_name(),
                                                  geom.size,
                                                  text_path))
      {
        return false;
      }

    BLContext& ctx = page_context();

    ctx.save();
    const BLResult transform_res = ctx.apply_transform(make_text_transform(geom));
    if (transform_res != BL_SUCCESS)
      {
        ctx.restore();
        LOG_S(WARNING) << "render_text_freetype: apply_transform failed"
                       << " (BLResult=" << transform_res << ")";
        return false;
      }

    ctx.set_fill_style(BLRgba32(0xFF000000u)); // opaque black
    if (not text_path.is_empty())
      {
        ctx.fill_path(text_path);
      }
    ctx.restore();

    draw_text_basepoint(ctx, geom);
    if (config_.draw_text_bbox)
      {
        stroke_text_bbox(ctx, bbox_path);
      }

    return true;
  }

  // ---------------------------------------------------------------------------
  // render_text
  //
  // Applies a full affine context transform
  // to handle rotated / skewed text, then renders the complete string via
  // fill_utf8_text() — Blend2D's stable high-level text API.
  //
  // The renderer avoids using glyph-outline rendering for the main text path,
  // because get_glyph_outlines() has triggered platform-specific crashes with
  // some system fonts. The outline API is still used only for optional glyph
  // bbox measurement before text is drawn with fill_utf8_text().
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::render_text(text_instruction& instr)
  {
    // LOG_S(INFO) << __FUNCTION__;

    if (shape_[0] == 0 or shape_[1] == 0) { return; }

    const text_geometry geom = make_text_geometry(instr);

    // Degenerate cell: quad_h too small to build a valid direction vector.
    // Dividing by quad_h would produce NaN/Inf in the affine matrix.
    if (geom.quad_h < 0.5) { return; }

    // Build the bounding quad path (for optional bbox outline / fallback).
    const BLPath bbox_path = make_quad_path(geom.bbox);

    // LOG_S(INFO) << "text=`" << instr.get_text() << "`"
    //             << " base=(" << bx << "," << by << ")"
    //             << " quad_h=" << quad_h
    //             << " a_norm=" << a_norm << " d_norm=" << d_norm
    //             << " cell_span=" << cell_span
    //             << " em_size=" << em_size << " size=" << size;

    // Resolution order: embedded font program — natively in Blend2D (SFNT)
    // or as FreeType outline paths (Type 1, bare CFF) — then the system font
    // resolver, then the hardcoded fallback.
    bool using_embedded_font = false;
    BLFontFace face;
    if (config_.use_embedded_fonts and instr.has_embedded_font())
      {
        face = embedded_font_cache_->resolve(instr.get_embedded_font());
        using_embedded_font = face.is_valid();
        if (not using_embedded_font)
          {
            if (render_text_freetype(instr, geom, bbox_path))
              {
                return;
              }

            LOG_S(INFO) << "render_text: embedded font not loadable"
                        << " font_name=`" << instr.get_font_name() << "`"
                        << " format=" << to_string(instr.get_embedded_font()->get_format())
                        << " — using system resolver";
          }
      }

    if (not using_embedded_font)
      {
        face = resolve_font_face(instr.get_font_name(),
                                 instr.get_base_font());
      }
    // LOG_S(INFO) << "face valid=" << face.is_valid()
    //             << " font_name=`" << instr.get_font_name() << "`"
    //             << " base_font=`" << instr.get_base_font() << "`"
    //             << " embedded=" << using_embedded_font;

    BLContext& ctx = page_context();

    auto draw_bbox_fallback = [&]()
    {
      stroke_text_bbox(ctx, bbox_path);
    };

    if (face.is_valid() and geom.size > 0.5)
      {
        if (config_.render_text)
          {
            // LOG_S(INFO) << "render_text: before BLFont construction";
            BLFont font;
            // LOG_S(INFO) << "render_text: before create_from_face size=" << size;
            const BLResult font_res =
              font.create_from_face(face, static_cast<float>(geom.size));
            // LOG_S(INFO) << "render_text: after create_from_face res=" << font_res;
            if (font_res != BL_SUCCESS)
              {
                LOG_S(WARNING) << "render_text: create_from_face failed"
                               << " (BLResult=" << font_res << ")"
                               << " font_name=`" << instr.get_font_name() << "`"
                               << " base_font=`" << instr.get_base_font() << "`";
                draw_bbox_fallback();
                return;
              }

            const BLMatrix2D ctm = make_text_transform(geom);
            // LOG_S(INFO) << "render_text: before ctx.save";
            ctx.save();
            // LOG_S(INFO) << "render_text: before apply_transform";
            const BLResult transform_res = ctx.apply_transform(ctm);
            // LOG_S(INFO) << "render_text: after apply_transform res=" << transform_res;
            if (transform_res != BL_SUCCESS)
              {
                ctx.restore();
                LOG_S(WARNING) << "render_text: apply_transform failed"
                               << " (BLResult=" << transform_res << ")";
                draw_bbox_fallback();
                return;
              }
            // LOG_S(INFO) << "render_text: before set_fill_style";
            ctx.set_fill_style(BLRgba32(0xFF000000u)); // opaque black
            // LOG_S(INFO) << "render_text: before fill_utf8_text";
            BLGlyphBuffer gb;
            gb.set_utf8_text(instr.get_text().c_str());
            const BLResult shape_res = font.shape(gb);
            // LOG_S(INFO) << "render_text: after shape res=" << shape_res
            //             << " empty=" << gb.is_empty();
            if (shape_res != BL_SUCCESS || gb.is_empty())
              {
                LOG_S(WARNING) << "render_text: shaping failed or produced no glyphs"
                               << " (BLResult=" << shape_res << ")"
                               << " text=`" << instr.get_text() << "`"
                               << " font_name=`" << instr.get_font_name() << "`"
                               << " base_font=`" << instr.get_base_font() << "`";
                ctx.restore();
                draw_bbox_fallback();
                return;
              }

            if (using_embedded_font and glyph_run_all_notdef(gb))
              {
                // Shaping against an embedded subset font "succeeds" with
                // glyph id 0 when the Unicode cmap simply lacks the
                // codepoint. Try glyph-identity mapping (CID identity,
                // symbol cmap) before giving the cell to the system face —
                // drawing the .notdef run would produce blank/tofu output.
                if (not recover_embedded_glyphs(font, instr, gb))
                  {
                    LOG_S(INFO) << "render_text: embedded face has no glyph"
                                << " text=`" << instr.get_text() << "`"
                                << " font_name=`" << instr.get_font_name() << "`"
                                << " — falling back to system font";

                    BLFontFace system_face = resolve_font_face(instr.get_font_name(),
                                                               instr.get_base_font());
                    BLFont system_font;
                    if (system_face.is_valid() and
                        system_font.create_from_face(system_face,
                                                     static_cast<float>(geom.size)) == BL_SUCCESS)
                      {
                        gb.set_utf8_text(instr.get_text().c_str());
                        if (system_font.shape(gb) == BL_SUCCESS and not gb.is_empty())
                          {
                            font = system_font;
                            using_embedded_font = false;
                          }
                      }
                  }
              }

            const auto* placement_data = gb.placement_data();
            if (placement_data == nullptr)
              {
                LOG_S(WARNING) << "render_text: glyph placement data is null, using fallback";
                ctx.restore();
                draw_bbox_fallback();
                return;
              }
            text_draw_adjustment adjustment =
              calculate_glyph_bbox_adjustment(font, gb, instr, geom.size);

            BLBox adjusted_render_box;
            if (adjustment.has_render_bbox)
              {
                adjusted_render_box.x0 =
                  adjustment.draw_origin.x +
                  adjustment.bbox_fit_scale * adjustment.render_bbox.x0;
                adjusted_render_box.y0 =
                  adjustment.draw_origin.y +
                  adjustment.bbox_fit_scale * adjustment.render_bbox.y0;
                adjusted_render_box.x1 =
                  adjustment.draw_origin.x +
                  adjustment.bbox_fit_scale * adjustment.render_bbox.x1;
                adjusted_render_box.y1 =
                  adjustment.draw_origin.y +
                  adjustment.bbox_fit_scale * adjustment.render_bbox.y1;
              }

            if (adjustment.bbox_fit_scale != 1.0)
              {
                const BLResult translate_res =
                  ctx.translate(adjustment.draw_origin.x,
                                adjustment.draw_origin.y);
                if (translate_res != BL_SUCCESS)
                  {
                    LOG_S(WARNING) << "render_text: translate failed"
                                   << " (BLResult=" << translate_res << ")";
                    ctx.restore();
                    draw_bbox_fallback();
                    return;
                  }
                const BLResult scale_res = ctx.scale(adjustment.bbox_fit_scale);
                if (scale_res != BL_SUCCESS)
                  {
                    LOG_S(WARNING) << "render_text: scale failed"
                                   << " (BLResult=" << scale_res << ")";
                    ctx.restore();
                    draw_bbox_fallback();
                    return;
                  }
                adjustment.draw_origin.reset(0.0, 0.0);
              }
            // Draw the glyph run that was already shaped (or recovered by
            // glyph identity) above; fill_utf8_text would re-shape the text
            // and lose any glyph-identity recovery.
            const BLResult text_res =
              ctx.fill_glyph_run(adjustment.draw_origin,
                                 font,
                                 gb.glyph_run());
            // LOG_S(INFO) << "render_text: after fill_glyph_run res=" << text_res;
            // LOG_S(INFO) << "render_text: before ctx.restore";
            ctx.restore();

            if (text_res != BL_SUCCESS)
              {
                LOG_S(WARNING) << "render_text: fill_glyph_run failed"
                               << " (BLResult=" << text_res << ")"
                               << " text=`" << instr.get_text() << "`";
                draw_bbox_fallback();
                return;
              }

            if (adjustment.has_render_bbox)
              {
                log_text_render_rect(instr.get_text(),
                                     geom.bbox,
                                     transform_text_box(geom,
                                                        adjusted_render_box));
              }

            // LOG_S(INFO) << "rendered `" << instr.get_text() << "`"
            //             << " ctm=[[" << adv_x << "," << adv_y << "],[" << dn_x << "," << dn_y << "],[" << bx << "," << by << "]]";
          }

        draw_text_basepoint(ctx, geom);

        if (config_.draw_text_bbox)
          {
            stroke_text_bbox(ctx, bbox_path);
          }
      }
    else
      {
        // No valid font — draw the bounding quad outline.
        draw_text_basepoint(ctx, geom);
        LOG_S(WARNING) << "render_text: no valid font for '"
                       << instr.get_font_name() << "' / '"
                       << instr.get_base_font() << "', drawing outline only";
        stroke_text_bbox(ctx, bbox_path);
      }
  }

  // ---------------------------------------------------------------------------
  // render_bitmap
  //
  // Converts raw pixel data and optional alpha data into a PRGB32 BLImage,
  // applies supported clipping, and blits the image into the destination quad
  // using either the axis-aligned fast path or an affine transform.
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

    BLContext& ctx = page_context();
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

    const BLImage src_img =
      build_bitmap_image(instr, sw, sh, sc, use_soft_mask_alpha);

    const bool can_use_axis_aligned_fast_path =
      axis_aligned and right_angle and quarter_turns == 0;

    const bool has_clip = instr.has_clip_state();
    bool clip_active = false;
    if(has_clip)
      {
        LOG_S(INFO) << "render_bitmap: applying "
                    << instr.get_clip_state().get_paths().size()
                    << " clip path(s)";
        ctx.save();
        const bitmap_clip_result clip_result =
          apply_bitmap_clip_state(ctx,
                                  instr.get_clip_state(),
                                  axis_aligned_rect(q));
        if(clip_result == BITMAP_CLIP_EMPTY)
          {
            ctx.restore();
            return;
          }

        clip_active = clip_result == BITMAP_CLIP_APPLIED;
        if(not clip_active)
          {
            ctx.restore();
          }
      }

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

    if(clip_active)
      {
        ctx.restore();
      }
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

    BLContext& ctx = page_context();
    ctx.set_fill_style(BLRgba32(0x660099FFu));   // A=40%, light blue
    ctx.fill_path(path);
    ctx.set_stroke_style(BLRgba32(0xFF0099FFu));  // A=100%, blue border
    ctx.set_stroke_width(1);
    ctx.stroke_path(path);
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

    BLContext& ctx = page_context();
    ctx.set_stroke_style(BLRgba32(stroke_color));
    ctx.set_stroke_width(1);
    ctx.stroke_path(path);
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

    finish_page_context();

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

    finish_page_context();

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
