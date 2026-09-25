//-*-C++-*-

#ifndef PAGE_ITEM_CELLS_SANITATOR_H
#define PAGE_ITEM_CELLS_SANITATOR_H

namespace pdflib
{

  template<>
  class page_item_sanitator<PAGE_CELLS>
  {
  public:
    /**
     * @brief Constructs a stateless cell sanitizer.
     *
     * All contraction state is local to an individual method call, so a
     * sanitizer instance can be reused for multiple pages.
     */
    page_item_sanitator() = default;

    /** @brief Destroys the sanitizer; no external resources are owned. */
    ~page_item_sanitator() = default;

    /**
     * @brief Serializes active cells into records suitable for downstream use.
     *
     * Each output record contains its sequential index, rotated quadrilateral,
     * text, font metadata, rendering mode, widget flag, and writing direction.
     * Inactive cells are omitted. The input container and its cells are not
     * modified.
     *
     * @param cells Cells to serialize, in their existing reading order.
     * @return A JSON array containing one object per active cell.
     *
     * @par Complexity
     * Linear in the number of input cells.
     */
    nlohmann::json to_records(page_item<PAGE_CELLS>& cells);

    /**
     * @brief Contracts character cells into inferred word cells.
     *
     * Characters are first divided into rotation-aware line runs. Within each
     * run, explicit space cells are used as authoritative word boundaries when
     * present. Lines without explicit spaces instead cluster normalized
     * inter-character gaps to distinguish ordinary placement from word
     * spacing. Each returned word receives the smallest rectangle, aligned
     * with its writing direction, that encloses all constituent cells.
     *
     * The input cells are inspected but not modified. The current algorithm is
     * geometry-driven and does not use contraction parameters from @p config;
     * the argument is retained as part of the public decoder interface.
     *
     * @param cells Character-level cells in content/reading order.
     * @param config Decoder configuration retained for API compatibility.
     * @return Newly allocated cells containing one entry per inferred word.
     *
     * @par Complexity
     * Linear in the number of input cells because gap clustering uses a fixed
     * number of iterations.
     */
    page_item<PAGE_CELLS> create_word_cells(page_item<PAGE_CELLS>& cells,
                                             const decode_config& config);

    /**
     * @brief Contracts character cells into inferred text-line cells.
     *
     * Uses the same rotation-aware line and word analysis as
     * create_word_cells(), then joins each run's words with one normalized
     * ASCII space. The resulting bounding quadrilateral is the maximum
     * writing-direction-aligned envelope of every character in the line.
     *
     * The input cells are inspected but not modified. The current algorithm
     * does not use contraction parameters from @p config.
     *
     * @param cells Character-level cells in content/reading order.
     * @param config Decoder configuration retained for API compatibility.
     * @return Newly allocated cells containing one entry per inferred line.
     *
     * @par Complexity
     * Linear in the number of input cells.
     */
    page_item<PAGE_CELLS> create_line_cells(page_item<PAGE_CELLS>& cells,
                                             const decode_config& config);

    /**
     * @brief Removes consecutive duplicate cells from a cell sequence.
     *
     * Two neighboring active cells are duplicates when their text and font
     * name match and all corresponding rotated corners are closer than
     * @p eps. The latter cell is deactivated, after which all inactive cells
     * are physically removed from the container.
     *
     * @param[in,out] cells Cell sequence to clean in place.
     * @param eps Maximum Euclidean distance between corresponding corners.
     *
     * @par Complexity
     * Linear in the number of input cells.
     */
    void remove_adjacent_cells(page_item<PAGE_CELLS>& cells, double eps);

    /**
     * @brief Removes matching cells that may be separated in the sequence.
     *
     * For every active cell, later cells with identical text and font name are
     * compared corner by corner. A later cell is removed when every rotated
     * corner lies within @p eps of the earlier cell. When @p same_line is true,
     * the scan stops after the rotated y-coordinate differs by more than
     * @p eps; this assumes the input is ordered by line position.
     *
     * This compatibility cleanup is intentionally separate from word and line
     * construction so its quadratic worst-case cost is not paid by contraction.
     *
     * @param[in,out] cells Cell sequence to deduplicate in place.
     * @param eps Maximum Euclidean distance between corresponding corners.
     * @param same_line Restrict comparisons to the current ordered text line.
     *
     * @par Complexity
     * O(n^2) in the worst case; the same-line early exit can reduce the scan.
     */
    void remove_duplicate_cells(page_item<PAGE_CELLS>& cells,
                                double eps,
                                bool same_line);

    /**
     * @brief Normalizes extracted text stored in every cell.
     *
     * Applies the substitutions in `text_constants::replacements`, then
     * converts PDF-style names such as `/A_B` and `/A_B_C` into `AB` and
     * `ABC`. Geometry, font metadata, ordering, and active state are unchanged.
     *
     * @param[in,out] cells Cells whose text fields are normalized in place.
     *
     * @par Complexity
     * Linear in the number of cells times the configured replacement count and
     * the cost of the individual string replacements.
     */
    void sanitize_text(page_item<PAGE_CELLS>& cells);

