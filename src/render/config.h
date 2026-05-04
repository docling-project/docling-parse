//-*-C++-*-

#ifndef PDF_RENDER_CONFIG_H
#define PDF_RENDER_CONFIG_H

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

    // Try to resolve the PDF font name / base_font to a system font file.
    // When false the renderer always uses the hardcoded fallback font
    // (Helvetica / Arial) without any name lookup.
    bool resolve_fonts = true;

    // Minimum Jaccard similarity required when fuzzy-matching a PDF font name
    // to a system font file.  Candidates below this threshold are rejected and
    // the hardcoded fallback font is used instead.  Range [0, 1]; lower values
    // accept weaker matches, higher values are more strict.
    float font_similarity_cutoff = 0.75f;

    // Target canvas dimensions in pixels.  -1 means "use the PDF page size".
    // If only one is set the other is derived to preserve the page aspect ratio.
    int canvas_width  = -1;
    int canvas_height = -1;
  };

}

#endif
