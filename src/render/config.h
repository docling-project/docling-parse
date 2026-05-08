//-*-C++-*-

#ifndef PDF_RENDER_CONFIG_H
#define PDF_RENDER_CONFIG_H

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

    // Draw the bounding quad of each text cell as a thin blue outline.
    bool draw_text_bbox = false;

    // Draw the text baseline origin as a small red dot.
    bool draw_text_basepoint = false;

    // Uniformly rescale measured glyph outlines so the rendered bbox fits
    // inside the target glyph bbox while matching either the target width or
    // target height exactly. Only applies when a glyph bbox is available.
    bool fit_glyph_bbox_to_target = false;

    // Try to resolve the PDF font name / base_font to a system font file.
    // When false the renderer always uses the hardcoded fallback font
    // (Helvetica / Arial) without any name lookup.
    bool resolve_fonts = true;

    // Minimum Jaccard similarity required when fuzzy-matching a PDF font name
    // to a system font file.  Candidates below this threshold are rejected and
    // the hardcoded fallback font is used instead.  Range [0, 1]; lower values
    // accept weaker matches, higher values are more strict.
    float font_similarity_cutoff = 0.75f;

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

    if(have_scale and (have_width or have_height))
      {
        throw std::runtime_error(
            "render_config.scale cannot be combined with canvas_width or canvas_height");
      }
  }

  inline std::pair<int, int> resolve_canvas_size(
      int pdf_width,
      int pdf_height,
      const render_config& config)
  {
    validate_render_config(config);

    int width = pdf_width;
    int height = pdf_height;

    const bool have_width = config.canvas_width > 0;
    const bool have_height = config.canvas_height > 0;
    const bool have_scale = config.scale > 0.0f;

    if(have_scale)
      {
        width = static_cast<int>(std::round(static_cast<double>(pdf_width) * config.scale));
        height = static_cast<int>(std::round(static_cast<double>(pdf_height) * config.scale));
      }
    else if(have_width and have_height)
      {
        width = config.canvas_width;
        height = config.canvas_height;
      }
    else if(have_width)
      {
        width = config.canvas_width;
        height = static_cast<int>(
            std::round(static_cast<double>(pdf_height) * width / pdf_width));
      }
    else if(have_height)
      {
        height = config.canvas_height;
        width = static_cast<int>(
            std::round(static_cast<double>(pdf_width) * height / pdf_height));
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