    /**
     * @brief Legacy in-place entry point for word or line contraction.
     *
     * Runs the geometry-based contractor and replaces @p cells with either its
     * word output or its line output. The former numeric tuning parameters no
     * longer influence the algorithm and are accepted only for source/API
     * compatibility. New code should call create_word_cells() or
     * create_line_cells() to state the desired result explicitly.
     *
     * @param[in,out] cells Character cells to replace with contracted cells.
     * @param horizontal_cell_tolerance Deprecated and ignored.
     * @param enforce_same_font Deprecated and ignored.
     * @param space_width_factor_for_merge Deprecated and ignored.
     * @param space_width_factor_for_merge_with_space Deprecated and ignored.
     * @param block_spaces Selects word cells when true and line cells when
     * false.
     *
     * @par Complexity
     * Linear in the number of input cells.
     */
    void sanitize_bbox(page_item<PAGE_CELLS>& cells,
                       double horizontal_cell_tolerance,
                       bool enforce_same_font,
                       double space_width_factor_for_merge,
                       double space_width_factor_for_merge_with_space,
                       bool block_spaces);

  private:
    /**
     * @brief A maximal sequence of cells judged to occupy one text line.
     *
     * Cells remain in source order. Explicit space cells are preserved inside
     * the run as semantic word-boundary markers even when their geometry is
     * empty. `has_semantic_spaces` records whether those authoritative
     * boundaries occur anywhere in the run, allowing ordinary inner-word
     * placement variation to be treated differently from large positioned
     * field jumps.
     */
    struct line_run
    {
      std::vector<page_item<PAGE_CELL>> cells;
      bool has_semantic_spaces = false;
    };

    /**
     * @brief Holds both views produced by one contraction pass.
     *
     * Building words and lines together prevents callers that need both from
     * applying the same geometric analysis independently.
     */
    struct contraction_result
    {
      page_item<PAGE_CELLS> words;
      page_item<PAGE_CELLS> lines;
    };

    /**
     * @brief Measures a cell perpendicular to its writing direction.
     *
     * Returns the average length of the `(r0,r3)` and `(r1,r2)` side edges.
     * Averaging tolerates slightly non-rectangular glyph quadrilaterals.
     *
     * @param cell Character or aggregate cell in rotated coordinates.
     * @return Average side-edge length in page-coordinate units.
     * @par Complexity O(1).
     */
    static double cell_height(const page_item<PAGE_CELL>& cell);

    /**
     * @brief Obtains a stable normalization width for inter-character gaps.
     *
     * Uses the cell's reported font-space width when it is finite and positive.
     * Missing or degenerate values fall back to half the geometric cell height,
     * clamped away from zero.
     *
     * @param cell Cell supplying font and geometric measurements.
     * @return Positive width in page-coordinate units.
     * @par Complexity O(1).
     */
    static double cell_space_width(const page_item<PAGE_CELL>& cell);

    /**
     * @brief Returns the device-space distance advanced by one character.
     *
     * Prefers the transient PDF cursor origin and advance endpoint. Legacy or
     * synthetic cells without placement metadata fall back to their reported
     * space width, keeping the contractor usable for manually constructed
     * PAGE_CELL inputs.
     *
     * @param cell Character cell whose local placement scale is required.
     * @return A finite positive normalization length.
     * @par Complexity O(1).
     */
    static double cell_advance_length(const page_item<PAGE_CELL>& cell);

    /**
     * @brief Distinguishes real whitespace from a suppressed glyph marker.
     *
     * Undecodable but visible glyphs are exposed as a single space when
     * `keep_glyphs` is disabled. Their transient suppression flag prevents
     * that placeholder from becoming a hard word boundary or disappearing
     * from line geometry. Genuine PDF spaces remain semantic boundaries.
     *
     * @param cell Character cell to classify.
     * @return True only for actual whitespace cells.
     * @par Complexity O(length of the cell text).
     */
    static bool is_semantic_space(const page_item<PAGE_CELL>& cell);

    /**
     * @brief Measures the directed gap from one cell to the next.
     *
     * Projects the vector from @p lhs's PDF cursor advance endpoint to @p
     * rhs's cursor origin onto the physical writing axis. This measures text
     * placement rather than painted side bearings. Legacy cells without
     * placement metadata fall back to their facing ink edges.
     *
     * @param lhs Previous cell, which defines direction and trailing edge.
     * @param rhs Following cell, which supplies the leading edge.
     * @return Signed longitudinal gap in page-coordinate units.
     * @par Complexity O(1).
     */
    static double forward_gap(const page_item<PAGE_CELL>& lhs,
                              const page_item<PAGE_CELL>& rhs);

    /**
     * @brief Recognizes a nearby off-baseline component placed by cursor
     * rewind.
     *
     * Mathematical limits, fraction parts, radical contents, and assembled
     * delimiters are commonly emitted after touching or rewinding the text
     * cursor. The transverse displacement must also remain within two nominal
     * character heights; this rejects unrelated figure labels that reuse the
     * same x-coordinate several text lines away.
     *
     * @param lhs Previous visible character.
     * @param rhs Candidate positioned component.
     * @return True only for a geometrically local positioned attachment.
     * @par Complexity O(1).
     */
    static bool is_local_positioned_attachment(
      const page_item<PAGE_CELL>& lhs,
      const page_item<PAGE_CELL>& rhs);

