//-*-C++-*-

#ifndef PDF_RENDER_CONFIG_H
#define PDF_RENDER_CONFIG_H

#include <array>
#include <cstddef>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace pdflib
{

  // ---------------------------------------------------------------------------
  // render_config
  //
  // Controls optional rendering behaviours for any renderer specialisation.
  // ---------------------------------------------------------------------------

  class render_config
  {
  public:

    // Render the glyph outline for each text cell.
    // When false and draw_text_bbox is true, only the bounding quad is drawn.
    bool render_text = true;

    // Paint vector shapes (fills and strokes). When false, shape
    // instructions are skipped entirely.
    bool render_shapes = true;

    // Paint shading instructions. Kept separate from render_shapes so crashes
    // or fidelity issues in gradients can be isolated from vector paths.
    bool render_shadings = true;

    // Apply non-rectangular clip paths through raster masks. When false, only
    // simple rectangular clips are applied; non-rectangular clips are ignored.
    bool render_non_rect_clip_masks = true;

    // Minimum stroke width in device pixels. PDF hairlines (`0 w`) and
    // sub-pixel strokes are promoted to this width so they remain visible.
    float min_stroke_width = 1.0f;

    // Draw the bounding quad of each text cell as a thin blue outline.
    bool draw_text_bbox = false;

    // Draw the text baseline origin as a small red dot.
    bool draw_text_basepoint = false;

    // Draw the bounds of each widget (form-field) annotation as a translucent
    // filled quad with a solid border, in color_widgets. This is a debug
    // visualisation with no counterpart in a viewer's output, so it lowers the
    // rendering-quality scores of documents with interactive fields (see
    // docs/quality_benchmarks.md) and is off by default. It is independent of
    // the field content: the widget's /AP appearance stream is decoded into
    // regular text and shape instructions, which are drawn either way.
    bool display_widgets = false;

    // RGB triple (0-255) used for the widget overlay drawn when
    // display_widgets is true. The fill is the same color at 40% alpha, the
    // border is fully opaque.
    std::array<int, 3> color_widgets = {0x00, 0x99, 0xFF};

    // Uniformly rescale measured glyph outlines so the rendered bbox fits
    // inside the target glyph bbox while matching either the target width or
    // target height exactly. Only applies when a glyph bbox is available.
    bool fit_glyph_bbox_to_target = false;

    // Try to resolve the PDF font name / base_font to a system font file.
    // When false the renderer always uses the hardcoded fallback font
    // (Helvetica / Arial) without any name lookup.
    bool resolve_fonts = true;

    // Prefer the embedded font program over system font resolution when the
    // text instruction carries one. SFNT programs (/FontFile2, /FontFile3
    // /OpenType) load natively in Blend2D; Type 1 and bare CFF programs
    // (/FontFile, /FontFile3 /Type1C, /CIDFontType0C) render as
    // FreeType-decomposed outline paths. Cells whose glyphs are missing from
    // the embedded (usually subset) font fall back to the system-resolved
    // font, so turning this off only forces the pre-embedded behaviour for
    // A/B comparisons. Independent of resolve_fonts: with resolve_fonts=false
    // the fallback is the hardcoded font instead of a name lookup.
    bool use_embedded_fonts = true;

    // Minimum Jaccard similarity required when fuzzy-matching a PDF font name
    // to a system font file.  Candidates below this threshold are rejected and
    // the hardcoded fallback font is used instead.  Range [0, 1]; lower values
    // accept weaker matches, higher values are more strict.
    float font_similarity_cutoff = 0.75f;

    // Maximum glyph outline bounding boxes cached by each render worker. The
    // cache is worker-local, so this is not multiplied by the number of pages.
    // Zero disables caching.
    std::size_t glyph_bbox_cache_capacity = 65536;

    // Target render scale in multiples of the PDF page size (72 ppi baseline).
    // -1 means "disabled". Mutually exclusive with canvas_width/canvas_height.
    float scale = -1.0f;

    // Target canvas dimensions in pixels.  -1 means "use the PDF page size".
    // If only one is set the other is derived to preserve the page aspect ratio.
    int canvas_width  = -1;
    int canvas_height = -1;
  };

  inline void validate_render_config(const render_config& config)
  {
    const bool have_width = config.canvas_width > 0;
    const bool have_height = config.canvas_height > 0;
    const bool have_scale = config.scale > 0.0f;

    if(config.scale != -1.0f and config.scale <= 0.0f)
      {
        throw std::runtime_error("render_config.scale must be > 0 or -1");
      }

    if(config.canvas_width != -1 and config.canvas_width <= 0)
      {
        throw std::runtime_error("render_config.canvas_width must be > 0 or -1");
      }

    if(config.canvas_height != -1 and config.canvas_height <= 0)
      {
        throw std::runtime_error("render_config.canvas_height must be > 0 or -1");
      }

    if(config.min_stroke_width < 0.0f)
      {
        throw std::runtime_error("render_config.min_stroke_width must be >= 0");
      }

    for(int channel : config.color_widgets)
      {
        if(channel < 0 or channel > 255)
          {
            throw std::runtime_error("render_config.color_widgets channels must be in [0, 255]");
          }
      }

    if(have_scale and (have_width or have_height))
      {
        throw std::runtime_error(
            "render_config.scale cannot be combined with canvas_width or canvas_height");
      }
  }

  // Number of pixels needed to cover a page extent expressed in canvas units.
  // The page box is a real-valued rectangle, so a page of 595.2756 points at
  // scale 2 needs 1191 pixels, not 1190: rounding down would drop the last
  // fractional column of the page. Rounding up is also what PDFium does
  // (FPDF_GetPageWidthF times the scale, rounded up), which keeps our canvas
  // dimensions identical to a pypdfium2 render of the same page.
  //
  // The epsilon absorbs the noise of the box subtraction that produced the
  // extent, so a page that is an exact 612 x 792 does not gain a pixel from a
  // 612.0000000000001 arithmetic result.
  inline int pixels_for_extent(double extent)
  {
    return static_cast<int>(std::ceil(extent - 1e-6));
  }

  inline std::pair<int, int> resolve_canvas_size(
      double pdf_width,
      double pdf_height,
      const render_config& config)
  {
    validate_render_config(config);

    int width = pixels_for_extent(pdf_width);
    int height = pixels_for_extent(pdf_height);

    const bool have_width = config.canvas_width > 0;
    const bool have_height = config.canvas_height > 0;
    const bool have_scale = config.scale > 0.0f;

    if(have_scale)
      {
        width = pixels_for_extent(pdf_width * config.scale);
        height = pixels_for_extent(pdf_height * config.scale);
      }
    else if(have_width and have_height)
      {
        width = config.canvas_width;
        height = config.canvas_height;
      }
    else if(have_width)
      {
        width = config.canvas_width;
        height = pixels_for_extent(pdf_height * width / pdf_width);
      }
    else if(have_height)
      {
        height = config.canvas_height;
        width = pixels_for_extent(pdf_width * height / pdf_height);
      }

    if(width <= 0)
      {
        width = 1;
      }
    if(height <= 0)
      {
        height = 1;
      }

    return {width, height};
  }

}

#endif
