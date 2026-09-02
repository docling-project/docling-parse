//-*-C++-*-

#ifndef PDF_BLEND2D_RENDERER_H
#define PDF_BLEND2D_RENDERER_H

#include <render/template_renderer.h>
#include <render/config.h>
#include <parse/utils/color/device_cmyk.h>
#include <render/blend2d_font_resolver.h>
#include <render/blend2d_embedded_font_cache.h>
#include <render/freetype_font_cache.h>
#include <render/glyph_bbox_cache.h>

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
                      std::shared_ptr<freetype_font_cache> embedded_freetype_cache = nullptr,
                      std::shared_ptr<glyph_bbox_cache> glyph_bbox_cache = nullptr);

    // Returns the page canvas to the per-thread pool. See set_size().
    ~renderer();

    // Per-phase rasterisation timings for the page just rendered. The threaded
    // pipeline merges these into the page decoder's own pdf_timings, so a
    // render run accounts for rasterising the same way it accounts for
    // decoding; without this the whole render stage is invisible to
    // scripts/benchmarking/run_performance_analysis.py.
    pdf_timings& get_timings() { return timings_; }
    const pdf_timings& get_timings() const { return timings_; }

    // Initializes the page canvas from the PDF crop box and render_config. This
    // computes the PDF-to-canvas scale/origin, creates a PRGB32 Blend2D image,
    // starts the page context, and fills the canvas with opaque white.
    //
    // All drawing happens in unrotated page space, which is the space the
    // render instructions are expressed in. When the page carries a /Rotate,
    // the canvas is allocated in that space (so width and height are swapped
    // with respect to the reported shape) and the finished canvas is rotated
    // into display orientation before it leaves the renderer.
    void set_size(size_instruction& instr);

    // Renders one text cell into the page canvas. The method converts the PDF
    // baseline and glyph quad into canvas coordinates, resolves the requested
    // font, applies an affine transform for rotated/skewed text, optionally
    // adjusts glyph placement from glyph bbox metadata, and draws the UTF-8 text
    // through Blend2D's high-level text API. When rendering fails it draws the
    // text bbox fallback so the cell remains visible.
    void render_text(text_instruction& instr);

    // Draws a text widget annotation as a translucent filled quadrilateral
    // outlined in render_config::color_widgets, and only when
    // render_config::display_widgets is set. This visualizes the widget bounds
    // only; the widget's text value arrives as ordinary text instructions from
    // its appearance stream and is rendered either way.
    void render_widget(text_widget_instruction& instr);

    // Renders one bitmap/image XObject. The method validates the image buffers,
    // converts the source pixels and optional soft mask into a PRGB32 BLImage,
    // applies supported rectangular clipping, and blits either with the
    // axis-aligned fast path or a full affine transform for rotated/skewed quads.
    void render_bitmap(bitmap_instruction& instr);

    // Paints one parsed vector path (all subpaths of one PDF painting
    // operator) in canvas coordinates: fills with the instruction's fill
    // color and fill rule, strokes with its stroking color, width, cap and
    // join parameters, honoring the paint mode (stroke / fill / both) and
    // axis-aligned rectangular clips. Curve segments render as true cubics.
    void render_shape(shape_instruction& instr);

    // Paints one `sh` shading over the current clip region as a Blend2D
    // gradient. The shading's colour ramp arrives pre-sampled, and its
    // geometry is transformed from shading space to canvas space through the
    // gradient's own matrix, so a flipped or skewed CTM stays exact. The clip
    // region is the only bound on the paint, so a non-rectangular clip becomes
    // the filled outline rather than a reason to skip the shading.
    void render_shading(shading_instruction& instr);

    // Returns the rendered canvas as RGBA bytes, row-major top-to-bottom, in
    // display orientation. The associated shape is {height, width, 4}.
    std::shared_ptr<std::vector<uint8_t>> get_canvas() const;

    // Returns the canvas shape in display orientation as {height, width,
    // channels}. Before set_size() this is {0, 0, 4}. This is the shape the
    // canvas has once it is handed out, so for a page rotated by 90 or 270
    // degrees it is already the transpose of the internal drawing canvas.
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
      // Horizontal condensation: the ratio of the cell's width edge to the
      // natural advance of the shaped run. PDF text may be anisotropically
      // scaled (Tz, or a text matrix with sx != sy); a transform built from
      // the up-vector alone draws every glyph at full width, and a condensed
      // newspaper headline turns into overlapping strokes.
      double h_scale = 1.0;
    };

    struct text_draw_adjustment
    {
      BLPoint draw_origin = BLPoint(0.0, 0.0);
      double bbox_fit_scale = 1.0;
      bool has_render_bbox = false;
      BLBox render_bbox{};
    };

    render_config config_;

    mutable pdf_timings timings_;

    mutable BLImage    image_;  // internal canvas (PRGB32 format)
    mutable BLContext  context_;
    mutable bool       context_active_ = false;
    std::array<int, 3> shape_;  // {height, width, 4}, display orientation
    int canvas_width_ = 0;     // internal canvas width (unrotated page space)
    int canvas_height_ = 0;    // internal canvas height (unrotated page space)
    double scale_x_ = 1.0;     // pdf-to-canvas scale along x
    double scale_y_ = 1.0;     // pdf-to-canvas scale along y
    double origin_x_ = 0.0;    // crop_bbox x origin (pdf units)
    double origin_y_ = 0.0;    // crop_bbox y origin (pdf units, y-up)

    // Quarter turns clockwise that take the internal canvas into display
    // orientation, derived from the page's /Rotate. In [0, 3].
    int quarter_turns_ = 0;

    // Guards the one-shot canvas rotation, so that repeated canvas access
    // (get_canvas() followed by save(), for instance) rotates only once.
    mutable bool rotation_applied_ = false;

    std::shared_ptr<blend2d_font_resolver> font_resolver_;
    std::shared_ptr<blend2d_embedded_font_cache> embedded_font_cache_;
    std::shared_ptr<freetype_font_cache> freetype_font_cache_;
    std::shared_ptr<glyph_bbox_cache> glyph_bbox_cache_;

    // Draws SYSTEM font files Blend2D cannot open. Process-wide, unlike
    // freetype_font_cache_: the same handful of system faces serves every
    // page, and re-opening a 30 MB CJK collection per renderer -- and
    // re-decomposing its glyphs -- would be paid on each one.
    std::shared_ptr<freetype_font_cache> system_font_cache_ =
      freetype_font_cache::default_cache();

    // Embedded faces (by blob cache key) whose outlines Blend2D decoded
    // implausibly; every later cell in them is drawn through FreeType so one
    // run is not rasterised by two engines. Scoped to this renderer, which
    // draws one page.
    mutable std::unordered_set<std::string> distrusted_embedded_faces_;
    std::unordered_map<std::string, blend2d_font_resolver::resolved_face>
      local_font_cache_;

    // One spare canvas per thread, handed between the successive per-page
    // renderers that a render worker creates. Function-local so it is
    // initialised on first use and destroyed at thread exit.
    static BLImage& canvas_pool()
    {
      thread_local BLImage pooled;
      return pooled;
    }

    // Returns the active Blend2D context for the page, starting it lazily if
    // necessary. Throws when called before a non-empty canvas has been created.
    BLContext& page_context();

    // Ends the active Blend2D context if one is open and rotates the finished
    // canvas into display orientation. This flushes pending drawing operations
    // before canvas extraction or file output.
    void finish_page_context() const;

    // Rotates the drawing canvas clockwise by quarter_turns_ so that it leaves
    // the renderer in display orientation. Multiples of 90 degrees permute
    // pixels exactly, so this neither resamples nor loses precision. Does
    // nothing for an unrotated page or an already rotated canvas, and must run
    // only after the Blend2D context has been ended.
    void apply_page_rotation() const;

    // Number of quarter turns clockwise for a /Rotate angle, normalized to
    // [0, 3]. Angles that are not a multiple of 90 are not representable in
    // PDF (ISO 32000-1, Table 30) and are reported and treated as unrotated.
    static int quarter_turns_from_angle(int angle);

    // True once set_size() has allocated a canvas to draw on.
    bool has_canvas() const { return canvas_width_ > 0 and canvas_height_ > 0; }

    // Convert PDF coordinates (origin at crop_bbox bottom-left, y-up) to
    // canvas coordinates (origin top-left, y-down), applying scale. These are
    // coordinates on the internal, unrotated canvas.
    double canvas_x(double pdf_x) const { return (pdf_x - origin_x_) * scale_x_; }
    double canvas_y(double pdf_y) const
    {
      return static_cast<double>(canvas_height_) - (pdf_y - origin_y_) * scale_y_;
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

    // Return the face for the given PDF font names, falling back to a system
    // font if none can be resolved. Results are cached. The face inside can
    // be invalid while the path is set: that is a file only FreeType can
    // read, drawn through render_text_freetype_file().
    blend2d_font_resolver::resolved_face resolve_font_face(
      const std::string& font_name,
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

    // How many glyphs of a shaped run came back as .notdef. A run with any of
    // them has characters the face cannot draw, each of which lands on the
    // page as a box.
    static size_t glyph_run_notdef_count(const BLGlyphBuffer& gb)
    {
      const size_t count = gb.size();
      const uint32_t* glyph_ids = gb.glyph_run().glyph_data_as<uint32_t>();
      if (count == 0 or glyph_ids == nullptr) { return 0; }

      size_t notdef = 0;
      for (size_t i = 0; i < count; ++i)
        {
          if (glyph_ids[i] == 0) { ++notdef; }
        }

      return notdef;
    }

    static bool glyph_run_has_notdef(const BLGlyphBuffer& gb)
    {
      return glyph_run_notdef_count(gb) > 0;
    }

    // Blend2D loads some SFNT faces and then decodes part of them into
    // outlines that are orders of magnitude too large: a valid CJK subset
    // (PMingLiU, 1024 units/em, every glyph simple and well formed) rendered
    // eight of its eleven glyphs as page-sized wedges and bars, while
    // FreeType read all eleven correctly at the same size. The face loads,
    // shaping succeeds and the glyph ids are right, so nothing upstream of
    // the outline reports a problem.
    //
    // A glyph cannot be many times its own em, so an outline that overshoots
    // by that much is a failed decode rather than a document asking for
    // something enormous, and the cell is better drawn through FreeType.
    bool glyph_outlines_are_implausible(const BLFont& font,
                                        const BLGlyphBuffer& gb,
                                        double size,
                                        const std::string& face_key)
    {
      const size_t count = gb.size();
      const uint32_t* glyph_ids = gb.glyph_run().glyph_data_as<uint32_t>();
      if (size <= 0.0 or count == 0 or glyph_ids == nullptr) { return false; }

      // Loose on purpose. A stretched delimiter or a swash is a few em; the
      // misdecodes seen here overshoot by one to two orders of magnitude.
      constexpr double max_em_multiple = 8.0;
      const double limit = max_em_multiple * size;

      const BLMatrix2D identity(1.0, 0.0,
                                0.0, 1.0,
                                0.0, 0.0);

      for (size_t l = 0; l < count; ++l)
        {
          BLBox box;
          if (not glyph_bbox_cache_->find(face_key, glyph_ids[l], size, box))
            {
              BLPath path;
              if (font.get_glyph_outlines(glyph_ids[l], identity, path) != BL_SUCCESS ||
                  path.is_empty() ||
                  path.get_bounding_box(&box) != BL_SUCCESS)
                {
                  continue;
                }
              glyph_bbox_cache_->insert(face_key, glyph_ids[l], size, box);
            }

          if ((box.x1 - box.x0) > limit or (box.y1 - box.y0) > limit)
            {
              return true;
            }
        }

      return false;
    }

    // Draws a text cell whose embedded font program Blend2D cannot load
    // (Type 1, bare CFF) by filling FreeType-decomposed outline paths.
    // Returns true when the cell was fully handled (including the optional
    // basepoint/bbox debug drawing); false means the caller should continue
    // with the system font path.
    bool render_text_freetype(text_instruction& instr,
                              const text_geometry& geom,
                              const BLPath& bbox_path);

    // Same, for a SYSTEM font file the resolver selected but Blend2D cannot
    // open -- CFF2 (variable) outlines, which is how Noto CJK looks to it on
    // a distribution that ships only the VF build. Without this the cell has
    // no face at all and the page comes out as .notdef boxes.
    bool render_text_freetype_file(text_instruction& instr,
                                   const text_geometry& geom,
                                   const BLPath& bbox_path,
                                   const std::string& font_path,
                                   uint32_t face_index);

    // The drawing half shared by both: fills (and, for the stroking text
    // render modes, strokes) an already-built glyph path under the cell's
    // text transform, clip state and layer handling.
    bool draw_text_outline_path(text_instruction& instr,
                                const text_geometry& geom,
                                const BLPath& bbox_path,
                                const BLPath& text_path,
                                double h_scale);

    // Glyph-identity mapping by PDF character code against an embedded
    // (Blend2D-loaded) face: CID fonts with an identity CIDToGIDMap use the
    // character code as glyph index directly; simple fonts are tried through
    // the 0xF000 private-use convention ((3,0) symbol cmaps) and then with
    // the raw character code ((1,0) / format-0 builtin cmaps, as written by
    // Cairo/LibreOffice subsetters). Used both as the primary path for
    // symbolic fonts and as recovery when Unicode shaping failed or
    // produced only .notdef. On success gb holds a positioned glyph run
    // ready for fill_glyph_run.
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
          const uint32_t symbol_codepoint = 0xF000u + static_cast<uint32_t>(char_code);
          gb.set_utf32_text(&symbol_codepoint, 1);
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

          const uint32_t raw_codepoint = static_cast<uint32_t>(char_code);
          gb.set_utf32_text(&raw_codepoint, 1);
          if (font.shape(gb) == BL_SUCCESS and
              not gb.is_empty() and
              gb.placement_data() != nullptr and
              not glyph_run_all_notdef(gb))
            {
              LOG_S(INFO) << "render_text: recovered glyph via raw char code"
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

    // Sets geom.h_scale from the cell's width edge and the natural advance of
    // the given run; identity when either is degenerate or nearly equal.
    static void apply_condensation(text_geometry& geom,
                                   const BLFont& font,
                                   BLGlyphBuffer& gb)
    {
      const double wx = geom.bbox.x1 - geom.bbox.x0;
      const double wy = geom.bbox.y1 - geom.bbox.y0;
      const double cell_w = std::hypot(wx, wy);
      if (cell_w <= 0.5) { return; }

      BLTextMetrics tm;
      if (font.get_text_metrics(gb, tm) != BL_SUCCESS) { return; }
      const double natural = tm.advance.x;
      if (natural <= 0.5) { return; }

      const double h = cell_w / natural;
      // Only meaningful, bounded deviations: runaway ratios mean the metrics
      // and the cell disagree for other reasons (fallback face substitution).
      if (h >= 0.30 and h <= 3.0 and std::abs(h - 1.0) > 0.03)
        {
          geom.h_scale = h;
        }
    }

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
      double size,
      const std::string* face_key);

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

    enum clip_apply_result
    {
      CLIP_NONE,
      CLIP_APPLIED,
      CLIP_EMPTY,
    };

    // Converts a parsed rectangular clip path into a canvas-space BLRect when
    // all clip vertices lie on the rectangle edges. Non-rectangular, degenerate,
    // or unsupported clip paths return false so callers can skip them without
    // corrupting the Blend2D clip state.
    //
    // The detection is purely geometric (every vertex on the bbox edge):
    // most rectangular clips are built with m/l/l/l/h rather than `re`, so
    // gating on shape_type == RECTANGLE would reject them.
    bool get_axis_aligned_clip_rect(const clip_path_instruction& clip,
                                    BLRect& rect) const
    {
      if(clip.size() < 4)
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

    // Builds a canvas-space BLPath from a clip path's flattened polyline.
    static BLPath make_clip_path(const clip_path_instruction& clip_path,
                                 const renderer<BLEND2D>* self)
    {
      BLPath path;
      const std::vector<double>& xs = clip_path.get_x();
      const std::vector<double>& ys = clip_path.get_y();
      const size_t n = std::min(xs.size(), ys.size());
      if(n < 3) { return path; }

      path.move_to(self->canvas_x(xs[0]), self->canvas_y(ys[0]));
      for(size_t i = 1; i < n; i++)
        {
          path.line_to(self->canvas_x(xs[i]), self->canvas_y(ys[i]));
        }
      path.close();
      return path;
    }

    // Multiplies `dst`'s coverage by `src`'s, pixel by pixel.
    //
    // The obvious BL_COMP_OP_DST_IN spelling does not work here: Blend2D only
    // composites where the source has coverage, so the pixels that need to be
    // *cleared* are exactly the ones it skips. Touching the bytes directly is
    // both correct and cheaper than a full-surface fill.
    static bool multiply_a8(BLImage& dst, const BLImage& src)
    {
      BLImageData d, s;
      if(dst.make_mutable(&d) != BL_SUCCESS or src.get_data(&s) != BL_SUCCESS)
        {
          return false;
        }
      if(d.format != BL_FORMAT_A8 or s.format != BL_FORMAT_A8) { return false; }
      if(d.size.w != s.size.w or d.size.h != s.size.h) { return false; }
      if(d.size.w <= 0 or d.size.h <= 0) { return false; }
      if(d.stride < d.size.w or s.stride < s.size.w) { return false; }

      for(int y = 0; y < d.size.h; y++)
        {
          uint8_t* drow = static_cast<uint8_t*>(d.pixel_data) + y * d.stride;
          const uint8_t* srow =
            static_cast<const uint8_t*>(s.pixel_data) + y * s.stride;
          for(int x = 0; x < d.size.w; x++)
            {
              drow[x] = static_cast<uint8_t>((drow[x] * srow[x] + 127) / 255);
            }
        }
      return true;
    }

    // Same, for a premultiplied colour layer: every channel scales with the
    // coverage so the result stays premultiplied.
    static bool multiply_prgb32_by_a8(BLImage& layer, const BLImage& mask)
    {
      BLImageData d, s;
      if(layer.make_mutable(&d) != BL_SUCCESS or mask.get_data(&s) != BL_SUCCESS)
        {
          return false;
        }
      if(d.format != BL_FORMAT_PRGB32 or s.format != BL_FORMAT_A8) { return false; }
      if(d.size.w != s.size.w or d.size.h != s.size.h) { return false; }
      if(d.size.w <= 0 or d.size.h <= 0) { return false; }
      if(d.stride < static_cast<intptr_t>(d.size.w) * 4 or
         s.stride < s.size.w)
        {
          return false;
        }

      for(int y = 0; y < d.size.h; y++)
        {
          uint8_t* drow = static_cast<uint8_t*>(d.pixel_data) + y * d.stride;
          const uint8_t* srow =
            static_cast<const uint8_t*>(s.pixel_data) + y * s.stride;
          for(int x = 0; x < d.size.w; x++)
            {
              const uint32_t m = srow[x];
              if(m == 255) { continue; }
              uint8_t* px = drow + 4 * x;
              for(int c = 0; c < 4; c++)
                {
                  px[c] = static_cast<uint8_t>((px[c] * m + 127) / 255);
                }
            }
        }
      return true;
    }

    static bool fill_a8(BLImage& image, uint8_t value)
    {
      BLImageData d;
      if(image.make_mutable(&d) != BL_SUCCESS) { return false; }
      if(d.format != BL_FORMAT_A8) { return false; }
      if(d.size.w <= 0 or d.size.h <= 0) { return false; }
      if(d.stride < d.size.w) { return false; }

      for(int y = 0; y < d.size.h; y++)
        {
          uint8_t* row = static_cast<uint8_t*>(d.pixel_data) + y * d.stride;
          std::fill_n(row, d.size.w, value);
        }
      return true;
    }

    static bool copy_prgb32_alpha_to_a8(BLImage& dst, const BLImage& src)
    {
      BLImageData d, s;
      if(dst.make_mutable(&d) != BL_SUCCESS or src.get_data(&s) != BL_SUCCESS)
        {
          return false;
        }
      if(d.format != BL_FORMAT_A8 or s.format != BL_FORMAT_PRGB32) { return false; }
      if(d.size.w != s.size.w or d.size.h != s.size.h) { return false; }
      if(d.size.w <= 0 or d.size.h <= 0) { return false; }
      if(d.stride < d.size.w or
         s.stride < static_cast<intptr_t>(s.size.w) * 4)
        {
          return false;
        }

      for(int y = 0; y < d.size.h; y++)
        {
          uint8_t* drow = static_cast<uint8_t*>(d.pixel_data) + y * d.stride;
          const uint8_t* srow =
            static_cast<const uint8_t*>(s.pixel_data) + y * s.stride;
          for(int x = 0; x < d.size.w; x++)
            {
              drow[x] = srow[4 * x + 3];
            }
        }
      return true;
    }

    static bool rasterize_path_to_a8(BLImage& dst,
                                     const BLPath& path,
                                     BLFillRule fill_rule)
    {
      BLImageData d;
      if(dst.get_data(&d) != BL_SUCCESS) { return false; }
      if(d.format != BL_FORMAT_A8) { return false; }
      if(d.size.w <= 0 or d.size.h <= 0) { return false; }

      BLImage plane;
      if(plane.create(d.size.w, d.size.h, BL_FORMAT_PRGB32) != BL_SUCCESS)
        {
          return false;
        }

      {
        BLContext pctx(plane);
        pctx.set_comp_op(BL_COMP_OP_SRC_COPY);
        pctx.fill_all(BLRgba32(0x00000000u));
        pctx.set_comp_op(BL_COMP_OP_SRC_OVER);
        pctx.set_fill_rule(fill_rule);
        pctx.fill_path(path, BLRgba32(0xFFFFFFFFu));
        pctx.end();
      }

      return copy_prgb32_alpha_to_a8(dst, plane);
    }

    bool clip_path_canvas_bbox(const clip_path_instruction& clip,
                               BLRect& rect) const
    {
      const auto& xs = clip.get_x();
      const auto& ys = clip.get_y();
      const size_t n = clip.size();
      if(n < 3) { return false; }

      double x_min = std::numeric_limits<double>::infinity();
      double y_min = std::numeric_limits<double>::infinity();
      double x_max = -std::numeric_limits<double>::infinity();
      double y_max = -std::numeric_limits<double>::infinity();

      for(size_t i = 0; i < n; i++)
        {
          const double x = canvas_x(xs[i]);
          const double y = canvas_y(ys[i]);
          if(not std::isfinite(x) or not std::isfinite(y)) { return false; }
          x_min = std::min(x_min, x);
          y_min = std::min(y_min, y);
          x_max = std::max(x_max, x);
          y_max = std::max(y_max, y);
        }

      if(x_max <= x_min or y_max <= y_min) { return false; }
      rect = BLRect(x_min, y_min, x_max - x_min, y_max - y_min);
      return true;
    }

    // True when the clip carries a path clip_to_rect cannot express. Such a
    // clip is applied through a coverage mask instead; the rectangular paths
    // beside it still go through the cheap context clip.
    bool clip_state_has_non_rect(const clip_state_instruction& clip_state) const
    {
      if(not clip_state.has_clip()) { return false; }

      std::unordered_map<int, int> group_sizes;
      for(const auto& clip_path : clip_state.get_paths())
        {
          group_sizes[clip_path.get_clip_group()]++;
        }

      for(const auto& clip_path : clip_state.get_paths())
        {
          // Subpaths of one W/W* operation only mean anything together (one
          // path under the clip's fill rule), which clip_to_rect cannot
          // express even when each subpath happens to be rectangular.
          if(group_sizes[clip_path.get_clip_group()] > 1) { return true; }

          BLRect unused;
          if(not get_axis_aligned_clip_rect(clip_path, unused)) { return true; }
        }
      return false;
    }

    // Integer canvas-space window covering `r`, clamped to the page.
    BLRectI canvas_clamped_rect(const BLRect& r) const
    {
      const int x0 = std::max(0, static_cast<int>(std::floor(r.x)));
      const int y0 = std::max(0, static_cast<int>(std::floor(r.y)));
      const int x1 = std::min(canvas_width_,  static_cast<int>(std::ceil(r.x + r.w)));
      const int y1 = std::min(canvas_height_, static_cast<int>(std::ceil(r.y + r.h)));
      return BLRectI(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
    }

    // Rasterises a non-rectangular clip into an A8 coverage mask covering
    // `area`, or returns an invalid image when the clip is rectangular (the
    // cheap clip_to_rect path handles those) or cannot be built.
    //
    // Blend2D clips only to rectangles, so a curved clip previously degraded
    // to its bounding box: a crescent came out as the box around it. A mask
    // plus fill_mask expresses the real shape.
    struct clip_mask_result
    {
      BLImage image;
      bool empty_clip = false;
    };

    clip_mask_result build_clip_mask(const clip_state_instruction& clip_state,
                                     const BLRectI& area) const
    {
      clip_mask_result result;
      BLImage mask;
      // LOG_S(INFO) << "build_clip_mask: begin"
      //             << " area=(" << area.x << ", " << area.y
      //             << ", " << area.w << ", " << area.h << ")"
      //             << " paths=" << clip_state.get_paths().size()
      //             << " rule=" << static_cast<int>(clip_state.get_rule());
      if(area.w <= 0 or area.h <= 0)
        {
          // LOG_S(INFO) << "build_clip_mask: empty area";
          result.empty_clip = true;
          return result;
        }
      if(area.w > 8192 or area.h > 8192)
        {
          LOG_S(WARNING) << "build_clip_mask: area too large, skipping";
          return result;
        }

      // LOG_S(INFO) << "build_clip_mask: creating A8 mask";
      const BLResult mask_create = mask.create(area.w, area.h, BL_FORMAT_A8);
      // LOG_S(INFO) << "build_clip_mask: mask.create result=" << mask_create;
      if(mask_create != BL_SUCCESS)
        {
          return result;
        }

      const BLFillRule clip_fill_rule =
        clip_state.get_rule() == CLIP_RULE_EVEN_ODD ? BL_FILL_RULE_EVEN_ODD
                                                    : BL_FILL_RULE_NON_ZERO;

      // LOG_S(INFO) << "build_clip_mask: creating mask context";
      // LOG_S(INFO) << "build_clip_mask: clearing mask";
      if(not fill_a8(mask, 0))
        {
          LOG_S(WARNING) << "build_clip_mask: could not clear A8 mask";
          return result;
        }
      // LOG_S(INFO) << "build_clip_mask: mask cleared";

      // One W/W* capture = one clip path, whatever its subpath count; the
      // subpaths compose under the clip's fill rule (a stencil of outline
      // art, a text-shaped clip). Only DIFFERENT captures intersect.
      // Intersecting the subpaths individually made any such stencil empty,
      // and everything filled through it vanished.
      std::unordered_map<int, BLRect> group_bboxes;
      std::unordered_map<int, bool> group_needs_mask;
      for(const auto& clip_path : clip_state.get_paths())
        {
          BLRect rect;
          const bool is_rect = get_axis_aligned_clip_rect(clip_path, rect);
          const int group = clip_path.get_clip_group();
          if(not is_rect) { group_needs_mask[group] = true; }

          BLRect bbox;
          if(clip_path_canvas_bbox(clip_path, bbox))
            {
              auto it = group_bboxes.find(group);
              if(it == group_bboxes.end())
                { group_bboxes.emplace(group, bbox); }
              else
                {
                  const double x0 = std::min(it->second.x, bbox.x);
                  const double y0 = std::min(it->second.y, bbox.y);
                  const double x1 =
                    std::max(it->second.x + it->second.w, bbox.x + bbox.w);
                  const double y1 =
                    std::max(it->second.y + it->second.h, bbox.y + bbox.h);
                  it->second = BLRect(x0, y0, x1 - x0, y1 - y0);
                }
            }
        }

      const BLRect area_rect(area.x, area.y, area.w, area.h);
      for(const auto& entry : group_needs_mask)
        {
          const auto bbox_it = group_bboxes.find(entry.first);
          if(bbox_it == group_bboxes.end() or
             not rects_intersect(bbox_it->second, area_rect))
            {
              result.empty_clip = true;
              return result;
            }
        }

      std::vector<std::pair<int, BLPath> > group_paths;
      [[maybe_unused]] size_t clip_index = 0;
      for(const auto& clip_path : clip_state.get_paths())
        {
          BLRect unused;
          const bool is_rect = get_axis_aligned_clip_rect(clip_path, unused);
          // LOG_S(INFO) << "build_clip_mask: path " << clip_index
          //             << " group=" << clip_path.get_clip_group()
          //             << " points=" << clip_path.size()
          //             << " is_rect=" << (is_rect ? "true" : "false");

          // rect paths that are alone in their group were already applied
          // through the context clip
          bool grouped = false;
          for(const auto& other : clip_state.get_paths())
            {
              if(&other != &clip_path and
                 other.get_clip_group() == clip_path.get_clip_group())
                {
                  grouped = true;
                  break;
                }
            }
          if(is_rect and not grouped)
            {
              // LOG_S(INFO) << "build_clip_mask: path " << clip_index
              //             << " skipped, rectangle already clipped by context";
              clip_index++;
              continue;
            }

          BLRect clip_bbox;
          if(not clip_path_canvas_bbox(clip_path, clip_bbox))
            {
              LOG_S(WARNING) << "build_clip_mask: skipping invalid clip path";
              clip_index++;
              continue;
            }
          // LOG_S(INFO) << "build_clip_mask: path " << clip_index
          //             << " bbox=(" << clip_bbox.x << ", " << clip_bbox.y
          //             << ", " << clip_bbox.w << ", " << clip_bbox.h << ")";
          if(not rects_intersect(clip_bbox, BLRect(area.x, area.y, area.w, area.h)))
            {
              // LOG_S(INFO) << "build_clip_mask: path " << clip_index
              //             << " outside mask area";
              clip_index++;
              continue;
            }

          // LOG_S(INFO) << "build_clip_mask: path " << clip_index
          //             << " make_clip_path begin";
          BLPath sub = make_clip_path(clip_path, this);
          // LOG_S(INFO) << "build_clip_mask: path " << clip_index
          //             << " make_clip_path done empty="
          //             << (sub.is_empty() ? "true" : "false");
          if(sub.is_empty())
            {
              clip_index++;
              continue;
            }

          BLPath* dst = nullptr;
          for(auto& gp : group_paths)
            {
              if(gp.first == clip_path.get_clip_group()) { dst = &gp.second; break; }
            }
          if(dst == nullptr)
            {
              group_paths.emplace_back(clip_path.get_clip_group(), BLPath());
              dst = &group_paths.back().second;
            }
          dst->add_path(sub);
          // LOG_S(INFO) << "build_clip_mask: path " << clip_index
          //             << " added to group";
          clip_index++;
        }

      bool first = true;
      std::vector<BLImage> pending_planes;
      [[maybe_unused]] size_t group_index = 0;
      for(auto& gp : group_paths)
        {
          // LOG_S(INFO) << "build_clip_mask: raster group " << group_index
          //             << " group_id=" << gp.first;
          BLPath shifted;
          // LOG_S(INFO) << "build_clip_mask: group " << group_index
          //             << " shifting path";
          shifted.add_path(gp.second, BLMatrix2D::make_translation(-area.x, -area.y));
          // LOG_S(INFO) << "build_clip_mask: group " << group_index
          //             << " shifted";

          if(first)
            {
              // LOG_S(INFO) << "build_clip_mask: group " << group_index
              //             << " filling base mask";
              if(not rasterize_path_to_a8(mask, shifted, clip_fill_rule))
                {
                  LOG_S(WARNING) << "build_clip_mask: could not rasterize base mask";
                  group_index++;
                  continue;
                }
              // LOG_S(INFO) << "build_clip_mask: group " << group_index
              //             << " base mask filled";
              first = false;
              group_index++;
              continue;
            }

          // Rasterise this capture on its own, then fold it in by multiplying.
          BLImage plane;
          // LOG_S(INFO) << "build_clip_mask: group " << group_index
          //             << " creating intersection plane";
          const BLResult plane_create = plane.create(area.w, area.h, BL_FORMAT_A8);
          // LOG_S(INFO) << "build_clip_mask: group " << group_index
          //             << " plane.create result=" << plane_create;
          if(plane_create != BL_SUCCESS)
            {
              group_index++;
              continue;
            }
          {
            // LOG_S(INFO) << "build_clip_mask: group " << group_index
            //             << " filling plane";
            if(not rasterize_path_to_a8(plane, shifted, clip_fill_rule))
              {
                LOG_S(WARNING) << "build_clip_mask: could not rasterize plane";
                group_index++;
                continue;
              }
            // LOG_S(INFO) << "build_clip_mask: group " << group_index
            //             << " plane filled";
          }
          pending_planes.push_back(std::move(plane));
          group_index++;
        }

      [[maybe_unused]] size_t plane_index = 0;
      for(const BLImage& plane : pending_planes)
        {
          // LOG_S(INFO) << "build_clip_mask: multiplying plane " << plane_index;
          if(not multiply_a8(mask, plane))
            {
              LOG_S(WARNING) << "build_clip_mask: could not multiply plane";
            }
          // LOG_S(INFO) << "build_clip_mask: plane " << plane_index
          //             << " multiply result=" << (ok ? "true" : "false");
          plane_index++;
        }

      // No usable non-rectangular geometry: let the caller draw normally.
      if(first)
        {
          // LOG_S(INFO) << "build_clip_mask: no usable geometry";
          return result;
        }

      // LOG_S(INFO) << "build_clip_mask: done";
      result.image = std::move(mask);
      return result;
    }

    // Applies an instruction's clip paths (bitmap or shape) to the Blend2D
    // context. Only axis-aligned rectangular clips are supported. The return
    // value tells the caller whether no clip was present, a clip was applied,
    // or the destination is fully outside the clip and rendering can be
    // skipped.
    clip_apply_result apply_clip_state(
      BLContext& ctx,
      const clip_state_instruction& clip_state,
      const BLRect& dst_rect) const
    {
      if(not clip_state.has_clip())
        {
          return CLIP_NONE;
        }

      std::unordered_map<int, int> group_sizes;
      for(const auto& clip_path : clip_state.get_paths())
        {
          group_sizes[clip_path.get_clip_group()]++;
        }

      bool applied_clip = false;
      for(const auto& clip_path : clip_state.get_paths())
        {
          // A subpath that shares its W/W* capture with others is not a clip
          // on its own; the whole group is handled through the coverage mask.
          if(group_sizes[clip_path.get_clip_group()] > 1) { continue; }

          BLRect clip_rect;
          if(get_axis_aligned_clip_rect(clip_path, clip_rect))
            {
              if(not rects_intersect(clip_rect, dst_rect))
                {
                  LOG_S(INFO) << "apply_clip_state: destination fully clipped"
                              << " clip=(" << clip_rect.x << ", " << clip_rect.y
                              << ", " << clip_rect.w << ", " << clip_rect.h << ")"
                              << " dst=(" << dst_rect.x << ", " << dst_rect.y
                              << ", " << dst_rect.w << ", " << dst_rect.h << ")";
                  return CLIP_EMPTY;
                }

              ctx.clip_to_rect(clip_rect);
              applied_clip = true;
            }
          else
            {
              // Handled by the caller through a coverage mask (build_clip_mask).
              LOG_S(INFO) << "apply_clip_state: non-rectangular clip path deferred to mask";
            }
        }

      return applied_clip ? CLIP_APPLIED : CLIP_NONE;
    }

    // Builds a closed canvas-space path from a clip path. The parse layer
    // stores clip geometry as a flattened polyline, so curved clip outlines
    // arrive already sampled. A fill treats every subpath as closed (8.5.3.2),
    // hence the unconditional close().
    BLPath make_clip_path(const clip_path_instruction& clip) const
    {
      BLPath path;

      const auto& xs = clip.get_x();
      const auto& ys = clip.get_y();
      const size_t n = clip.size();

      if(n < 3)
        {
          return path;
        }

      path.move_to(canvas_x(xs[0]), canvas_y(ys[0]));
      for(size_t i = 1; i < n; i++)
        {
          path.line_to(canvas_x(xs[i]), canvas_y(ys[i]));
        }
      path.close();

      return path;
    }

    // What a shading should paint once its clip state has been applied.
    struct shading_clip
    {
      bool   empty = false;    // the clip leaves no area on the canvas
      bool   bounded = false;  // at least one clip path could be honored
      bool   has_path = false; // fill `path` instead of the whole canvas
      BLPath path;
    };

    // Restricts `ctx` to a shading's clip region and reports the geometry to
    // fill. `sh` paints the entire clip region, so the clip is the only thing
    // bounding the paint. Blend2D clips to rectangles only, so a rectangular
    // clip path becomes a context clip while a non-rectangular one becomes the
    // fill geometry itself -- filling its outline covers exactly the region the
    // operator reaches. Additional non-rectangular paths can only be
    // approximated, and are applied as their bounding box so the paint stays
    // bounded.
    //
    // The caller must have saved `ctx` beforehand: this leaves clips on it.
    shading_clip apply_shading_clip(BLContext& ctx,
                                    const clip_state_instruction& clip_state,
                                    const BLRect& canvas_rect) const
    {
      shading_clip result;

      for(const auto& clip_path : clip_state.get_paths())
        {
          BLRect clip_rect;
          if(get_axis_aligned_clip_rect(clip_path, clip_rect))
            {
              if(not rects_intersect(clip_rect, canvas_rect))
                {
                  result.empty = true;
                  return result;
                }

              ctx.clip_to_rect(clip_rect);
              result.bounded = true;
              continue;
            }

          BLPath path = make_clip_path(clip_path);
          if(path.is_empty())
            {
              LOG_S(WARNING) << "apply_shading_clip: skipping degenerate clip path";
              continue;
            }

          if(not result.has_path)
            {
              result.path = std::move(path);
              result.has_path = true;
              result.bounded = true;
              continue;
            }

          // several non-rectangular paths: their intersection is not
          // expressible, so bound the fill by this one's bounding box
          BLBox box;
          if(path.get_bounding_box(&box) == BL_SUCCESS)
            {
              const BLRect box_rect(box.x0, box.y0,
                                    box.x1 - box.x0, box.y1 - box.y0);

              if(not rects_intersect(box_rect, canvas_rect))
                {
                  result.empty = true;
                  return result;
                }

              ctx.clip_to_rect(box_rect);
              result.bounded = true;
            }

          LOG_S(WARNING) << "apply_shading_clip: more than one non-rectangular"
                         << " clip path, approximating the extra one by its"
                         << " bounding box";
        }

      return result;
    }

    // Converts one CMYK image sample to sRGB. `CMYK_CONVENTION_ADOBE_INVERTED`
    // is how Adobe writes CMYK into a JPEG: the bytes hold 255 minus the ink
    // amount, so the only difference from the process convention is how the
    // four bytes are read.
    static std::array<int, 3> cmyk_bytes_to_rgb_uncached(uint8_t c, uint8_t m,
                                                         uint8_t y, uint8_t k,
                                                         cmyk_convention convention)
    {
      const double ink[4] = {c / 255.0, m / 255.0, y / 255.0, k / 255.0};

      // CMYK samples are ink values unless something positively established
      // the Adobe-inverted convention. Treating "unknown" as inverted made
      // the default a silent negation, which every decode path then had to
      // cancel by luck.
      if (convention == CMYK_CONVENTION_ADOBE_INVERTED)
        {
          return color::cmyk_to_rgb255(1.0 - ink[0], 1.0 - ink[1],
                                       1.0 - ink[2], 1.0 - ink[3]);
        }

      return color::cmyk_to_rgb255(ink[0], ink[1], ink[2], ink[3]);
    }

    // color::cmyk_to_rgb() costs nine pow() calls, and render_bitmap() calls
    // this once per pixel: profiling put it at ~31% of all rendering time, at
    // ~120 ns per pixel. The conversion is a pure function of the four sample
    // bytes and the convention, so memoise it -- a hit returns exactly what
    // recomputing would, which is why this cannot change a rendered pixel.
    //
    // Direct-mapped and thread_local: no synchronisation, no sharing between
    // render workers, and a fixed 48 KB per thread that stays inside L2. Flat
    // regions and limited palettes (the common case in print PDFs) hit almost
    // always; fully photographic CMYK falls back to the arithmetic at no
    // measurable extra cost.
    static std::array<int, 3> cmyk_bytes_to_rgb(uint8_t c, uint8_t m,
                                                uint8_t y, uint8_t k,
                                                cmyk_convention convention)
    {
      constexpr uint32_t CACHE_SIZE = 1u << 12;
      // no real key can collide with this: the payload is only 33 bits wide
      constexpr uint64_t CACHE_EMPTY = ~uint64_t(0);

      thread_local std::vector<uint64_t> memo_key(CACHE_SIZE, CACHE_EMPTY);
      thread_local std::vector<uint32_t> memo_rgb(CACHE_SIZE, 0);

      const uint64_t key =
        (uint64_t(convention == CMYK_CONVENTION_ADOBE_INVERTED) << 32) |
        (uint64_t(c) << 24) | (uint64_t(m) << 16) |
        (uint64_t(y) <<  8) |  uint64_t(k);

      uint32_t slot = static_cast<uint32_t>((key * 0x9E3779B97F4A7C15ull) >> 40)
                      & (CACHE_SIZE - 1);

      if (memo_key[slot] == key)
        {
          const uint32_t v = memo_rgb[slot];
          return {int((v >> 16) & 0xFFu), int((v >> 8) & 0xFFu), int(v & 0xFFu)};
        }

      const std::array<int, 3> rgb =
        cmyk_bytes_to_rgb_uncached(c, m, y, k, convention);

      // The slot packs three bytes, so only cache a result that survives the
      // round trip. linear_to_srgb() clamps to [0, 1] and the caller rounds,
      // so this holds today for every input; the guard keeps the cache exact
      // rather than merely correct-by-inspection if that ever changes.
      const bool storable =
        rgb[0] >= 0 and rgb[0] <= 255 and
        rgb[1] >= 0 and rgb[1] <= 255 and
        rgb[2] >= 0 and rgb[2] <= 255;

      if (storable)
        {
          memo_key[slot] = key;
          memo_rgb[slot] = (uint32_t(rgb[0]) << 16) |
                           (uint32_t(rgb[1]) <<  8) |
                            uint32_t(rgb[2]);
        }

      return rgb;
    }

    // Maps an ExtGState /BM to the Blend2D compositing operator (11.3.5,
    // Table 136). The four non-separable modes of Table 137 -- Hue,
    // Saturation, Color and Luminosity -- have no Blend2D counterpart and fall
    // back to Normal, which is what they degrade to anyway when a reader does
    // not implement them.
    static BLCompOp to_comp_op(blend_mode_name mode)
    {
      switch(mode)
        {
        case BLEND_MODE_MULTIPLY:    { return BL_COMP_OP_MULTIPLY; }
        case BLEND_MODE_SCREEN:      { return BL_COMP_OP_SCREEN; }
        case BLEND_MODE_OVERLAY:     { return BL_COMP_OP_OVERLAY; }
        case BLEND_MODE_DARKEN:      { return BL_COMP_OP_DARKEN; }
        case BLEND_MODE_LIGHTEN:     { return BL_COMP_OP_LIGHTEN; }
        case BLEND_MODE_COLOR_DODGE: { return BL_COMP_OP_COLOR_DODGE; }
        case BLEND_MODE_COLOR_BURN:  { return BL_COMP_OP_COLOR_BURN; }
        case BLEND_MODE_HARD_LIGHT:  { return BL_COMP_OP_HARD_LIGHT; }
        case BLEND_MODE_SOFT_LIGHT:  { return BL_COMP_OP_SOFT_LIGHT; }
        case BLEND_MODE_DIFFERENCE:  { return BL_COMP_OP_DIFFERENCE; }
        case BLEND_MODE_EXCLUSION:   { return BL_COMP_OP_EXCLUSION; }

        default: { return BL_COMP_OP_SRC_OVER; }
        }
    }

    // Sets the compositing operator for one painting operation and reports
    // whether the context has to be restored afterwards. Blend2D holds the
    // operator on the context, so anything but Normal is bracketed by
    // save/restore rather than left behind for the next instruction.
    static bool push_blend_mode(BLContext& ctx, blend_mode_name mode)
    {
      const BLCompOp comp_op = to_comp_op(mode);
      if(comp_op == BL_COMP_OP_SRC_OVER)
        {
          return false;
        }

      ctx.save();
      ctx.set_comp_op(comp_op);
      return true;
    }

    // Scoped form of push_blend_mode(), for painting paths that leave through
    // more than one exit.
    class blend_mode_scope
    {
    public:

      blend_mode_scope(BLContext& ctx, blend_mode_name mode);
      ~blend_mode_scope();

      blend_mode_scope(const blend_mode_scope&) = delete;
      blend_mode_scope& operator=(const blend_mode_scope&) = delete;

    private:

      BLContext& ctx_;
      bool       active_;
    };

    // Scoped ctx.save()/ctx.restore().
    //
    // A clip is pushed onto the context with a save and has to come off again
    // on EVERY exit. One early return that forgot its restore does not just
    // lose that instruction: the save stays on the context, every later
    // restore pops somebody else's state, and the clip that should have been
    // undone keeps narrowing what the rest of the page is allowed to paint.
    // A page of pattern cells, whose lattice deliberately steps past the
    // filled path, leaks one save per off-target cell and ends up painting
    // almost nothing. Tying the save to a scope makes the balance structural.
    class context_state_scope
    {
    public:

      // `active` false constructs a scope that saves nothing.
      explicit context_state_scope(BLContext& ctx, bool active = true);
      ~context_state_scope();

      // Restores now rather than at the end of the scope; the destructor then
      // does nothing. Calling it twice is harmless.
      void restore();

      context_state_scope(const context_state_scope&) = delete;
      context_state_scope& operator=(const context_state_scope&) = delete;

    private:

      BLContext& ctx_;
      bool       active_;
    };

    // Converts a parsed 0-255 RGB triple and a [0, 1] alpha into a Blend2D
    // color (straight alpha; the context premultiplies while compositing).
    static BLRgba32 make_rgba32(const std::array<int, 3>& rgb,
                                double alpha = 1.0)
    {
      const double clamped = std::min(1.0, std::max(0.0, alpha));
      const uint32_t a = static_cast<uint32_t>(std::lround(255.0 * clamped));
      return BLRgba32((a                              << 24) |
                      (static_cast<uint32_t>(rgb[0])  << 16) |
                      (static_cast<uint32_t>(rgb[1])  <<  8) |
                       static_cast<uint32_t>(rgb[2]));
    }

    // Maps the PDF line cap value (0 butt, 1 round, 2 projecting square) to
    // Blend2D. The numeric values differ (Blend2D: square=1, round=2).
    static BLStrokeCap to_stroke_cap(int line_cap)
    {
      switch(line_cap)
        {
        case 1:  return BL_STROKE_CAP_ROUND;
        case 2:  return BL_STROKE_CAP_SQUARE;
        default: return BL_STROKE_CAP_BUTT;
        }
    }

    // Maps the PDF line join value (0 miter, 1 round, 2 bevel) to Blend2D.
    // PDF miter joins fall back to bevel when the miter limit is exceeded,
    // which is exactly BL_STROKE_JOIN_MITER_BEVEL.
    static BLStrokeJoin to_stroke_join(int line_join)
    {
      switch(line_join)
        {
        case 1:  return BL_STROKE_JOIN_ROUND;
        case 2:  return BL_STROKE_JOIN_BEVEL;
        default: return BL_STROKE_JOIN_MITER_BEVEL;
        }
    }

    // Builds the Blend2D path for a shape instruction in canvas coordinates,
    // walking the exact segment ops (true cubics for curves), and returns
    // the canvas-space bounding rectangle of all touched points. Control
    // points give a conservative bbox (a Bézier never leaves its control
    // polygon's hull), which is what the clip skip-test needs.
    BLPath make_shape_path(const shape_instruction& instr, BLRect& bbox) const;

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
                               bool use_soft_mask_alpha,
                               int out_w,
                               int out_h) const;

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
      freetype_font_cache_(std::make_shared<freetype_font_cache>()),
      glyph_bbox_cache_(std::make_shared<glyph_bbox_cache>(config_.glyph_bbox_cache_capacity))
  {}

  inline renderer<BLEND2D>::renderer(render_config config)
    : config_(config),
      shape_({0, 0, 4}),
      font_resolver_(blend2d_font_resolver::default_resolver()),
      embedded_font_cache_(std::make_shared<blend2d_embedded_font_cache>()),
      freetype_font_cache_(std::make_shared<freetype_font_cache>()),
      glyph_bbox_cache_(std::make_shared<glyph_bbox_cache>(config_.glyph_bbox_cache_capacity))
  {}

  inline renderer<BLEND2D>::renderer(render_config config,
                                     std::shared_ptr<blend2d_font_resolver> font_resolver)
    : config_(config),
      shape_({0, 0, 4}),
      font_resolver_(font_resolver ? std::move(font_resolver)
                                   : blend2d_font_resolver::default_resolver()),
      embedded_font_cache_(std::make_shared<blend2d_embedded_font_cache>()),
      freetype_font_cache_(std::make_shared<freetype_font_cache>()),
      glyph_bbox_cache_(std::make_shared<glyph_bbox_cache>(config_.glyph_bbox_cache_capacity))
  {}

  inline renderer<BLEND2D>::renderer(render_config config,
                                     std::shared_ptr<blend2d_font_resolver> font_resolver,
                                     std::shared_ptr<blend2d_embedded_font_cache> embedded_font_cache,
                                     std::shared_ptr<freetype_font_cache> embedded_freetype_cache,
                                     std::shared_ptr<glyph_bbox_cache> glyph_bbox_cache)
    : config_(config),
      shape_({0, 0, 4}),
      font_resolver_(font_resolver ? std::move(font_resolver)
                                   : blend2d_font_resolver::default_resolver()),
      embedded_font_cache_(embedded_font_cache
                             ? std::move(embedded_font_cache)
                             : std::make_shared<blend2d_embedded_font_cache>()),
      freetype_font_cache_(embedded_freetype_cache
                             ? std::move(embedded_freetype_cache)
                             : std::make_shared<freetype_font_cache>()),
      glyph_bbox_cache_(glyph_bbox_cache
                          ? std::move(glyph_bbox_cache)
                          : std::make_shared<pdflib::glyph_bbox_cache>(
                              config_.glyph_bbox_cache_capacity))
  {}

  inline renderer<BLEND2D>::~renderer()
  {
    // Hand the page canvas back for the next renderer on this thread. The
    // context must be closed first, or Blend2D would still hold a reference
    // and the buffer would be copied instead of reused.
    if (context_active_)
      {
        context_.end();
        context_active_ = false;
      }

    if (image_.width() > 0 and image_.height() > 0)
      {
        canvas_pool() = std::move(image_);
        image_.reset();
      }
  }

  inline BLContext& renderer<BLEND2D>::page_context()
  {
    if (context_active_)
      {
        return context_;
      }

    if (not has_canvas())
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
    if (context_active_)
      {
        const BLResult err = context_.end();
        context_active_ = false;
        if (err != BL_SUCCESS)
          {
            throw std::runtime_error(
              "renderer<BLEND2D>::finish_page_context: failed to end Blend2D context "
              "(BLResult=" + std::to_string(err) + ")");
          }
      }

    apply_page_rotation();
  }

  inline int renderer<BLEND2D>::quarter_turns_from_angle(int angle)
  {
    int normalised = angle % 360;
    if (normalised < 0)
      {
        normalised += 360;
      }

    if ((normalised % 90) != 0)
      {
        LOG_S(ERROR) << "the /Rotate angle should be a multiple of 90, got: "
                     << angle << " -> rendering the page unrotated";
        return 0;
      }

    return normalised / 90;
  }

  inline void renderer<BLEND2D>::apply_page_rotation() const
  {
    if (rotation_applied_ or quarter_turns_ == 0 or image_.is_empty())
      {
        return;
      }

    rotation_applied_ = true;

    const int src_w = image_.width();
    const int src_h = image_.height();
    if (src_w <= 0 or src_h <= 0)
      {
        return;
      }

    BLImageData src_data;
    const BLResult src_res = image_.get_data(&src_data);
    if (src_res != BL_SUCCESS)
      {
        throw std::runtime_error(
          "renderer<BLEND2D>::apply_page_rotation: failed to read canvas data "
          "(BLResult=" + std::to_string(src_res) + ")");
      }

    const bool transposed = (quarter_turns_ % 2) != 0;
    const int dst_w = transposed ? src_h : src_w;
    const int dst_h = transposed ? src_w : src_h;

    BLImage rotated;
    const BLResult create_res = rotated.create(dst_w, dst_h, BL_FORMAT_PRGB32);
    if (create_res != BL_SUCCESS)
      {
        throw std::runtime_error(
          "renderer<BLEND2D>::apply_page_rotation: failed to create rotated canvas "
          "(BLResult=" + std::to_string(create_res) + ")");
      }

    BLImageData dst_data;
    const BLResult dst_res = rotated.make_mutable(&dst_data);
    if (dst_res != BL_SUCCESS)
      {
        throw std::runtime_error(
          "renderer<BLEND2D>::apply_page_rotation: failed to lock rotated canvas "
          "(BLResult=" + std::to_string(dst_res) + ")");
      }

    const auto* src_base = static_cast<const uint8_t*>(src_data.pixel_data);
    auto* dst_base = static_cast<uint8_t*>(dst_data.pixel_data);

    for (int y = 0; y < src_h; ++y)
      {
        const auto* src_row =
          reinterpret_cast<const uint32_t*>(src_base + y * src_data.stride);

        for (int x = 0; x < src_w; ++x)
          {
            // clockwise: the source top-left corner moves to the top-right for
            // one quarter turn, to the bottom-right for two, and so on
            int dst_x = 0;
            int dst_y = 0;
            switch (quarter_turns_)
              {
              case 1:
                dst_x = src_h - 1 - y;
                dst_y = x;
                break;
              case 2:
                dst_x = src_w - 1 - x;
                dst_y = src_h - 1 - y;
                break;
              default:
                dst_x = y;
                dst_y = src_w - 1 - x;
                break;
              }

            auto* dst_row =
              reinterpret_cast<uint32_t*>(dst_base + dst_y * dst_data.stride);
            dst_row[dst_x] = src_row[x];
          }
      }

    image_ = rotated;

    LOG_S(INFO) << "apply_page_rotation:"
                << " quarter_turns=" << quarter_turns_
                << " canvas=" << src_w << "x" << src_h
                << " -> " << dst_w << "x" << dst_h;
  }

  // ---------------------------------------------------------------------------
  // set_size
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::set_size(size_instruction& instr)
  {
    utils::scoped_timer phase_timer(timings_, pdf_timings::KEY_RENDER_SET_SIZE);

    finish_page_context();

    const auto& bbox = instr.crop_bbox;
    const double pdf_w = bbox[2] - bbox[0];
    const double pdf_h = bbox[3] - bbox[1];

    if (pdf_w <= 0.0 or pdf_h <= 0.0) { return; }

    quarter_turns_ = quarter_turns_from_angle(instr.angle);
    rotation_applied_ = false;

    // The requested canvas size describes the page as displayed, so it is
    // resolved against the displayed page size and mapped back onto the
    // unrotated canvas we actually draw on.
    const bool transposed = (quarter_turns_ % 2) != 0;
    const double display_pdf_w = transposed ? pdf_h : pdf_w;
    const double display_pdf_h = transposed ? pdf_w : pdf_h;

    const auto [display_width, display_height] =
      resolve_canvas_size(display_pdf_w, display_pdf_h, config_);

    const int width  = transposed ? display_height : display_width;
    const int height = transposed ? display_width  : display_height;

    // The page box is mapped onto the whole canvas, so the effective scale is
    // the pixel count over the page extent. Since the canvas covers a
    // fractional page extent, that is marginally more than the requested scale.
    scale_x_ = static_cast<double>(width)  / pdf_w;
    scale_y_ = static_cast<double>(height) / pdf_h;
    origin_x_ = bbox[0];
    origin_y_ = bbox[1];

    canvas_width_ = width;
    canvas_height_ = height;
    shape_ = {display_height, display_width, 4};

    LOG_S(INFO) << "set_size:"
                << " crop_bbox=[" << bbox[0] << "," << bbox[1] << "," << bbox[2] << "," << bbox[3] << "]"
                << " pdf_size=" << pdf_w << "x" << pdf_h
                << " angle=" << instr.angle
                << " canvas=" << width << "x" << height
                << " display=" << display_width << "x" << display_height
                << " scale=(" << scale_x_ << "," << scale_y_ << ")";

    // The canvas is the largest allocation the pipeline makes -- a letter page
    // at scale 1 is ~1.9 MB -- and macOS serves allocations that size from a
    // single process-wide lock (_xzm_malloc_large_huge ->
    // xzm_segment_group_alloc_chunk -> os_unfair_lock). Allocating one per page
    // therefore turns into a scaling bottleneck: profiling at 12 render threads
    // put 23% of all thread time in that lock. Keep the buffer per thread
    // instead, and hand it back in ~renderer(). Reuse cannot leak anything from
    // the previous page: the fill_all() below rewrites every pixel with
    // SRC_COPY.
    //
    // The image is *moved* out of the pool so this renderer holds the only
    // reference; Blend2D copies on write, and a shared buffer would defeat the
    // reuse it is here to provide.
    if (canvas_pool().width() == width and
        canvas_pool().height() == height and
        canvas_pool().format() == BL_FORMAT_PRGB32)
      {
        image_ = std::move(canvas_pool());
        canvas_pool().reset();
      }
    else
      {
        image_.create(width, height, BL_FORMAT_PRGB32);
      }

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

  inline blend2d_font_resolver::resolved_face renderer<BLEND2D>::resolve_font_face(
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
    blend2d_font_resolver::resolved_face resolved =
      font_resolver_->resolve_font(cache_key,
                                   base_font,
                                   config_.resolve_fonts,
                                   config_.font_similarity_cutoff);
    auto [inserted_itr, inserted] = local_font_cache_.emplace(cache_key, resolved);
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

    return BLMatrix2D(adv_x * geom.h_scale, adv_y * geom.h_scale,
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
      double size,
      const std::string* face_key)
  {
    text_draw_adjustment adjustment;

    const BLGlyphId glyph_id = gb.glyph_run().glyph_data_as<uint32_t>()[0];
    const BLMatrix2D identity(1.0, 0.0,
                              0.0, 1.0,
                              0.0, 0.0);
    BLBox rendered_box;
    if (face_key == nullptr ||
        not glyph_bbox_cache_->find(*face_key, glyph_id, size, rendered_box))
      {
        BLPath glyph_path;
        const BLResult outline_res =
          font.get_glyph_outlines(glyph_id, identity, glyph_path);
        if (outline_res != BL_SUCCESS || glyph_path.is_empty())
          {
            return adjustment;
          }

        const BLResult bbox_res = glyph_path.get_bounding_box(&rendered_box);
        if (bbox_res != BL_SUCCESS)
          {
            LOG_S(WARNING) << "render_text: glyph outline bbox failed"
                           << " (BLResult=" << bbox_res << ")";
            return adjustment;
          }

        if (face_key != nullptr)
          {
            glyph_bbox_cache_->insert(*face_key, glyph_id, size, rendered_box);
          }
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
      bool use_soft_mask_alpha,
      int out_w,
      int out_h) const
  {
    const auto& src_data = instr.get_data();
    const auto& alpha_data = instr.get_alpha_data();
    const bool image_mask = instr.is_image_mask();
    const auto fmt = instr.get_pixel_format();
    const auto fill_rgb = instr.get_rgb_filling();
    const int dw = std::max(1, out_w);
    const int dh = std::max(1, out_h);

    BLImage src_img;
    src_img.create(dw, dh, BL_FORMAT_PRGB32);

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
        const std::array<int, 3> sample =
          cmyk_bytes_to_rgb(c, m, y, k, instr.get_cmyk_convention());
        const int r = sample[0];
        const int g = sample[1];
        const int b = sample[2];
        LOG_S(INFO) << "render_bitmap: cmyk_sample[0]"
                    << " raw=(" << static_cast<int>(c) << ","
                    << static_cast<int>(m) << ","
                    << static_cast<int>(y) << ","
                    << static_cast<int>(k) << ")"
                    << " rgb=(" << static_cast<int>(r) << ","
                    << static_cast<int>(g) << ","
                    << static_cast<int>(b) << ")";
      }

    // Convert one source sample to the exact PRGB32 value the old full-size
    // intermediate used. The downsample path below averages these values
    // directly, so it is pixel-equivalent while avoiding that intermediate.
    const auto source_to_prgb32 = [&](int row, int col) -> uint32_t
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
            // The Adobe convention stores the ink amounts inverted, so both
            // conventions reach the same conversion; only the byte reading
            // differs.
            const std::array<int, 3> rgb =
              cmyk_bytes_to_rgb(src_data->at(idx + 0), src_data->at(idx + 1),
                                src_data->at(idx + 2), src_data->at(idx + 3),
                                instr.get_cmyk_convention());
            r = static_cast<uint8_t>(rgb[0]);
            g = static_cast<uint8_t>(rgb[1]);
            b = static_cast<uint8_t>(rgb[2]);
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

        const uint32_t pm_r = static_cast<uint32_t>(r) * a / 255u;
        const uint32_t pm_g = static_cast<uint32_t>(g) * a / 255u;
        const uint32_t pm_b = static_cast<uint32_t>(b) * a / 255u;
        return (static_cast<uint32_t>(a) << 24)
          | (pm_r                     << 16)
          | (pm_g                     <<  8)
          |  pm_b;
      };

    for (int y = 0; y < dh; ++y)
      {
        auto* drow = reinterpret_cast<uint32_t*>(base + y * stride);
        const int sy0 = (y * sh) / dh;
        const int sy1 = std::max(sy0 + 1, ((y + 1) * sh) / dh);

        for (int x = 0; x < dw; ++x)
          {
            const int sx0 = (x * sw) / dw;
            const int sx1 = std::max(sx0 + 1, ((x + 1) * sw) / dw);

            // The no-downsample path is kept as a direct conversion. When
            // downsampling, this is exactly the old premultiply-then-average
            // operation, performed before the full-size PRGB32 image exists.
            if (dw == sw and dh == sh)
              {
                drow[x] = source_to_prgb32(y, x);
                continue;
              }

            uint32_t a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int sy = sy0; sy < sy1 and sy < sh; ++sy)
              {
                for (int sx = sx0; sx < sx1 and sx < sw; ++sx)
                  {
                    const uint32_t p = source_to_prgb32(sy, sx);
                    a += (p >> 24) & 0xFFu;
                    r += (p >> 16) & 0xFFu;
                    g += (p >>  8) & 0xFFu;
                    b += (p      ) & 0xFFu;
                    n += 1;
                  }
              }

            drow[x] = (n == 0) ? 0u
                               : (((a / n) << 24) | ((r / n) << 16) |
                                  ((g / n) <<  8) |  (b / n));
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

    // This path draws the embedded program itself, so the glyphs keep the
    // proportions the font designed: the only horizontal scaling is the one
    // 9.4.4 defines (Th, plus an anisotropic text matrix or CTM), which the
    // parser computed. Deriving it from the face's advances instead stretched
    // every glyph of a font whose advances disagree with the PDF's /Widths.
    return draw_text_outline_path(instr, geom, bbox_path, text_path,
                                  instr.get_horizontal_scale());
  }

  // ---------------------------------------------------------------------------
  // render_text_freetype_file
  //
  // The same route for a system font FILE: the resolver chose it, Blend2D
  // could not open it (CFF2 outlines), FreeType can. Only the Unicode cmap is
  // consulted -- a substituted face knows nothing of the PDF's character
  // codes -- and there is no shaping, which for CJK costs nothing: the cmap
  // is 1:1 and the PDF positions every cell itself.
  // ---------------------------------------------------------------------------

  inline bool renderer<BLEND2D>::render_text_freetype_file(text_instruction& instr,
                                                           const text_geometry& geom,
                                                           const BLPath& bbox_path,
                                                           const std::string& font_path,
                                                           uint32_t face_index)
  {
    if (system_font_cache_ == nullptr or not system_font_cache_->available())
      {
        return false;
      }

    if (not config_.render_text) { return false; }

    if (geom.size <= 0.5 or font_path.empty()) { return false; }

    BLPath text_path;
    double run_width = 0.0;
    if (not system_font_cache_->build_text_path_from_file(font_path,
                                                          face_index,
                                                          instr.get_text(),
                                                          geom.size,
                                                          text_path,
                                                          &run_width))
      {
        return false;
      }

    // A substituted face has no claim on the PDF's widths: its advances are
    // unrelated to them, and fitting the run to the cell is what keeps the
    // page's layout intact. Same rule, and same bounds, as the condensation
    // the Blend2D substitution path applies.
    double h_scale = instr.get_horizontal_scale();
    {
      const double wx = geom.bbox.x1 - geom.bbox.x0;
      const double wy = geom.bbox.y1 - geom.bbox.y0;
      const double cell_w = std::hypot(wx, wy);
      if (cell_w > 0.5 and run_width > 0.5)
        {
          const double h = cell_w / run_width;
          if (h >= 0.30 and h <= 3.0 and std::abs(h - 1.0) > 0.03)
            {
              h_scale = h;
            }
        }
    }

    return draw_text_outline_path(instr, geom, bbox_path, text_path, h_scale);
  }

  // ---------------------------------------------------------------------------
  // draw_text_outline_path
  //
  // Fills the built glyph path under the cell's text transform, honouring the
  // clip state, the non-rect clip mask layer and the stroking text render
  // modes. Shared by the embedded-program and system-file FreeType routes.
  // ---------------------------------------------------------------------------

  inline bool renderer<BLEND2D>::draw_text_outline_path(text_instruction& instr,
                                                        const text_geometry& geom,
                                                        const BLPath& bbox_path,
                                                        const BLPath& text_path,
                                                        double h_scale)
  {
    text_geometry draw_geom = geom;
    draw_geom.h_scale = h_scale;

    BLContext& ctx = page_context();

    auto draw_path = [&](BLContext& draw_ctx,
                         const BLRectI* layer_area) -> bool
      {
        draw_ctx.save();
        if(layer_area != nullptr)
          {
            draw_ctx.translate(-layer_area->x, -layer_area->y);
          }

        const BLResult transform_res =
          draw_ctx.apply_transform(make_text_transform(draw_geom));
        if (transform_res != BL_SUCCESS)
          {
            draw_ctx.restore();
            LOG_S(WARNING) << "draw_text_outline_path: apply_transform failed"
                           << " (BLResult=" << transform_res << ")";
            return false;
          }

        draw_ctx.set_fill_style(make_rgba32(instr.get_rgb_filling(),
                                            instr.get_fill_alpha()));
        if (not text_path.is_empty())
          {
            draw_ctx.fill_path(text_path);

            // Text render modes with a stroke component (1, 2, 5, 6) are how
            // producers synthesise bold from a regular face (PDF 32000-1, 9.3.6),
            // and the Blend2D path strokes the run for exactly that reason. This
            // path has to do the same or a heading routed here comes out lighter
            // than the rest of the page -- which is how it looks when only some
            // of a face's glyphs land here.
            const int mode = instr.get_rendering_mode();
            if (mode == 1 or mode == 2 or mode == 5 or mode == 6)
              {
                draw_ctx.set_stroke_style(make_rgba32(instr.get_rgb_filling(),
                                                      instr.get_fill_alpha()));
                draw_ctx.set_stroke_width(std::max(0.3, geom.size * 0.03));
                draw_ctx.stroke_path(text_path);
              }
          }
        draw_ctx.restore();
        return true;
      };

    bool clip_active = false;
    if(instr.has_clip_state())
      {
        ctx.save();
        const clip_apply_result clip_result =
          apply_clip_state(ctx, instr.get_clip_state(),
                           axis_aligned_rect(geom.bbox));
        if(clip_result == CLIP_EMPTY)
          {
            ctx.restore();
            return true;
          }

        clip_active = clip_result == CLIP_APPLIED;
        if(not clip_active)
          {
            ctx.restore();
          }
      }

    BLImage clip_mask;
    BLRectI mask_area(0, 0, 0, 0);
    bool needs_clip_mask = false;
    if(instr.has_clip_state() and clip_state_has_non_rect(instr.get_clip_state()))
      {
        if(config_.render_non_rect_clip_masks)
          {
            needs_clip_mask = true;
            mask_area = canvas_clamped_rect(axis_aligned_rect(geom.bbox));
            const clip_mask_result clip =
              build_clip_mask(instr.get_clip_state(), mask_area);
            if(clip.empty_clip)
              {
                if(clip_active) { ctx.restore(); }
                return true;
              }
            clip_mask = std::move(clip.image);
          }
      }

    bool ok = true;
    if(needs_clip_mask and clip_mask.is_empty())
      {
        if(clip_active) { ctx.restore(); }
        return true;
      }
    else if(not clip_mask.is_empty())
      {
        BLImage layer;
        if(layer.create(mask_area.w, mask_area.h, BL_FORMAT_PRGB32) == BL_SUCCESS)
          {
            {
              BLContext lctx(layer);
              lctx.set_comp_op(BL_COMP_OP_SRC_COPY);
              lctx.fill_all(BLRgba32(0x00000000u));
              lctx.set_comp_op(BL_COMP_OP_SRC_OVER);
              ok = draw_path(lctx, &mask_area);
              lctx.end();
            }

            if(not multiply_prgb32_by_a8(layer, clip_mask))
              {
                LOG_S(WARNING) << "draw_text_outline_path: could not apply clip mask";
              }

            ctx.blit_image(BLPointI(mask_area.x, mask_area.y), layer);
          }
        else
          {
            LOG_S(WARNING) << "draw_text_outline_path: could not allocate clip layer "
                           << mask_area.w << "x" << mask_area.h
                           << ", falling back to context clip";
            ok = draw_path(ctx, nullptr);
          }
      }
    else
      {
        ok = draw_path(ctx, nullptr);
      }

    if(clip_active)
      {
        ctx.restore();
      }

    if(not ok) { return false; }

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

  inline renderer<BLEND2D>::blend_mode_scope::blend_mode_scope(BLContext& ctx,
                                                               blend_mode_name mode):
    ctx_(ctx),
    active_(push_blend_mode(ctx, mode))
  {}

  inline renderer<BLEND2D>::blend_mode_scope::~blend_mode_scope()
  {
    if (active_)
      {
        ctx_.restore();
      }
  }

  inline renderer<BLEND2D>::context_state_scope::context_state_scope(BLContext& ctx,
                                                                     bool active):
    ctx_(ctx),
    active_(active)
  {
    if (active_)
      {
        ctx_.save();
      }
  }

  inline renderer<BLEND2D>::context_state_scope::~context_state_scope()
  {
    restore();
  }

  inline void renderer<BLEND2D>::context_state_scope::restore()
  {
    if (active_)
      {
        ctx_.restore();
        active_ = false;
      }
  }

  inline void renderer<BLEND2D>::render_text(text_instruction& instr)
  {
    utils::scoped_timer phase_timer(timings_, pdf_timings::KEY_RENDER_TEXT);

    // LOG_S(INFO) << __FUNCTION__;

    if (not has_canvas()) { return; }

    const text_geometry geom = make_text_geometry(instr);

    // Degenerate cell: quad_h too small to build a valid direction vector.
    // Dividing by quad_h would produce NaN/Inf in the affine matrix.
    if (geom.quad_h < 0.5) { return; }

    // Type3 glyphs are content-stream procedures; their ink arrives as
    // image-mask bitmaps emitted at parse time. Drawing the cell text through
    // a font face here could only produce placeholder boxes over that ink.
    if (instr.is_type3()) { return; }

    // Build the bounding quad path (for optional bbox outline / fallback).
    const BLPath bbox_path = make_quad_path(geom.bbox);

    // Text render modes 3 and 7 paint no glyphs (invisible / clip-only,
    // e.g. OCR text layers); keep only the debug drawing.
    if (instr.is_invisible())
      {
        BLContext& ctx = page_context();
        draw_text_basepoint(ctx, geom);
        if (config_.draw_text_bbox)
          {
            stroke_text_bbox(ctx, bbox_path);
          }
        return;
      }

    // LOG_S(INFO) << "text=`" << instr.get_text() << "`"
    //             << " base=(" << bx << "," << by << ")"
    //             << " quad_h=" << quad_h
    //             << " a_norm=" << a_norm << " d_norm=" << d_norm
    //             << " cell_span=" << cell_span
    //             << " em_size=" << em_size << " size=" << size;

    const blend_mode_scope blend(page_context(), instr.get_blend_mode());

    // Resolution order: embedded font program — natively in Blend2D (SFNT)
    // or as FreeType outline paths (Type 1, bare CFF) — then the system font
    // resolver, then the hardcoded fallback.
    bool using_embedded_font = false;
    BLFontFace face;
    if (config_.use_embedded_fonts and instr.has_embedded_font())
      {
        utils::scoped_timer font_timer(
          timings_, pdf_timings::KEY_RENDER_TEXT_FONT_RESOLVE);
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

    blend2d_font_resolver::resolved_face resolved;
    if (not using_embedded_font)
      {
        resolved = resolve_font_face(instr.get_font_name(),
                                     instr.get_base_font());
        face = resolved.face;

        // The resolver found a file Blend2D cannot open -- CFF2 outlines,
        // which on a host that ships Noto CJK only as the variable build is
        // every CJK face it has. FreeType reads it; drawing the cell from
        // there is the difference between the text and a row of boxes.
        if (not face.is_valid() and not resolved.path.empty())
          {
            if (render_text_freetype_file(instr, geom, bbox_path,
                                          resolved.path, resolved.face_index))
              {
                return;
              }
          }

        // Neither backend can draw the cell with the name-resolved face. The
        // only thing left between this run and a bare rectangle is a face
        // chosen by the SCRIPT of the text, so ask for one before giving up.
        if (not face.is_valid() and font_resolver_ and not instr.is_type3())
          {
            const blend2d_font_resolver::resolved_face script_resolved =
              font_resolver_->resolve_font_for_text(instr.get_text());

            if (script_resolved.face.is_valid())
              {
                face = script_resolved.face;
              }
            else if (not script_resolved.path.empty() and
                     render_text_freetype_file(instr, geom, bbox_path,
                                               script_resolved.path,
                                               script_resolved.face_index))
              {
                return;
              }
          }
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

            // Shape the text before touching the context, so the recovery
            // paths below (FreeType outlines, system-face retry) don't have
            // to unwind a saved context state.
            BLGlyphBuffer gb;
            BLResult shape_res = BL_SUCCESS;
            bool shaped = false;

            // A symbolic simple font without /Encoding maps its character
            // codes through the builtin cmap (9.6.6.4), so Unicode shaping
            // is meaningless — and dangerously plausible: a codepoint that
            // collides with the font's code range "succeeds" with the wrong
            // glyph (e.g. U+0020 hitting subset code 0x20). Resolve by
            // character code first for such faces.
            //
            // CID fonts with an identity CIDToGIDMap must also resolve by
            // character code first: the content-stream CID is the exact glyph
            // the producer chose (contextual Arabic forms, lam-alef ligatures,
            // …). Round-tripping through the Unicode cmap instead picks the
            // nominal (isolated) form — Blend2D applies no GSUB shaping — so
            // right-to-left scripts come out as disconnected base letters.
            const auto& embedded_blob = instr.get_embedded_font();
            const bool char_code_first =
              using_embedded_font and
              instr.has_embedded_font() and
              (embedded_blob->get_is_cid_font()
                 ? embedded_blob->get_cid_to_gid_identity()
                 : embedded_blob->get_uses_builtin_encoding());

            if (char_code_first)
              {
                // A recovery that resolves every code to glyph 0 has not
                // recovered anything: accepting it draws a row of .notdef
                // boxes where the page has an unmapped glyph and every other
                // renderer draws nothing.
                shaped = recover_embedded_glyphs(font, instr, gb)
                         and not glyph_run_all_notdef(gb);
              }

            if (not shaped)
              {
                gb.set_utf8_text(instr.get_text().c_str());
                shape_res = font.shape(gb);
                shaped = (shape_res == BL_SUCCESS and not gb.is_empty());
              }

            if (using_embedded_font and
                (not shaped or glyph_run_all_notdef(gb)))
              {
                // An embedded subset face misses a cell in two ways: shape()
                // fails outright (BL_ERROR_FONT_NO_CHARACTER_MAPPING when the
                // subset carries no Unicode cmap at all) or "succeeds" with a
                // .notdef-only run when the cmap lacks the codepoint. Either
                // way, try glyph-identity mapping (CID identity, symbol cmap,
                // raw char code — unless already tried above), then FreeType
                // glyph-name/builtin-encoding resolution, then re-shape
                // against the system face — drawing the .notdef run would
                // produce blank/tofu output.
                shaped = (not char_code_first)
                         and recover_embedded_glyphs(font, instr, gb)
                         and not glyph_run_all_notdef(gb);

                if (not shaped)
                  {
                    if (render_text_freetype(instr, geom, bbox_path))
                      {
                        return;
                      }

                    LOG_S(INFO) << "render_text: embedded face cannot map"
                                << " (BLResult=" << shape_res << ")"
                                << " text=`" << instr.get_text() << "`"
                                << " font_name=`" << instr.get_font_name() << "`"
                                << " — falling back to system font";

                    const blend2d_font_resolver::resolved_face system_resolved =
                      resolve_font_face(instr.get_font_name(),
                                        instr.get_base_font());
                    BLFontFace system_face = system_resolved.face;

                    if (not system_face.is_valid() and
                        not system_resolved.path.empty() and
                        render_text_freetype_file(instr, geom, bbox_path,
                                                  system_resolved.path,
                                                  system_resolved.face_index))
                      {
                        return;
                      }

                    BLFont system_font;
                    if (system_face.is_valid() and
                        system_font.create_from_face(system_face,
                                                     static_cast<float>(geom.size)) == BL_SUCCESS)
                      {
                        gb.set_utf8_text(instr.get_text().c_str());
                        shape_res = system_font.shape(gb);
                        if (shape_res == BL_SUCCESS and not gb.is_empty() and
                            not glyph_run_all_notdef(gb))
                          {
                            font = system_font;
                            using_embedded_font = false;
                            shaped = true;
                          }
                      }
                  }
              }

            // Last resort before giving up on the run: choose a face by the
            // SCRIPT of the text. Name-based resolution hands Arabic or CJK
            // runs a Latin face, which shapes every glyph to .notdef -- the
            // page fills with boxes. Type3 stays out: its ink arrives as
            // parse-time bitmaps, and shaping its cell text against a system
            // face would smear placeholder boxes over that ink.
            //
            // A run that shaped with *some* glyphs missing needs this too. The
            // name-resolved face for a CJK font is often one that carries part
            // of the repertoire; keeping it because it drew most of the run
            // leaves a box at every character it lacks, scattered through the
            // page. Only a run with nothing missing is settled.
            const size_t notdef_before =
              shaped ? glyph_run_notdef_count(gb)
                     : std::numeric_limits<size_t>::max();

            // An embedded face is authoritative for the glyphs it does map:
            // swapping the whole run for a system face because one character
            // is missing would redraw the rest in another typeface, so only a
            // run it cannot draw at all is given up. A substituted face has no
            // such claim -- every character it lacks is a box, and a face that
            // has them all is strictly better.
            const bool needs_better_face =
              not shaped or
              (using_embedded_font ? glyph_run_all_notdef(gb)
                                   : glyph_run_has_notdef(gb));

            if (font_resolver_ and not instr.is_type3() and needs_better_face)
              {
                const blend2d_font_resolver::resolved_face script_resolved =
                  font_resolver_->resolve_font_for_text(instr.get_text());
                BLFontFace script_face = script_resolved.face;

                // The only face that covers this script is one Blend2D
                // cannot open. Nothing has been drawn yet -- shaping happens
                // before the context is touched -- so the cell can go to
                // FreeType whole.
                if (not script_face.is_valid() and
                    not script_resolved.path.empty() and
                    render_text_freetype_file(instr, geom, bbox_path,
                                              script_resolved.path,
                                              script_resolved.face_index))
                  {
                    return;
                  }

                BLFont script_font;
                if (script_face.is_valid() and
                    script_font.create_from_face(
                      script_face, static_cast<float>(geom.size)) == BL_SUCCESS)
                  {
                    gb.set_utf8_text(instr.get_text().c_str());
                    shape_res = script_font.shape(gb);

                    const bool script_shaped =
                      (shape_res == BL_SUCCESS and not gb.is_empty());
                    const size_t notdef_after =
                      script_shaped ? glyph_run_notdef_count(gb)
                                    : std::numeric_limits<size_t>::max();

                    if (script_shaped and notdef_after < notdef_before)
                      {
                        font = script_font;
                        using_embedded_font = false;
                        shaped = true;
                      }
                    else if (shaped)
                      {
                        // The script face is no better; put the run that was
                        // already shaped back into the buffer.
                        gb.set_utf8_text(instr.get_text().c_str());
                        shape_res = font.shape(gb);
                        shaped = (shape_res == BL_SUCCESS and not gb.is_empty());
                      }
                  }
              }

            if (not shaped)
              {
                LOG_S(WARNING) << "render_text: shaping failed or produced no glyphs"
                               << " (BLResult=" << shape_res << ")"
                               << " text=`" << instr.get_text() << "`"
                               << " font_name=`" << instr.get_font_name() << "`"
                               << " base_font=`" << instr.get_base_font() << "`";
                draw_bbox_fallback();
                return;
              }

            // The face loaded and shaped, but its outlines can still come back
            // mangled; FreeType reads the same glyphs correctly, so hand the
            // cell to it rather than stamping a page-sized wedge on the page.
            //
            // The whole face goes with it, not just the glyphs caught. The
            // misdecode hits some glyphs of a face and not others, and the two
            // rasterisers differ slightly in weight, so drawing one word from
            // both leaves it visibly patchy. One bad outline condemns the face
            // for the rest of the page.
            const std::string* glyph_bbox_face_key = nullptr;
            if (using_embedded_font)
              {
                const std::string& face_key =
                  instr.get_embedded_font()->get_cache_key();
                glyph_bbox_face_key = &face_key;
                const bool distrusted =
                  distrusted_embedded_faces_.count(face_key) > 0;

                // Extracting every glyph outline of the run to sanity-check
                // the face is the single most expensive thing rendering does;
                // time it separately from the drawing it guards.
                bool implausible = false;
                if (not distrusted)
                  {
                    utils::scoped_timer glyph_timer(
                      timings_, pdf_timings::KEY_RENDER_TEXT_GLYPH_METRICS);
                    implausible =
                      glyph_outlines_are_implausible(font, gb, geom.size, face_key);
                  }

                if (distrusted or implausible)
                  {
                    if (not distrusted)
                      {
                        LOG_S(WARNING) << "render_text: embedded face decoded an"
                                       << " outline far larger than its em"
                                       << " text=`" << instr.get_text() << "`"
                                       << " font_name=`" << instr.get_font_name() << "`"
                                       << " — drawing this face through FreeType";
                        distrusted_embedded_faces_.insert(face_key);
                      }

                    if (render_text_freetype(instr, geom, bbox_path))
                      {
                        return;
                      }
                  }
              }

            const auto* placement_data = gb.placement_data();
            if (placement_data == nullptr)
              {
                LOG_S(WARNING) << "render_text: glyph placement data is null, using fallback";
                draw_bbox_fallback();
                return;
              }

            text_geometry draw_geom = geom;

            // The font program's own advances are not the widths the PDF lays
            // the page out by: a /W of 1000 over a face whose glyphs advance
            // 504 is a normal, conforming file. Fitting the run to the cell
            // therefore stretched those glyphs to twice their width. The glyph
            // is drawn as the program designed it, scaled only by what 9.4.4
            // says scales it -- which the parser worked out and sent along.
            //
            // A substituted face has no such claim: its advances are unrelated
            // to the PDF's, and fitting the run to the cell is what keeps a
            // page's layout intact.
            if (using_embedded_font)
              {
                draw_geom.h_scale = instr.get_horizontal_scale();
              }
            else
              {
                apply_condensation(draw_geom, font, gb);
              }
            const BLMatrix2D ctm = make_text_transform(draw_geom);
            text_draw_adjustment adjustment;
            {
              utils::scoped_timer glyph_timer(
                timings_, pdf_timings::KEY_RENDER_TEXT_GLYPH_METRICS);
              adjustment =
                calculate_glyph_bbox_adjustment(font, gb, instr, geom.size,
                                                glyph_bbox_face_key);
            }

            BLBox adjusted_render_box{};
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

            auto draw_run = [&](BLContext& draw_ctx,
                                const BLRectI* layer_area) -> BLResult
              {
                text_draw_adjustment draw_adjustment = adjustment;
                draw_ctx.save();
                if(layer_area != nullptr)
                  {
                    draw_ctx.translate(-layer_area->x, -layer_area->y);
                  }

                const BLResult transform_res = draw_ctx.apply_transform(ctm);
                if (transform_res != BL_SUCCESS)
                  {
                    draw_ctx.restore();
                    LOG_S(WARNING) << "render_text: apply_transform failed"
                                   << " (BLResult=" << transform_res << ")";
                    return transform_res;
                  }

                draw_ctx.set_fill_style(make_rgba32(instr.get_rgb_filling(),
                                                    instr.get_fill_alpha()));

                if (draw_adjustment.bbox_fit_scale != 1.0)
                  {
                    const BLResult translate_res =
                      draw_ctx.translate(draw_adjustment.draw_origin.x,
                                         draw_adjustment.draw_origin.y);
                    if (translate_res != BL_SUCCESS)
                      {
                        LOG_S(WARNING) << "render_text: translate failed"
                                       << " (BLResult=" << translate_res << ")";
                        draw_ctx.restore();
                        return translate_res;
                      }
                    const BLResult scale_res =
                      draw_ctx.scale(draw_adjustment.bbox_fit_scale);
                    if (scale_res != BL_SUCCESS)
                      {
                        LOG_S(WARNING) << "render_text: scale failed"
                                       << " (BLResult=" << scale_res << ")";
                        draw_ctx.restore();
                        return scale_res;
                      }
                    draw_adjustment.draw_origin.reset(0.0, 0.0);
                  }
                // Draw the glyph run that was already shaped (or recovered by
                // glyph identity) above; fill_utf8_text would re-shape the text
                // and lose any glyph-identity recovery.
                const BLResult text_res =
                  draw_ctx.fill_glyph_run(draw_adjustment.draw_origin,
                                          font,
                                          gb.glyph_run());

                // Text render modes with a stroke component (1, 2, 5, 6) are how
                // producers synthesise bold from a regular face (PDF 32000-1,
                // 9.3.6). Stroking the same run on top of the fill is what carries
                // that weight; drawing only the fill silently drops the emphasis
                // from every heading set this way.
                {
                  const int mode = instr.get_rendering_mode();
                  if(mode == 1 or mode == 2 or mode == 5 or mode == 6)
                    {
                      draw_ctx.set_stroke_style(make_rgba32(instr.get_rgb_filling(),
                                                            instr.get_fill_alpha()));
                      draw_ctx.set_stroke_width(std::max(0.3, geom.size * 0.03));
                      draw_ctx.stroke_glyph_run(draw_adjustment.draw_origin,
                                                font,
                                                gb.glyph_run());
                    }
                }
                // LOG_S(INFO) << "render_text: after fill_glyph_run res=" << text_res;
                // LOG_S(INFO) << "render_text: before ctx.restore";
                draw_ctx.restore();
                return text_res;
              };

            bool clip_active = false;
            if(instr.has_clip_state())
              {
                ctx.save();
                const clip_apply_result clip_result =
                  apply_clip_state(ctx, instr.get_clip_state(),
                                   axis_aligned_rect(geom.bbox));
                if(clip_result == CLIP_EMPTY)
                  {
                    ctx.restore();
                    return;
                  }

                clip_active = clip_result == CLIP_APPLIED;
                if(not clip_active)
                  {
                    ctx.restore();
                  }
              }

            BLImage clip_mask;
            BLRectI mask_area(0, 0, 0, 0);
            bool needs_clip_mask = false;
            if(instr.has_clip_state() and
               clip_state_has_non_rect(instr.get_clip_state()))
              {
                if(config_.render_non_rect_clip_masks)
                  {
                    needs_clip_mask = true;
                    mask_area =
                      canvas_clamped_rect(axis_aligned_rect(geom.bbox));
                    const clip_mask_result clip =
                      build_clip_mask(instr.get_clip_state(), mask_area);
                    if(clip.empty_clip)
                      {
                        if(clip_active) { ctx.restore(); }
                        return;
                      }
                    clip_mask = std::move(clip.image);
                  }
              }

            BLResult text_res = BL_SUCCESS;
            if(needs_clip_mask and clip_mask.is_empty())
              {
                if(clip_active) { ctx.restore(); }
                return;
              }
            else if(not clip_mask.is_empty())
              {
                BLImage layer;
                if(layer.create(mask_area.w, mask_area.h, BL_FORMAT_PRGB32) == BL_SUCCESS)
                  {
                    {
                      BLContext lctx(layer);
                      lctx.set_comp_op(BL_COMP_OP_SRC_COPY);
                      lctx.fill_all(BLRgba32(0x00000000u));
                      lctx.set_comp_op(BL_COMP_OP_SRC_OVER);
                      text_res = draw_run(lctx, &mask_area);
                      lctx.end();
                    }

                    if(not multiply_prgb32_by_a8(layer, clip_mask))
                      {
                        LOG_S(WARNING) << "render_text: could not apply clip mask";
                      }

                    ctx.blit_image(BLPointI(mask_area.x, mask_area.y), layer);
                  }
                else
                  {
                    LOG_S(WARNING) << "render_text: could not allocate clip layer "
                                   << mask_area.w << "x" << mask_area.h
                                   << ", falling back to context clip";
                    text_res = draw_run(ctx, nullptr);
                  }
              }
            else
              {
                text_res = draw_run(ctx, nullptr);
              }

            if(clip_active)
              {
                ctx.restore();
              }

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
    utils::scoped_timer phase_timer(timings_, pdf_timings::KEY_RENDER_BITMAP);

    LOG_S(INFO) << __FUNCTION__ << " for xobject_key=" << instr.get_key();

    if (not has_canvas())
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

    // How many source pixels land on one destination pixel. The quad's two
    // edges give the drawn extent, which is what the blit resamples onto.
    int blit_w = sw;
    int blit_h = sh;
    {
      const double dst_w = std::hypot(q.x2 - q.x1, q.y2 - q.y1);
      const double dst_h = std::hypot(q.x0 - q.x1, q.y0 - q.y1);

      const int fx = (dst_w > 0.5) ? static_cast<int>(sw / dst_w) : 1;
      const int fy = (dst_h > 0.5) ? static_cast<int>(sh / dst_h) : 1;

      if(fx >= 2 or fy >= 2)
        {
          blit_w = std::max(1, sw / std::max(1, fx));
          blit_h = std::max(1, sh / std::max(1, fy));

          LOG_S(INFO) << "render_bitmap: averaged " << sw << "x" << sh
                      << " down to " << blit_w << "x" << blit_h
                      << " for a " << dst_w << "x" << dst_h << " draw"
                      << " (xobject_key=" << instr.get_key() << ")";
        }
    }

    BLImage src_img = build_bitmap_image(instr, sw, sh, sc, use_soft_mask_alpha,
                                         blit_w, blit_h);

    const bool can_use_axis_aligned_fast_path =
      axis_aligned and right_angle and quarter_turns == 0;

    // ExtGState constant alpha (/ca) in force at the `Do`. It is applied as a
    // context-global alpha so that it *multiplies* the per-pixel soft-mask
    // alpha baked into src_img instead of replacing it.
    static constexpr double min_visible_alpha = 1.0 / 512.0;
    const double fill_alpha =
      std::min(1.0, std::max(0.0, instr.get_fill_alpha()));

    if (fill_alpha <= min_visible_alpha)
      {
        LOG_S(INFO) << "render_bitmap: fill-alpha " << fill_alpha
                    << " is invisible, skipping xobject_key=" << instr.get_key();
        return;
      }

    // The save/restore pairs nest: the alpha is pushed inside the clip, and
    // the clip inside the blend mode. Each is a scope, so every exit below
    // unwinds them in that order without a restore to remember.
    const blend_mode_scope blend(ctx, instr.get_blend_mode());

    const bool has_clip = instr.has_clip_state();
    context_state_scope clip_scope(ctx, has_clip);
    if(has_clip)
      {
        LOG_S(INFO) << "render_bitmap: applying "
                    << instr.get_clip_state().get_paths().size()
                    << " clip path(s)";
        const clip_apply_result clip_result =
          apply_clip_state(ctx,
                           instr.get_clip_state(),
                           axis_aligned_rect(q));
        if(clip_result == CLIP_EMPTY)
          {
            return;
          }

        if(clip_result != CLIP_APPLIED)
          {
            clip_scope.restore();
          }
      }

    // A curved clip (a crescent, a rounded avatar, a pie slice) has no
    // clip_to_rect equivalent, so apply_clip_state skips it and the image
    // would spill over its whole bounding box. Draw such images offscreen and
    // knock them back with a rasterised coverage mask instead.
    BLRectI mask_area(0, 0, 0, 0);
    BLImage clip_mask;
    if(has_clip and clip_state_has_non_rect(instr.get_clip_state()))
      {
        if(config_.render_non_rect_clip_masks)
          {
            mask_area = canvas_clamped_rect(axis_aligned_rect(q));
            const clip_mask_result clip =
              build_clip_mask(instr.get_clip_state(), mask_area);
            if(clip.empty_clip)
              {
                return;
              }
            clip_mask = std::move(clip.image);
          }
        else
          {
            LOG_S(INFO) << "render_bitmap: non-rectangular clip mask disabled";
          }
      }

    const bool alpha_active = fill_alpha < 1.0;
    context_state_scope alpha_scope(ctx, alpha_active);
    if(alpha_active)
      {
        LOG_S(INFO) << "render_bitmap: applying constant alpha " << fill_alpha;
        ctx.set_global_alpha(fill_alpha);
      }

    if(not clip_mask.is_empty())
      {
        BLImage layer;
        if(layer.create(mask_area.w, mask_area.h, BL_FORMAT_PRGB32) == BL_SUCCESS)
          {
            {
              BLContext lctx(layer);
              lctx.set_comp_op(BL_COMP_OP_SRC_COPY);
              lctx.fill_all(BLRgba32(0x00000000u));
              lctx.set_comp_op(BL_COMP_OP_SRC_OVER);

              // Draw in page coordinates; the layer is just a shifted window
              // onto the page, so the existing helpers need no changes.
              lctx.translate(-mask_area.x, -mask_area.y);

              if(can_use_axis_aligned_fast_path)
                { render_bitmap_axis_aligned(lctx, src_img, q, blit_w, blit_h); }
              else
                { render_bitmap_affine(lctx, src_img, q, blit_w, blit_h); }
              lctx.end();
            }

            if(not multiply_prgb32_by_a8(layer, clip_mask))
              {
                LOG_S(WARNING) << "render_bitmap: could not apply clip mask";
              }

            ctx.blit_image(BLPointI(mask_area.x, mask_area.y), layer);
          }
        else
          {
            LOG_S(WARNING) << "render_bitmap: could not allocate clip layer "
                           << mask_area.w << "x" << mask_area.h
                           << ", falling back to an unclipped draw";
            render_bitmap_affine(ctx, src_img, q, blit_w, blit_h);
          }
      }
    else if (can_use_axis_aligned_fast_path)
      {
        LOG_S(INFO) << "render_bitmap: selecting axis-aligned path";
        render_bitmap_axis_aligned(ctx, src_img, q, blit_w, blit_h);
      }
    else
      {
        LOG_S(INFO) << "render_bitmap: selecting affine path"
                    << " (right_angle=" << (right_angle ? "true" : "false")
                    << ", quarter_turns=" << quarter_turns << ")";
        render_bitmap_affine(ctx, src_img, q, blit_w, blit_h);
      }

  }

  // ---------------------------------------------------------------------------
  // render_widget
  //
  // Draws the widget's rotated bounding quad as a semi-transparent filled
  // polygon in config_.color_widgets. The text value is not rendered here: the
  // widget's appearance stream is decoded into ordinary text and shape
  // instructions, which are drawn regardless of config_.display_widgets.
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::render_widget(text_widget_instruction& instr)
  {
    utils::scoped_timer phase_timer(timings_, pdf_timings::KEY_RENDER_WIDGET);

    LOG_S(INFO) << __FUNCTION__ << "  text='" << instr.get_text() << "'";

    if (not has_canvas()) { return; }

    if (not config_.display_widgets) { return; }

    BLPath path;
    path.move_to(canvas_x(instr.get_r_x0()), canvas_y(instr.get_r_y0()));
    path.line_to(canvas_x(instr.get_r_x1()), canvas_y(instr.get_r_y1()));
    path.line_to(canvas_x(instr.get_r_x2()), canvas_y(instr.get_r_y2()));
    path.line_to(canvas_x(instr.get_r_x3()), canvas_y(instr.get_r_y3()));
    path.close();

    BLContext& ctx = page_context();
    ctx.set_fill_style(make_rgba32(config_.color_widgets, 0.4));   // translucent body
    ctx.fill_path(path);
    ctx.set_stroke_style(make_rgba32(config_.color_widgets, 1.0)); // opaque border
    ctx.set_stroke_width(1);
    ctx.stroke_path(path);
  }

  // ---------------------------------------------------------------------------
  // make_shape_path
  // ---------------------------------------------------------------------------

  inline BLPath renderer<BLEND2D>::make_shape_path(const shape_instruction& instr,
                                                   BLRect& bbox) const
  {
    BLPath path;

    double x_min = std::numeric_limits<double>::infinity();
    double y_min = std::numeric_limits<double>::infinity();
    double x_max = -std::numeric_limits<double>::infinity();
    double y_max = -std::numeric_limits<double>::infinity();

    auto accumulate = [&](double cx, double cy)
    {
      x_min = std::min(x_min, cx);
      y_min = std::min(y_min, cy);
      x_max = std::max(x_max, cx);
      y_max = std::max(y_max, cy);
    };

    for (const auto& sp : instr.get_subpaths())
      {
        if (sp.empty()) { continue; } // move-to alone draws nothing

        const double sx = canvas_x(sp.get_x0());
        const double sy = canvas_y(sp.get_y0());
        path.move_to(sx, sy);
        accumulate(sx, sy);

        const auto& ops = sp.get_ops();
        const auto& px  = sp.get_px();
        const auto& py  = sp.get_py();

        size_t k = 0;
        for (const auto op : ops)
          {
            const size_t needed = (op == SEGMENT_CUBIC_TO) ? 3 : 1;
            if (k + needed > px.size() or k + needed > py.size())
              {
                LOG_S(ERROR) << "render_shape: segment ops and points are"
                             << " inconsistent, truncating subpath";
                break;
              }

            if (op == SEGMENT_LINE_TO)
              {
                const double x = canvas_x(px[k]);
                const double y = canvas_y(py[k]);
                k += 1;

                path.line_to(x, y);
                accumulate(x, y);
              }
            else // SEGMENT_CUBIC_TO
              {
                const double x1 = canvas_x(px[k]);
                const double y1 = canvas_y(py[k]);
                const double x2 = canvas_x(px[k + 1]);
                const double y2 = canvas_y(py[k + 1]);
                const double x3 = canvas_x(px[k + 2]);
                const double y3 = canvas_y(py[k + 2]);
                k += 3;

                path.cubic_to(x1, y1, x2, y2, x3, y3);
                accumulate(x1, y1);
                accumulate(x2, y2);
                accumulate(x3, y3);
              }
          }

        if (sp.get_closing_type() == CLOSED)
          {
            path.close();
          }
      }

    if (x_min <= x_max and y_min <= y_max)
      {
        bbox = BLRect(x_min, y_min, x_max - x_min, y_max - y_min);
      }
    else
      {
        bbox = BLRect(0.0, 0.0, 0.0, 0.0);
      }

    return path;
  }

  // ---------------------------------------------------------------------------
  // render_shape
  //
  // Fill first, stroke on top (the PDF paint order for B/b operators), with
  // the fill rule, colors, stroke width and cap/join parameters delivered by
  // the parse layer. Only axis-aligned rectangular clips are applied; dashed
  // strokes render solid because the vendored Blend2D stroker does not
  // implement dashing.
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::render_shape(shape_instruction& instr)
  {
    utils::scoped_timer phase_timer(timings_, pdf_timings::KEY_RENDER_SHAPE);

    if (not has_canvas()) { return; }
    if (not config_.render_shapes) { return; }

    LOG_S(INFO) << __FUNCTION__ << ": "
		<< " shape_paint_mode: " << instr.get_paint_mode()
      		<< ", shape_fill_rule: " << instr.get_fill_rule()
		<< ", length: " << instr.get_subpaths_length();
    
    BLRect bbox;
    const BLPath path = make_shape_path(instr, bbox);
    if (path.is_empty()) { return; }

    BLContext& ctx = page_context();

    // Both scopes unwind on every exit below, in reverse order of their
    // construction: the clip was saved inside the blend mode, so it comes off
    // first.
    const blend_mode_scope blend(ctx, instr.get_blend_mode());
    context_state_scope    clip_scope(ctx, instr.has_clip_state());

    if (instr.has_clip_state())
      {
        const clip_apply_result clip_result =
          apply_clip_state(ctx, instr.get_clip_state(), bbox);
        if (clip_result == CLIP_EMPTY)
          {
            return;
          }

        if (clip_result != CLIP_APPLIED)
          {
            clip_scope.restore();
          }
      }

    // A curved clip cannot be expressed by clip_to_rect, so apply_clip_state
    // leaves it out and the fill would spill over its whole bounding box.
    // Rasterise those paths into a coverage mask and paint through it.
    BLImage clip_mask;
    BLRectI mask_area(0, 0, 0, 0);
    if (instr.has_clip_state() and clip_state_has_non_rect(instr.get_clip_state()))
      {
        if(config_.render_non_rect_clip_masks)
          {
            mask_area = canvas_clamped_rect(bbox);
            // LOG_S(INFO) << "render_shape: building clip mask"
            //             << " bbox=(" << bbox.x << ", " << bbox.y
            //             << ", " << bbox.w << ", " << bbox.h << ")"
            //             << " mask_area=(" << mask_area.x << ", " << mask_area.y
            //             << ", " << mask_area.w << ", " << mask_area.h << ")";
            const clip_mask_result clip =
              build_clip_mask(instr.get_clip_state(), mask_area);
            if(clip.empty_clip)
              {
                // The clip leaves this shape nothing to paint. Leaving through
                // here used to skip the restores below and strand the clip on
                // the context.
                return;
              }
            clip_mask = std::move(clip.image);
            // LOG_S(INFO) << "render_shape: build_clip_mask returned empty="
            //             << (clip_mask.is_empty() ? "true" : "false");
          }
        else
          {
            // LOG_S(INFO) << "render_shape: non-rectangular clip mask disabled";
          }
      }

    const shape_paint_mode mode = instr.get_paint_mode();

    // ExtGState constant alpha: alpha 0 paint is invisible and skipped
    // entirely (a common idiom for hiding helper geometry).
    static constexpr double min_visible_alpha = 1.0 / 512.0;
    const double fill_alpha = instr.get_fill_alpha();
    const double stroke_alpha = instr.get_stroke_alpha();

    if ((mode == SHAPE_PAINT_FILL or mode == SHAPE_PAINT_FILL_STROKE)
        and fill_alpha > min_visible_alpha)
      {
        ctx.set_fill_rule(instr.get_fill_rule() == SHAPE_FILL_EVEN_ODD
                            ? BL_FILL_RULE_EVEN_ODD
                            : BL_FILL_RULE_NON_ZERO);
        ctx.set_fill_style(make_rgba32(instr.get_rgb_filling(), fill_alpha));

        if (not clip_mask.is_empty())
          {
            // LOG_S(INFO) << "render_shape: clipped fill layer begin"
            //             << " area=(" << mask_area.x << ", " << mask_area.y
            //             << ", " << mask_area.w << ", " << mask_area.h << ")";
            // Paint the fill into an offscreen window, knock it back with the
            // clip coverage, then composite. Blend2D cannot clip to a path, so
            // this is what keeps a crescent from filling its bounding box.
            BLImage layer;
            const BLResult layer_create =
              layer.create(mask_area.w, mask_area.h, BL_FORMAT_PRGB32);
            // LOG_S(INFO) << "render_shape: layer.create result=" << layer_create;
            if (layer_create == BL_SUCCESS)
              {
                {
                  // LOG_S(INFO) << "render_shape: filling clipped layer";
                  BLContext lctx(layer);
                  lctx.set_comp_op(BL_COMP_OP_SRC_COPY);
                  lctx.fill_all(BLRgba32(0x00000000u));
                  lctx.set_comp_op(BL_COMP_OP_SRC_OVER);
                  lctx.set_fill_rule(instr.get_fill_rule() == SHAPE_FILL_EVEN_ODD
                                       ? BL_FILL_RULE_EVEN_ODD
                                       : BL_FILL_RULE_NON_ZERO);

                  BLPath shifted;
                  shifted.add_path(path,
                                   BLMatrix2D::make_translation(-mask_area.x, -mask_area.y));
                  lctx.fill_path(shifted,
                                 make_rgba32(instr.get_rgb_filling(), fill_alpha));
                  lctx.end();
                  // LOG_S(INFO) << "render_shape: clipped layer filled";
                }

                // LOG_S(INFO) << "render_shape: applying clip mask to layer";
                if (not multiply_prgb32_by_a8(layer, clip_mask))
                  {
                    LOG_S(WARNING) << "render_shape: could not apply clip mask";
                  }
                // LOG_S(INFO) << "render_shape: clip mask applied";

                // LOG_S(INFO) << "render_shape: blitting clipped layer";
                ctx.blit_image(BLPointI(mask_area.x, mask_area.y), layer);
                // LOG_S(INFO) << "render_shape: clipped layer blitted";
              }
            else
              {
                LOG_S(WARNING) << "render_shape: layer allocation failed,"
                               << " filling path without mask";
                ctx.fill_path(path);
              }
          }
        else
          {
            ctx.fill_path(path);
          }
      }

    if ((mode == SHAPE_PAINT_STROKE or mode == SHAPE_PAINT_FILL_STROKE)
        and stroke_alpha > min_visible_alpha)
      {
        ctx.set_stroke_style(make_rgba32(instr.get_rgb_stroking(),
                                         stroke_alpha));

        // the line width arrives in page space; scale to canvas and keep
        // sub-pixel strokes visible (PDF `0 w` means hairline)
        const double width =
          instr.get_line_width() * 0.5 * (scale_x_ + scale_y_);
        ctx.set_stroke_width(std::max(
          width,
          static_cast<double>(config_.min_stroke_width)));

        ctx.set_stroke_caps(to_stroke_cap(instr.get_line_cap()));
        ctx.set_stroke_join(to_stroke_join(instr.get_line_join()));
        ctx.set_stroke_miter_limit(instr.get_miter_limit());

        if (not instr.get_dash_array().empty())
          {
            LOG_S(INFO) << "render_shape: dash pattern ignored"
                        << " (not implemented by the Blend2D stroker)";
          }

        ctx.stroke_path(path);
      }
  }

  // ---------------------------------------------------------------------------
  // render_shading
  //
  // Paints an axial or radial shading (`sh`) over the current clip region.
  //
  // The gradient geometry is left in shading space and handed to Blend2D
  // together with a style transform, so the shading's level sets stay correct
  // under a flipped or skewed CTM. /Extend controls what happens beyond the
  // axis: `true` is Blend2D's pad mode, `false` is emulated with a transparent
  // guard stop, since the pad mode is the only one that keeps the interior
  // ramp intact.
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::render_shading(shading_instruction& instr)
  {
    utils::scoped_timer phase_timer(timings_, pdf_timings::KEY_RENDER_SHADING);

    if (not has_canvas()) { return; }
    if (not config_.render_shadings) { return; }

    const std::vector<shading_stop>& stops = instr.get_stops();
    const std::vector<double>& coords = instr.get_coords();

    LOG_S(INFO) << __FUNCTION__ << ": key='" << instr.get_key() << "'"
                << ", geometry: " << static_cast<int>(instr.get_geometry())
                << ", #-stops: " << stops.size()
                << ", alpha: " << instr.get_fill_alpha();

    if (stops.size() < 2)
      {
        LOG_S(WARNING) << "render_shading: shading " << instr.get_key()
                       << " has fewer than two colour stops, skipping";
        return;
      }

    static constexpr double min_visible_alpha = 1.0 / 512.0;
    const double fill_alpha =
      std::min(1.0, std::max(0.0, instr.get_fill_alpha()));

    if (fill_alpha <= min_visible_alpha)
      {
        LOG_S(INFO) << "render_shading: fill-alpha " << fill_alpha
                    << " is invisible, skipping " << instr.get_key();
        return;
      }

    // shading space -> canvas space: the PDF matrix [a b c d e f] composed
    // with the page-to-canvas mapping (which flips y).
    const std::array<double, 6>& m = instr.get_matrix();
    const BLMatrix2D transform(
       scale_x_ * m[0], -scale_y_ * m[1],
       scale_x_ * m[2], -scale_y_ * m[3],
       scale_x_ * (m[4] - origin_x_),
       static_cast<double>(canvas_height_) - scale_y_ * (m[5] - origin_y_));

    // Average canvas-space scale of that transform, used to size the guard
    // band of a non-extended end in axis units.
    const double det = transform.m00 * transform.m11 - transform.m01 * transform.m10;
    const double canvas_scale = std::sqrt(std::abs(det));

    BLGradient gradient;

    bool extend_start = instr.get_extend_start();
    bool extend_end = instr.get_extend_end();
    bool reversed = false;
    double axis_length = 0.0;

    if (instr.get_geometry() == SHADING_GEOMETRY_AXIAL)
      {
        if (coords.size() < 4)
          {
            LOG_S(WARNING) << "render_shading: axial shading " << instr.get_key()
                           << " has " << coords.size() << " coordinates, skipping";
            return;
          }

        const double dx = coords[2] - coords[0];
        const double dy = coords[3] - coords[1];
        axis_length = std::sqrt(dx * dx + dy * dy);

        if (axis_length <= 0.0)
          {
            LOG_S(WARNING) << "render_shading: axial shading " << instr.get_key()
                           << " has a zero-length axis, skipping";
            return;
          }

        gradient = BLGradient(BLLinearGradientValues(coords[0], coords[1],
                                                     coords[2], coords[3]));
      }
    else
      {
        if (coords.size() < 6)
          {
            LOG_S(WARNING) << "render_shading: radial shading " << instr.get_key()
                           << " has " << coords.size() << " coordinates, skipping";
            return;
          }

        // Blend2D's radial gradient runs from a focal circle (offset 0) to a
        // center circle (offset 1) and expects the focal circle to be the
        // smaller one, so a PDF shading that shrinks is fed in reverse.
        reversed = coords[5] < coords[2];

        const std::size_t focal = reversed ? 3 : 0;
        const std::size_t center = reversed ? 0 : 3;

        axis_length = std::abs(coords[5] - coords[2]);

        if (axis_length <= 0.0)
          {
            LOG_S(WARNING) << "render_shading: radial shading " << instr.get_key()
                           << " has two equal radii, skipping";
            return;
          }

        gradient = BLGradient(BLRadialGradientValues(coords[center + 0],
                                                     coords[center + 1],
                                                     coords[focal + 0],
                                                     coords[focal + 1],
                                                     coords[center + 2],
                                                     coords[focal + 2]));

        if (reversed)
          {
            std::swap(extend_start, extend_end);
          }
      }

    gradient.set_extend_mode(BL_EXTEND_MODE_PAD);
    gradient.set_transform(transform);

    // Guard band for a non-extended end: wide enough to stay sub-pixel on the
    // canvas, so the transparent-to-colour transition is not visible.
    const double axis_pixels = std::max(1.0, axis_length * canvas_scale);
    const double guard = std::min(0.25, 0.5 / axis_pixels);

    // The ramp in gradient-offset order. A reversed radial shading flips both
    // the offsets and the traversal, so everything below works on offsets that
    // already ascend.
    std::vector<std::pair<double, std::array<int, 3>>> ramp;
    ramp.reserve(stops.size());
    for (std::size_t i = 0; i < stops.size(); i++)
      {
        const shading_stop& stop = reversed ? stops[stops.size() - 1 - i] : stops[i];
        ramp.emplace_back(reversed ? 1.0 - stop.get_offset() : stop.get_offset(),
                          stop.get_rgb());
      }

    // Colour of the ramp at an arbitrary offset, so a guard stop can carry the
    // colour of the point it replaces.
    auto color_at = [&ramp](double offset) -> std::array<int, 3>
      {
        if (offset <= ramp.front().first) { return ramp.front().second; }
        if (offset >= ramp.back().first)  { return ramp.back().second; }

        for (std::size_t i = 1; i < ramp.size(); i++)
          {
            const double o0 = ramp[i - 1].first;
            const double o1 = ramp[i].first;
            if (offset > o1) { continue; }

            const double f = (o1 > o0) ? (offset - o0) / (o1 - o0) : 0.0;
            const auto& c0 = ramp[i - 1].second;
            const auto& c1 = ramp[i].second;

            return {static_cast<int>(std::lround(c0[0] + f * (c1[0] - c0[0]))),
                    static_cast<int>(std::lround(c0[1] + f * (c1[1] - c0[1]))),
                    static_cast<int>(std::lround(c0[2] + f * (c1[2] - c0[2])))};
          }

        return ramp.back().second;
      };

    const double low = extend_start ? 0.0 : guard;
    const double high = extend_end ? 1.0 : 1.0 - guard;

    if (not extend_start)
      {
        // pad replicates this stop backwards, leaving t < 0 unpainted
        gradient.add_stop(0.0, make_rgba32(color_at(low), 0.0));
        gradient.add_stop(low, make_rgba32(color_at(low), fill_alpha));
      }

    for (const auto& entry : ramp)
      {
        if (entry.first < low or entry.first > high) { continue; }

        gradient.add_stop(entry.first, make_rgba32(entry.second, fill_alpha));
      }

    if (not extend_end)
      {
        gradient.add_stop(high, make_rgba32(color_at(high), fill_alpha));
        gradient.add_stop(1.0, make_rgba32(color_at(high), 0.0));
      }

    // `sh` covers the whole clip region, so the fill is the entire canvas and
    // the clip is what bounds it. With no clip at all the region is the page
    // itself; with a clip that cannot be applied an unbounded fill would flood
    // the page, which is worse than not painting at all.
    const BLRect canvas_rect(0.0, 0.0,
                             static_cast<double>(canvas_width_),
                             static_cast<double>(canvas_height_));

    BLContext& ctx = page_context();

    const bool blend_active = push_blend_mode(ctx, instr.get_blend_mode());

    shading_clip clip;
    const bool has_clip_state = instr.has_clip_state();

    if (has_clip_state)
      {
        ctx.save();
        clip = apply_shading_clip(ctx, instr.get_clip_state(), canvas_rect);

        if (clip.empty)
          {
            ctx.restore();
            if (blend_active) { ctx.restore(); }
            LOG_S(INFO) << "render_shading: shading " << instr.get_key()
                        << " is fully clipped away";
            return;
          }

        if (not clip.bounded)
          {
            ctx.restore();
            if (blend_active) { ctx.restore(); }
            LOG_S(WARNING) << "render_shading: shading " << instr.get_key()
                           << " has a clip that could not be applied, skipping"
                           << " rather than flooding the page";
            return;
          }
      }

    ctx.set_fill_style(gradient);

    if (clip.has_path)
      {
        ctx.set_fill_rule(instr.get_clip_state().get_rule() == CLIP_RULE_EVEN_ODD
                            ? BL_FILL_RULE_EVEN_ODD
                            : BL_FILL_RULE_NON_ZERO);
        ctx.fill_path(clip.path);
      }
    else
      {
        ctx.fill_rect(canvas_rect);
      }

    if (has_clip_state)
      {
        ctx.restore();
      }

    if (blend_active)
      {
        ctx.restore();
      }
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
    utils::scoped_timer phase_timer(timings_, pdf_timings::KEY_RENDER_GET_CANVAS);


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