    /**
     * @brief Identifies cells emitted by a mathematical layout context.
     *
     * TeX/math font-family names and unambiguous non-ASCII mathematical
     * operators qualify. Ordinary ASCII punctuation alone deliberately does
     * not, because figure labels often align punctuation and headings at the
     * same x-coordinate on neighboring rows.
     *
     * @param cell Character cell to classify.
     * @return True when off-baseline positioning is plausibly mathematical.
     * @par Complexity O(length of the font name and cell text).
     */
    static bool has_math_positioning_semantics(
      const page_item<PAGE_CELL>& cell);

    /**
     * @brief Tests whether two characters may belong to one word despite a
     * baseline shift.
     *
     * Characters on approximately the same baseline are compatible. A larger
     * transverse shift is also accepted when the PDF cursor touches or rewinds
     * along the writing axis, which is how typesetters place fraction parts,
     * limits, radical contents, and assembled delimiters. Forward movement to
     * a different baseline remains a boundary, preventing nearby prose from
     * being absorbed into a following display equation.
     *
     * @param lhs Previous visible character.
     * @param rhs Candidate character to append to the same word.
     * @return True when their placement relationship is word-compatible.
     * @par Complexity O(1).
     */
    static bool word_baselines_compatible(
      const page_item<PAGE_CELL>& lhs,
      const page_item<PAGE_CELL>& rhs);

    /**
     * @brief Measures transverse overlap between two cells' facing edges.
     *
     * Projects the trailing edge of @p lhs and leading edge of @p rhs onto the
     * axis normal to the writing direction, then intersects the two projected
     * intervals. This makes the same-line test invariant under arbitrary text
     * rotation, including vertical and 45-degree text.
     *
     * @param lhs Previous cell, which defines the local writing frame.
     * @param rhs Candidate following cell.
     * @return Positive overlap length, zero for touching edges, or a negative
     * value whose magnitude is the transverse separation.
     * @par Complexity O(1).
     */
    static double facing_edge_overlap(const page_item<PAGE_CELL>& lhs,
                                      const page_item<PAGE_CELL>& rhs);

    /**
     * @brief Decides whether two visible cells continue the same text line.
     *
     * Requires nearly parallel writing axes, a cursor-placement gap between
     * one backward and four forward neighboring character advances, and
     * positive facing-edge overlap. The overlap requirement is relaxed only
     * when placement-aware cursors touch or rewind, which is how vertically
     * offset scripts, fraction parts, and stacked delimiters are positioned.
     * The gap bounds still separate source-order column resets and forward
     * layout jumps. Word boundaries are inferred later.
     *
     * @param lhs Previous visible cell in content order.
     * @param rhs Candidate following visible cell.
     * @return True when @p rhs should remain in @p lhs's line run.
     * @par Complexity O(1).
     */
    static bool continues_line(const page_item<PAGE_CELL>& lhs,
                               const page_item<PAGE_CELL>& rhs);

    /**
     * @brief Partitions active, non-empty cells into rotation-aware line runs.
     *
     * Each visible cell is compared with the most recent non-space cell in the
     * current run. Explicit spaces are appended without a geometry test because
     * PDFs often encode them with zero-area quads; they cannot begin a new run.
     * Inactive and empty-text cells are omitted.
     *
     * @param cells Cells in content/reading order; they are not modified.
     * @return Ordered maximal line runs containing copied cells.
     * @par Complexity O(n), since only trailing explicit spaces are skipped.
     */
    static std::vector<line_run> collect_line_runs(page_item<PAGE_CELLS>& cells);

    /**
     * @brief Infers a line-local normalized threshold for word separation.
     *
     * Non-space cursor-placement gaps are divided by the neighboring cells'
     * average advances. A fixed eight-iteration, one-dimensional
     * two-cluster k-means separates ordinary character gaps from dilated word
     * gaps. Sparse or insufficiently separated samples use a conservative
     * default threshold of 0.25; valid inferred thresholds are clamped to at
     * least 0.18. Runs containing explicit spaces do not use this inferred
     * threshold: those source-level boundaries are authoritative, with only
     * jumps larger than one character advance allowed to add a boundary.
     *
     * @param run One line of cells in source order.
     * @return Dimensionless normalized gap above which a new word begins.
     * @par Complexity O(n), with a constant number of clustering iterations.
     */
    static double infer_word_gap(const line_run& run);

    /**
     * @brief Appends one cell to a text aggregate and expands its geometry.
     *
     * Text is appended or prepended according to writing direction, optionally
     * inserting one normalized ASCII space. All eight corners are projected
     * into the aggregate's local writing frame to compute the maximum enclosing
     * rotation-aligned rectangle. The axis-aligned `x0/y0/x1/y1` bounds are then
     * refreshed from that rectangle. If neither cell supplies a usable writing
     * axis, text is still combined but geometry remains unchanged.
     *
     * @param[in,out] aggregate Existing word or line to extend.
     * @param next Cell or word being incorporated; it is not modified.
     * @param insert_space Insert one space between the two text fragments.
     * @par Complexity O(1).
     */
    static void append_cell(page_item<PAGE_CELL>& aggregate,
                            page_item<PAGE_CELL>& next,
                            bool insert_space);

    /**
     * @brief Constructs word and line cells in a single contraction pass.
     *
     * Builds line runs and treats explicit space cells as authoritative
     * boundaries. Runs without them infer a line-local cursor-placement gap
     * threshold. Runs with them tolerate ordinary inner-word placement
     * variation, while still splitting absolute-positioned fields separated
     * by more than one neighboring character advance. Words are then merged
     * with normalized spaces into one line. Both levels retain maximum
     * enclosing rotation-aligned bounding boxes.
     * Visible glyphs without a recoverable Unicode mapping retain their U+FFFD
     * replacement character, so their aggregates remain honest, non-empty
     * public cells. Blank unresolved glyph programs are classified earlier as
     * semantic spaces and remain boundaries rather than words.
     *
     * @param cells Character cells in content/reading order; not modified.
     * @return Word and line collections derived from the same analysis.
     * @par Complexity O(n), including fixed-iteration gap clustering.
     */
    static contraction_result contract(page_item<PAGE_CELLS>& cells);
  };

  inline double page_item_sanitator<PAGE_CELLS>::cell_height(
      const page_item<PAGE_CELL>& cell)
  {
    const double h0 = utils::values::distance(cell.r_x0, cell.r_y0,
                                              cell.r_x3, cell.r_y3);
    const double h1 = utils::values::distance(cell.r_x1, cell.r_y1,
                                              cell.r_x2, cell.r_y2);
    return 0.5 * (h0 + h1);
  }

  inline double page_item_sanitator<PAGE_CELLS>::cell_space_width(
      const page_item<PAGE_CELL>& cell)
  {
    if(std::isfinite(cell.space_width) and cell.space_width > 1.e-6)
      {
        return cell.space_width;
      }
    return std::max(1.e-6, 0.5 * cell_height(cell));
  }

  inline double page_item_sanitator<PAGE_CELLS>::cell_advance_length(
      const page_item<PAGE_CELL>& cell)
  {
    if(cell.has_text_placement)
      {
        const double advance = std::hypot(
          cell.text_advance_x - cell.text_origin_x,
          cell.text_advance_y - cell.text_origin_y);
        if(std::isfinite(advance) and advance > 1.e-6)
          {
            return advance;
          }
      }
    return cell_space_width(cell);
  }

  inline bool page_item_sanitator<PAGE_CELLS>::is_semantic_space(
      const page_item<PAGE_CELL>& cell)
  {
    return cell.is_pdf_word_space or
           (not cell.text_is_suppressed_glyph and
            utils::string::is_space(cell.text));
  }

  inline double page_item_sanitator<PAGE_CELLS>::forward_gap(
      const page_item<PAGE_CELL>& lhs,
      const page_item<PAGE_CELL>& rhs)
  {
    if(lhs.has_text_placement and rhs.has_text_placement)
      {
        return (rhs.text_origin_x - lhs.text_advance_x) *
                 lhs.writing_axis_x +
               (rhs.text_origin_y - lhs.text_advance_y) *
                 lhs.writing_axis_y;
      }

    double ux = lhs.r_x1 - lhs.r_x0;
    double uy = lhs.r_y1 - lhs.r_y0;
    double norm = std::hypot(ux, uy);
    if(norm <= 1.e-9)
      {
        return std::numeric_limits<double>::infinity();
      }
    ux /= norm;
    uy /= norm;

    const bool ltr = lhs.left_to_right;
    if(not ltr)
      {
        ux = -ux;
        uy = -uy;
      }

    const double tail_x = ltr
      ? 0.5 * (lhs.r_x1 + lhs.r_x2)
      : 0.5 * (lhs.r_x0 + lhs.r_x3);
    const double tail_y = ltr
      ? 0.5 * (lhs.r_y1 + lhs.r_y2)
      : 0.5 * (lhs.r_y0 + lhs.r_y3);
    const double head_x = ltr
      ? 0.5 * (rhs.r_x0 + rhs.r_x3)
      : 0.5 * (rhs.r_x1 + rhs.r_x2);
    const double head_y = ltr
      ? 0.5 * (rhs.r_y0 + rhs.r_y3)
      : 0.5 * (rhs.r_y1 + rhs.r_y2);
    return (head_x - tail_x) * ux + (head_y - tail_y) * uy;
  }

  inline bool page_item_sanitator<PAGE_CELLS>::word_baselines_compatible(
      const page_item<PAGE_CELL>& lhs,
      const page_item<PAGE_CELL>& rhs)
  {
    if(not lhs.has_text_placement or not rhs.has_text_placement)
      {
        return true;
      }

    const double nx = -lhs.writing_axis_y;
    const double ny = lhs.writing_axis_x;
    const double baseline_offset = std::abs(
      (rhs.text_origin_x - lhs.text_origin_x) * nx +
      (rhs.text_origin_y - lhs.text_origin_y) * ny);
    const double nominal_height = std::max(
      1.e-6,
      std::min(lhs.nominal_text_height, rhs.nominal_text_height));
    if(baseline_offset <= 0.35 * nominal_height)
      {
        return true;
      }

    return is_local_positioned_attachment(lhs, rhs);
  }

  inline bool page_item_sanitator<PAGE_CELLS>::is_local_positioned_attachment(
      const page_item<PAGE_CELL>& lhs,
      const page_item<PAGE_CELL>& rhs)
  {
    if(not lhs.has_text_placement or not rhs.has_text_placement)
      {
        return false;
      }

    const double scale = std::max(cell_advance_length(lhs),
                                  cell_advance_length(rhs));
    if(forward_gap(lhs, rhs) > 0.10 * scale)
      {
        return false;
      }

    const double nx = -lhs.writing_axis_y;
    const double ny = lhs.writing_axis_x;
    const double transverse_offset = std::abs(
      (rhs.text_origin_x - lhs.text_origin_x) * nx +
      (rhs.text_origin_y - lhs.text_origin_y) * ny);
    const double nominal_height = std::max(
      1.e-6,
      std::max(lhs.nominal_text_height, rhs.nominal_text_height));
    return transverse_offset <= 2.0 * nominal_height and
           (has_math_positioning_semantics(lhs) or
            has_math_positioning_semantics(rhs));
  }

  inline bool page_item_sanitator<PAGE_CELLS>::has_math_positioning_semantics(
      const page_item<PAGE_CELL>& cell)
  {
    const std::string font = utils::string::to_lower(cell.font_name);
    for(const char* token : {"cmmi", "cmsy", "cmex", "cmr", "cmbx",
                             "msam", "msbm", "math", "symbol", "mtextra"})
      {
        if(font.find(token) != std::string::npos)
          {
            return true;
          }
      }

    for(const char* symbol : {"∑", "∏", "∫", "√", "∞", "∈",
                              "∉", "≠", "≤", "≥", "⊂", "⊃",
                              "⊆", "⊇", "⊥", "∪", "∩", "⎛",
                              "⎜", "⎝", "⎞", "⎟", "⎠", "⎡",
                              "⎢", "⎣", "⎤", "⎥", "⎦"})
      {
        if(cell.text.find(symbol) != std::string::npos)
          {
            return true;
          }
      }
    return false;
  }

  inline double page_item_sanitator<PAGE_CELLS>::facing_edge_overlap(
      const page_item<PAGE_CELL>& lhs,
      const page_item<PAGE_CELL>& rhs)
  {
    double ux = lhs.r_x1 - lhs.r_x0;
    double uy = lhs.r_y1 - lhs.r_y0;
    const double norm = std::hypot(ux, uy);
    if(norm <= 1.e-9)
      {
        return 0.0;
      }
    ux /= norm;
    uy /= norm;
    if(not lhs.left_to_right)
      {
        ux = -ux;
        uy = -uy;
      }

    // Project the first character's trailing edge and the second character's
    // leading edge onto the axis normal to the writing direction. Their
    // intervals must overlap for the cells to be on the same text line. This
    // formulation is invariant under arbitrary page/text rotation.
    const double nx = -uy;
    const double ny = ux;
    const bool ltr = lhs.left_to_right;
    const double lhs_0 = ltr
      ? lhs.r_x1 * nx + lhs.r_y1 * ny
      : lhs.r_x0 * nx + lhs.r_y0 * ny;
    const double lhs_1 = ltr
      ? lhs.r_x2 * nx + lhs.r_y2 * ny
      : lhs.r_x3 * nx + lhs.r_y3 * ny;
    const double rhs_0 = ltr
      ? rhs.r_x0 * nx + rhs.r_y0 * ny
      : rhs.r_x1 * nx + rhs.r_y1 * ny;
    const double rhs_1 = ltr
      ? rhs.r_x3 * nx + rhs.r_y3 * ny
      : rhs.r_x2 * nx + rhs.r_y2 * ny;

    const double lhs_min = std::min(lhs_0, lhs_1);
    const double lhs_max = std::max(lhs_0, lhs_1);
    const double rhs_min = std::min(rhs_0, rhs_1);
    const double rhs_max = std::max(rhs_0, rhs_1);
    return std::min(lhs_max, rhs_max) - std::max(lhs_min, rhs_min);
  }

  inline bool page_item_sanitator<PAGE_CELLS>::continues_line(
      const page_item<PAGE_CELL>& lhs,
      const page_item<PAGE_CELL>& rhs)
  {
    double lux = lhs.has_text_placement
      ? lhs.writing_axis_x : lhs.r_x1 - lhs.r_x0;
    double luy = lhs.has_text_placement
      ? lhs.writing_axis_y : lhs.r_y1 - lhs.r_y0;
    double rux = rhs.has_text_placement
      ? rhs.writing_axis_x : rhs.r_x1 - rhs.r_x0;
    double ruy = rhs.has_text_placement
      ? rhs.writing_axis_y : rhs.r_y1 - rhs.r_y0;
    const double ln = std::hypot(lux, luy);
    const double rn = std::hypot(rux, ruy);
    if(ln <= 1.e-9 or rn <= 1.e-9)
      {
        return false;
      }
    lux /= ln; luy /= ln;
    rux /= rn; ruy /= rn;
    if(lux * rux + luy * ruy < 0.985)
      {
        return false;
      }

    // A very large same-baseline cursor jump is a column/block boundary, not
    // a line continuation. Character advances are local placement scales and
    // cannot be inflated by tall ink or a malformed font space metric.
    const double layout_scale = std::max(cell_advance_length(lhs),
                                         cell_advance_length(rhs));
    const double gap = forward_gap(lhs, rhs);
    if(facing_edge_overlap(lhs, rhs) <= 1.e-6 and
       not is_local_positioned_attachment(lhs, rhs))
      {
        return false;
      }
    return -layout_scale <= gap and gap <= 4.0 * layout_scale;
  }

  inline std::vector<typename page_item_sanitator<PAGE_CELLS>::line_run>
  page_item_sanitator<PAGE_CELLS>::collect_line_runs(
      page_item<PAGE_CELLS>& cells)
  {
    std::vector<line_run> runs;
    for(auto& cell : cells)
      {
        if(not cell.active or cell.text.empty())
          {
            continue;
          }

        if(runs.empty())
          {
            runs.emplace_back();
          }

        // Explicit spaces are semantic boundary markers. Some PDFs give them
        // a zero-sized quad, so they neither start a line nor participate in
        // the facing-edge geometry test.
        if(is_semantic_space(cell))
          {
            runs.back().has_semantic_spaces = true;
            runs.back().cells.push_back(cell);
            continue;
          }

        const page_item<PAGE_CELL>* previous_visible = nullptr;
        for(auto itr = runs.back().cells.rbegin();
            itr != runs.back().cells.rend(); ++itr)
          {
            if(not is_semantic_space(*itr))
              {
                previous_visible = &*itr;
                break;
              }
          }
        if(previous_visible != nullptr and
           not continues_line(*previous_visible, cell))
          {
            runs.emplace_back();
          }
        runs.back().cells.push_back(cell);
      }
    return runs;
  }

  inline double page_item_sanitator<PAGE_CELLS>::infer_word_gap(
      const line_run& run)
  {
    std::vector<double> gaps;
    const page_item<PAGE_CELL>* previous = nullptr;
    bool crossed_explicit_space = false;

    for(const auto& cell : run.cells)
      {
        if(is_semantic_space(cell))
          {
            crossed_explicit_space = true;
            continue;
          }
        if(previous != nullptr and not crossed_explicit_space)
          {
            const double scale = 0.5 * (cell_advance_length(*previous) +
                                        cell_advance_length(cell));
            const double normalized = std::max(0.0, forward_gap(*previous, cell)) /
                                      std::max(1.e-6, scale);
            gaps.push_back(normalized);
          }
        previous = &cell;
        crossed_explicit_space = false;
      }

    if(gaps.size() < 2)
      {
        return 0.25;
      }

    double low = *std::min_element(gaps.begin(), gaps.end());
    double high = *std::max_element(gaps.begin(), gaps.end());
    if(high < 0.18 or high - low < 0.08)
      {
        return 0.25;
      }

    // Fixed-iteration 1-D k-means. Its cost is O(number of gaps), with a
    // constant iteration count, and it discovers the local dilation between
    // normal character placement and word spacing.
    for(int iteration = 0; iteration < 8; ++iteration)
      {
        double low_sum = 0.0, high_sum = 0.0;
        std::size_t low_count = 0, high_count = 0;
        const double midpoint = 0.5 * (low + high);
        for(double gap : gaps)
          {
            if(gap <= midpoint)
              {
                low_sum += gap;
                ++low_count;
              }
            else
              {
                high_sum += gap;
                ++high_count;
              }
          }
        if(low_count == 0 or high_count == 0)
          {
            return 0.25;
          }
        low = low_sum / static_cast<double>(low_count);
        high = high_sum / static_cast<double>(high_count);
      }

    if(high < 0.18 or high - low < 0.08)
      {
        return 0.25;
      }
    return std::max(0.18, 0.5 * (low + high));
  }

  inline void page_item_sanitator<PAGE_CELLS>::append_cell(
      page_item<PAGE_CELL>& aggregate,
      page_item<PAGE_CELL>& next,
      bool insert_space)
  {
    const bool ltr = aggregate.left_to_right and next.left_to_right;
    if(ltr)
      {
        if(insert_space) { aggregate.text.push_back(' '); }
        aggregate.text += next.text;
      }
    else
      {
        aggregate.text = next.text + (insert_space ? " " : "") + aggregate.text;
        aggregate.left_to_right = false;
      }

    // Compute the maximum bbox in the text's own coordinate frame. Merely
    // retaining the first leading edge and the last trailing edge produces a
    // sloped top/bottom whenever intermediate glyphs have a larger ascent or
    // descent. Projecting every corner onto the writing and normal axes gives
    // the smallest rotation-aligned rectangle that encloses both bboxes.
    double ux = aggregate.r_x1 - aggregate.r_x0;
    double uy = aggregate.r_y1 - aggregate.r_y0;
    const double norm = std::hypot(ux, uy);
    if(norm <= 1.e-9)
      {
        ux = next.r_x1 - next.r_x0;
        uy = next.r_y1 - next.r_y0;
      }
    const double adjusted_norm = std::hypot(ux, uy);
    if(adjusted_norm <= 1.e-9)
      {
        return;
      }
    ux /= adjusted_norm;
    uy /= adjusted_norm;
    const double nx = -uy;
    const double ny = ux;

    const std::array<std::pair<double, double>, 8> points = {{
      {aggregate.r_x0, aggregate.r_y0},
      {aggregate.r_x1, aggregate.r_y1},
      {aggregate.r_x2, aggregate.r_y2},
      {aggregate.r_x3, aggregate.r_y3},
      {next.r_x0, next.r_y0},
      {next.r_x1, next.r_y1},
      {next.r_x2, next.r_y2},
      {next.r_x3, next.r_y3}
    }};

    double u_min = std::numeric_limits<double>::infinity();
    double u_max = -std::numeric_limits<double>::infinity();
    double n_min = std::numeric_limits<double>::infinity();
    double n_max = -std::numeric_limits<double>::infinity();
    for(const auto& point : points)
      {
        const double u = point.first * ux + point.second * uy;
        const double n = point.first * nx + point.second * ny;
        u_min = std::min(u_min, u);
        u_max = std::max(u_max, u);
        n_min = std::min(n_min, n);
        n_max = std::max(n_max, n);
      }

    aggregate.r_x0 = u_min * ux + n_min * nx;
    aggregate.r_y0 = u_min * uy + n_min * ny;
    aggregate.r_x1 = u_max * ux + n_min * nx;
    aggregate.r_y1 = u_max * uy + n_min * ny;
    aggregate.r_x2 = u_max * ux + n_max * nx;
    aggregate.r_y2 = u_max * uy + n_max * ny;
    aggregate.r_x3 = u_min * ux + n_max * nx;
    aggregate.r_y3 = u_min * uy + n_max * ny;

    aggregate.x0 = std::min({aggregate.r_x0, aggregate.r_x1,
                             aggregate.r_x2, aggregate.r_x3});
    aggregate.y0 = std::min({aggregate.r_y0, aggregate.r_y1,
                             aggregate.r_y2, aggregate.r_y3});
    aggregate.x1 = std::max({aggregate.r_x0, aggregate.r_x1,
                             aggregate.r_x2, aggregate.r_x3});
    aggregate.y1 = std::max({aggregate.r_y0, aggregate.r_y1,
                             aggregate.r_y2, aggregate.r_y3});
    aggregate.text_is_suppressed_glyph =
      aggregate.text_is_suppressed_glyph and next.text_is_suppressed_glyph;
  }

  inline typename page_item_sanitator<PAGE_CELLS>::contraction_result
  page_item_sanitator<PAGE_CELLS>::contract(page_item<PAGE_CELLS>& cells)
  {
    contraction_result result;
    for(auto& run : collect_line_runs(cells))
      {
        // Explicit PDF spaces are authoritative normal boundaries. On those
        // lines only a jump larger than a complete neighboring advance may
        // add another boundary; this still separates absolutely positioned
        // table fields without splitting a word merely because its glyph ink
        // or placement has an irregular space-sized gap.
        const double word_gap = run.has_semantic_spaces
          ? 1.0 : infer_word_gap(run);
        std::vector<page_item<PAGE_CELL>> words;
        page_item<PAGE_CELL>* previous_visible = nullptr;
        bool explicit_boundary = false;

        for(auto& cell : run.cells)
          {
            if(is_semantic_space(cell))
              {
                explicit_boundary = true;
                continue;
              }

            bool starts_word = words.empty() or explicit_boundary;
            if(not starts_word and previous_visible != nullptr)
              {
                if(not word_baselines_compatible(*previous_visible, cell))
                  {
                    starts_word = true;
                  }
                else
                  {
                    const double scale =
                      0.5 * (cell_advance_length(*previous_visible) +
                             cell_advance_length(cell));
                    const double normalized_gap =
                      std::max(0.0, forward_gap(*previous_visible, cell)) /
                      std::max(1.e-6, scale);
                    starts_word = normalized_gap > word_gap;
                  }
              }

            page_item<PAGE_CELL> aggregate_cell = cell;

            if(starts_word)
              {
                words.push_back(std::move(aggregate_cell));
              }
            else
              {
                append_cell(words.back(), aggregate_cell, false);
              }
            previous_visible = &cell;
            explicit_boundary = false;
          }

        if(words.empty())
          {
            continue;
          }

        page_item<PAGE_CELL> line = words.front();
        if(not words.front().text.empty())
          {
            result.words.push_back(words.front());
          }
        for(std::size_t i = 1; i < words.size(); ++i)
          {
            if(not words[i].text.empty())
              {
                result.words.push_back(words[i]);
              }
            const bool insert_space =
              not line.text.empty() and not words[i].text.empty();
            append_cell(line, words[i], insert_space);
          }
        if(not line.text.empty())
          {
            result.lines.push_back(line);
          }
      }
    return result;
  }

  inline page_item<PAGE_CELLS>
  page_item_sanitator<PAGE_CELLS>::create_word_cells(
      page_item<PAGE_CELLS>& cells,
      const decode_config& config)
  {
    (void)config;
    return contract(cells).words;
  }

  inline page_item<PAGE_CELLS>
  page_item_sanitator<PAGE_CELLS>::create_line_cells(
      page_item<PAGE_CELLS>& cells,
      const decode_config& config)
  {
    (void)config;
    return contract(cells).lines;
  }

  inline void page_item_sanitator<PAGE_CELLS>::sanitize_bbox(
      page_item<PAGE_CELLS>& cells,
      double horizontal_cell_tolerance,
      bool enforce_same_font,
      double space_width_factor_for_merge,
      double space_width_factor_for_merge_with_space,
      bool block_spaces)
  {
    (void)horizontal_cell_tolerance;
    (void)enforce_same_font;
    (void)space_width_factor_for_merge;
    (void)space_width_factor_for_merge_with_space;
    contraction_result contracted = contract(cells);
    cells = block_spaces ? contracted.words : contracted.lines;
  }

  inline nlohmann::json page_item_sanitator<PAGE_CELLS>::to_records(
      page_item<PAGE_CELLS>& cells)
  {
    nlohmann::json result = nlohmann::json::array({});
    int order = 0;
    for(auto& cell : cells)
      {
        if(not cell.active) { continue; }
        nlohmann::json item;
        item["index"] = order++;
        item["rect"] = {{"r_x0", cell.r_x0}, {"r_y0", cell.r_y0},
                        {"r_x1", cell.r_x1}, {"r_y1", cell.r_y1},
                        {"r_x2", cell.r_x2}, {"r_y2", cell.r_y2},
                        {"r_x3", cell.r_x3}, {"r_y3", cell.r_y3}};
        item["text"] = cell.text;
        item["orig"] = cell.text;
        item["font_key"] = cell.font_key;
        item["font_name"] = cell.font_name;
        item["rendering_mode"] = cell.rendering_mode;
        item["widget"] = cell.widget;
        item["left_to_right"] = cell.left_to_right;
        result.push_back(std::move(item));
      }
    return result;
  }

  inline void page_item_sanitator<PAGE_CELLS>::remove_adjacent_cells(
      page_item<PAGE_CELLS>& cells,
      double eps)
  {
    for(std::size_t i = 1; i < cells.size(); ++i)
      {
        auto& lhs = cells[i - 1];
        auto& rhs = cells[i];
        if(lhs.active and rhs.active and lhs.font_name == rhs.font_name and
           lhs.text == rhs.text and
           utils::values::distance(lhs.r_x0, lhs.r_y0, rhs.r_x0, rhs.r_y0) < eps and
           utils::values::distance(lhs.r_x1, lhs.r_y1, rhs.r_x1, rhs.r_y1) < eps and
           utils::values::distance(lhs.r_x2, lhs.r_y2, rhs.r_x2, rhs.r_y2) < eps and
           utils::values::distance(lhs.r_x3, lhs.r_y3, rhs.r_x3, rhs.r_y3) < eps)
          {
            rhs.active = false;
          }
      }
    cells.remove_inactive_cells();
  }

  inline void page_item_sanitator<PAGE_CELLS>::remove_duplicate_cells(
      page_item<PAGE_CELLS>& cells,
      double eps,
      bool same_line)
  {
    // Retained for compatibility with the decode cleanup path. Word/line
    // construction itself does not call this quadratic routine.
    for(std::size_t i = 0; i < cells.size(); ++i)
      {
        if(not cells[i].active) { continue; }
        for(std::size_t j = i + 1; j < cells.size(); ++j)
          {
            if(same_line and std::abs(cells[i].r_y0 - cells[j].r_y0) > eps)
              {
                break;
              }
            if(not cells[j].active) { continue; }
            if(cells[i].font_name == cells[j].font_name and
               cells[i].text == cells[j].text and
               utils::values::distance(cells[i].r_x0, cells[i].r_y0, cells[j].r_x0, cells[j].r_y0) < eps and
               utils::values::distance(cells[i].r_x1, cells[i].r_y1, cells[j].r_x1, cells[j].r_y1) < eps and
               utils::values::distance(cells[i].r_x2, cells[i].r_y2, cells[j].r_x2, cells[j].r_y2) < eps and
               utils::values::distance(cells[i].r_x3, cells[i].r_y3, cells[j].r_x3, cells[j].r_y3) < eps)
              {
                cells[j].active = false;
              }
          }
      }
    cells.remove_inactive_cells();
  }

  inline void page_item_sanitator<PAGE_CELLS>::sanitize_text(
      page_item<PAGE_CELLS>& cells)
  {
    for(auto& cell : cells)
      {
        if(cell.text.empty()) { continue; }
        for(const auto& pair : text_constants::replacements)
          {
            utils::string::replace(cell.text, pair.first, pair.second);
          }
      }

    static const std::regex pattern(R"(^\/([A-Za-z])_([A-Za-z])(_([A-Za-z]))?$)");
    for(auto& cell : cells)
      {
        if(cell.text.empty() or cell.text[0] != '/') { continue; }
        std::smatch match;
        if(std::regex_match(cell.text, match, pattern))
          {
            cell.text = match[1].str() + match[2].str();
            if(match[3].matched) { cell.text += match[4].str(); }
          }
      }
  }

}

#endif
