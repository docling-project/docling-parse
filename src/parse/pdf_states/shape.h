//-*-C++-*-

#ifndef PDF_SHAPE_STATE_H
#define PDF_SHAPE_STATE_H

namespace pdflib
{

  enum clipping_path_mode_type {
    NO_CLIPPING_PATH_RULE,
    NONZERO_WINDING_NUMBER_RULE,
    EVEN_ODD_RULE
  };

  template<>
  class pdf_state<SHAPE>
  {
    /*
      PDF doesn’t have a “shape object” in the stream. It has a current path
      that you keep appending segments to. Then a painting operator paints it.

      Common path-building operators (not exhaustive):

      - `m`: move-to (starts a new subpath)
      - `l`: line-to
      - `c`: cubic Bézier curve-to
      - `v`: curve variants
      - `y`: curve variants
      - `re`: rectangle convenience (adds a closed subpath rectangle)
      - `h`: closepath

      Common painting operators:

      - `f`: fill (nonzero winding rule)
      - `F`: is a legacy alias for f
      - `f*`: fill (even-odd rule)
      - `S`: stroke
      - `s`: closepath + stroke
      - `B`: fill + stroke (nonzero)
      - `B*`: fill + stroke (even-odd)
      - `b`, `b*`:  closepath + fill+stroke
      - `n`: end path without painting (also clears the current path)
      - `W`, `W*`: set clipping path (then usually n)
     */
    
  public:

    pdf_state(const decode_config& config,
              const pdf_state<GRPH>& grph_state_,
              std::array<double, 9>& trafo_matrix_,
              page_item<PAGE_SHAPES>& page_shapes_,
	      pdf_render_instructions& instructions_);

    pdf_state(const pdf_state<SHAPE>& other);

    ~pdf_state();

    pdf_state<SHAPE>& operator=(const pdf_state<SHAPE>& other);

    void m(std::vector<qpdf_stream_instruction>& instructions);
    void l(std::vector<qpdf_stream_instruction>& instructions);

    void c(std::vector<qpdf_stream_instruction>& instructions);
    void v(std::vector<qpdf_stream_instruction>& instructions);
    void y(std::vector<qpdf_stream_instruction>& instructions);

    void h(std::vector<qpdf_stream_instruction>& instructions);
    void re(std::vector<qpdf_stream_instruction>& instructions);

    void s(std::vector<qpdf_stream_instruction>& instructions);
    void S(std::vector<qpdf_stream_instruction>& instructions);

    void f(std::vector<qpdf_stream_instruction>& instructions);
    void fStar(std::vector<qpdf_stream_instruction>& instructions);

    void F(std::vector<qpdf_stream_instruction>& instructions);

    void B(std::vector<qpdf_stream_instruction>& instructions);
    void BStar(std::vector<qpdf_stream_instruction>& instructions);

    void b(std::vector<qpdf_stream_instruction>& instructions);
    void bStar(std::vector<qpdf_stream_instruction>& instructions);

    void W(std::vector<qpdf_stream_instruction>& instructions);
    void WStar(std::vector<qpdf_stream_instruction>& instructions);

    void n(std::vector<qpdf_stream_instruction>& instructions);

    clip_state_instruction get_clip_state();

  private:

    bool verify(std::vector<qpdf_stream_instruction>& instructions,
                std::size_t num_instr, std::string name);

    bool keep_shape(page_item<PAGE_SHAPE>& shape);

    void close_last_path();

    void register_paths(shape_paint_mode paint_mode, shape_fill_rule fill_rule);

    // Consumes a pending W/W* clip by capturing the current path into
    // `clippings`. Called from n() (shapes still in user space) and from
    // register_paths() (shapes already transformed to page space) — the
    // flag prevents double application of the CTM on the `W f`/`W S` path.
    void capture_pending_clip(bool already_transformed);

    // Average CTM scale factor, used to map the user-space line width into
    // page space (the path coordinates are transformed; the width must be
    // too).
    double trafo_scale() const;

    void m(double x, double y);
    void l(double x, double y);

    void h();
    void re(double x, double y,
            double w, double h);

    void interpolate(page_item<PAGE_SHAPE>& shape,
                     double x0, double y0,
                     double x1, double y1,
                     double x2, double y2,
                     double x3, double y3,
                     int N=8);

  private:

    const decode_config& config;
    const pdf_state<GRPH>& grph_state;

    std::array<double, 9>& trafo_matrix;

    page_item<PAGE_SHAPES>& page_shapes;

    pdf_render_instructions& instructions;
    
    page_item<PAGE_SHAPES> curr_shapes;
    page_item<PAGE_SHAPES> clippings;

    clipping_path_mode_type clipping_path_mode;
    bool clipping_path_pending;
  };

  pdf_state<SHAPE>::pdf_state(const decode_config& config_,
                              const pdf_state<GRPH>& grph_state_,
                              std::array<double, 9>& trafo_matrix_,
                              page_item<PAGE_SHAPES>& page_shapes_,
			      pdf_render_instructions& instructions_):
    config(config_),
    grph_state(grph_state_),

    trafo_matrix(trafo_matrix_),

    page_shapes(page_shapes_),
    instructions(instructions_),
    
    curr_shapes(),
    clippings(),

    clipping_path_mode(NO_CLIPPING_PATH_RULE),
    clipping_path_pending(false)
  {
    //LOG_S(INFO) << "pdf_state<SHAPE>";
  }

  pdf_state<SHAPE>::pdf_state(const pdf_state<SHAPE>& other):
    config(other.config),
    grph_state(other.grph_state),

    trafo_matrix(other.trafo_matrix),

    page_shapes(other.page_shapes),
    instructions(other.instructions)
  {
    *this = other;
  }

  pdf_state<SHAPE>::~pdf_state()
  {
    // a path that was built but never painted (no S/f/B/.../n) is dropped
    if(curr_shapes.size()>0 and curr_shapes[0].size()>0)
      {
        LOG_S(WARNING) << "~pdf_state<SHAPE>: dropping " << curr_shapes.size()
                       << " unpainted path(s)";
      }
  }

  pdf_state<SHAPE>& pdf_state<SHAPE>::operator=(const pdf_state<SHAPE>& other)
  {
    this->curr_shapes = other.curr_shapes;
    this->clippings  = other.clippings;
    this->clipping_path_mode = other.clipping_path_mode;
    this->clipping_path_pending = other.clipping_path_pending;

    return *this;
  }

  void pdf_state<SHAPE>::m(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    if(not verify(instructions, 2, __FUNCTION__) ) { return; }

    double x = instructions[0].to_double();
    double y = instructions[1].to_double();

    this->m(x,y);
  }

  void pdf_state<SHAPE>::l(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    if(not verify(instructions, 2, __FUNCTION__) ) { return; }

    double x = instructions[0].to_double();
    double y = instructions[1].to_double();

    this->l(x,y);
  }

  void pdf_state<SHAPE>::c(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    if(not verify(instructions, 6, __FUNCTION__) ) { return; }

    auto& shape = curr_shapes.back();
    shape.set_shape_type(BEZIER);

    std::pair<double, double> coor = shape.back();

    double x0 = coor.first;
    double y0 = coor.second;

    double x1 = instructions[0].to_double();
    double y1 = instructions[1].to_double();

    double x2 = instructions[2].to_double();
    double y2 = instructions[3].to_double();

    double x3 = instructions[4].to_double();
    double y3 = instructions[5].to_double();

    // exact curve for rendering; flattened samples for the JSON polyline
    shape.append_cubic_segment(x1,y1, x2,y2, x3,y3);

    this->interpolate(shape, x0,y0, x1,y1, x2,y2, x3, y3, 8);
  }

  void pdf_state<SHAPE>::v(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    if(not verify(instructions, 4, __FUNCTION__) ) { return; }

    auto& shape = curr_shapes.back();
    shape.set_shape_type(BEZIER);
    std::pair<double, double> coor = shape.back();

    double x0 = coor.first;
    double y0 = coor.second;

    double x1 = x0;
    double y1 = y0;

    double x2 = instructions[0].to_double();
    double y2 = instructions[1].to_double();

    double x3 = instructions[2].to_double();
    double y3 = instructions[3].to_double();

    // exact curve for rendering; flattened samples for the JSON polyline
    shape.append_cubic_segment(x1,y1, x2,y2, x3,y3);

    this->interpolate(shape, x0,y0, x1,y1, x2,y2, x3, y3, 8);
  }

  void pdf_state<SHAPE>::y(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    if(not verify(instructions, 4, __FUNCTION__) ) { return; }

    auto& shape = curr_shapes.back();
    shape.set_shape_type(BEZIER);
    std::pair<double, double> coor = shape.back();

    double x0 = coor.first;
    double y0 = coor.second;

    double x1 = instructions[0].to_double();
    double y1 = instructions[1].to_double();

    double x3 = instructions[2].to_double();
    double y3 = instructions[3].to_double();

    double x2 = x3;
    double y2 = y3;

    // exact curve for rendering; flattened samples for the JSON polyline
    shape.append_cubic_segment(x1,y1, x2,y2, x3,y3);

    this->interpolate(shape, x0,y0, x1,y1, x2,y2, x3, y3, 8);
  }

  void pdf_state<SHAPE>::h(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    if(not verify(instructions, 0, __FUNCTION__) ) { return; }

    this->h();
  }

  void pdf_state<SHAPE>::re(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    if(not verify(instructions, 4, __FUNCTION__) ) { return; }

    double x = instructions[0].to_double();
    double y = instructions[1].to_double();

    double w = instructions[2].to_double();
    double h = instructions[3].to_double();

    this->re(x,y, w,h);
  }

  // Path-painting operator table (PDF spec Table 60): each operator selects
  // paint mode, fill rule and whether the last subpath is closed first.
  // Only s, f/F/f*, b/b* close; S, B, B* paint the path as-is.

  void pdf_state<SHAPE>::s(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    close_last_path();

    register_paths(SHAPE_PAINT_STROKE, SHAPE_FILL_NONZERO);
  }

  void pdf_state<SHAPE>::S(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    register_paths(SHAPE_PAINT_STROKE, SHAPE_FILL_NONZERO);
  }

  void pdf_state<SHAPE>::f(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    close_last_path();

    register_paths(SHAPE_PAINT_FILL, SHAPE_FILL_NONZERO);
  }

  void pdf_state<SHAPE>::F(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    this->f(instructions);
  }

  void pdf_state<SHAPE>::fStar(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    close_last_path();

    register_paths(SHAPE_PAINT_FILL, SHAPE_FILL_EVEN_ODD);
  }

  void pdf_state<SHAPE>::B(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    register_paths(SHAPE_PAINT_FILL_STROKE, SHAPE_FILL_NONZERO);
  }

  void pdf_state<SHAPE>::BStar(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    register_paths(SHAPE_PAINT_FILL_STROKE, SHAPE_FILL_EVEN_ODD);
  }

  void pdf_state<SHAPE>::b(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    close_last_path();

    register_paths(SHAPE_PAINT_FILL_STROKE, SHAPE_FILL_NONZERO);
  }

  void pdf_state<SHAPE>::bStar(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    close_last_path();

    register_paths(SHAPE_PAINT_FILL_STROKE, SHAPE_FILL_EVEN_ODD);
  }

  void pdf_state<SHAPE>::W(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    clipping_path_mode = NONZERO_WINDING_NUMBER_RULE;
    clipping_path_pending = true;
  }

  void pdf_state<SHAPE>::WStar(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    clipping_path_mode = EVEN_ODD_RULE;
    clipping_path_pending = true;
  }

  clip_state_instruction pdf_state<SHAPE>::get_clip_state()
  {
    if(clipping_path_mode == NO_CLIPPING_PATH_RULE or clippings.size() == 0)
      {
        return clip_state_instruction();
      }

    clip_rule rule = CLIP_RULE_NONE;
    if(clipping_path_mode == NONZERO_WINDING_NUMBER_RULE)
      {
        rule = CLIP_RULE_NONZERO;
      }
    else if(clipping_path_mode == EVEN_ODD_RULE)
      {
        rule = CLIP_RULE_EVEN_ODD;
      }

    std::vector<clip_path_instruction> paths;
    for(int l = 0; l < clippings.size(); l++)
      {
        auto& shape = clippings[l];
        paths.emplace_back(shape.get_x(),
                           shape.get_y(),
                           shape.get_closing_type(),
                           shape.get_shape_type());
      }

    return clip_state_instruction(rule, std::move(paths));
  }

  void pdf_state<SHAPE>::n(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    capture_pending_clip(false);

    curr_shapes.clear();

    page_item<PAGE_SHAPE> shape;
    curr_shapes.push_back(shape);
  }

  void pdf_state<SHAPE>::capture_pending_clip(bool already_transformed)
  {
    if(not clipping_path_pending) { return; }

    // Per spec the new clip is the *intersection* of the old and new clip
    // regions; appending approximates that, since the renderer ANDs the
    // clip paths it applies.
    for(int l=0; l<curr_shapes.size(); l++)
      {
        auto shape = curr_shapes[l];

        if(not already_transformed)
          {
            shape.transform(trafo_matrix);
          }

        if(keep_shape(shape))
          {
            clippings.push_back(shape);
          }
        else if(shape.size() >= 2)
          {
            LOG_S(WARNING) << "ignoring a degenerate clip path";
          }
        // 0/1-point subpaths are the expected continuation left behind by
        // the `h` operator and are dropped silently
      }

    clipping_path_pending = false;
  }

  /**************************************
   ***
   ***   Private methods
   ***
   *************************************/

  bool pdf_state<SHAPE>::verify(std::vector<qpdf_stream_instruction>& instructions,
                                std::size_t num_instr, std::string name)
  {
    if(instructions.size()==num_instr)
      {
        return true;
      }

    if(instructions.size()>num_instr)
      {
        LOG_S(ERROR) << "#-instructions " << instructions.size()
                     << " exceeds expected value " << num_instr << " for "
                     << name;
        LOG_S(ERROR) << " => we can continue but might have incorrect results!";

        return true;
      }

    if(instructions.size()<num_instr) // fatal ...
      {
        std::stringstream ss;
        ss << "#-instructions " << instructions.size()
           << " does not match expected value " << num_instr
           << " for PDF operation: "
           << name;

        LOG_S(ERROR) << ss.str();
        throw std::logic_error(ss.str());
      }

    return false;
  }

  double pdf_state<SHAPE>::trafo_scale() const
  {
    // sqrt(|det|) of the 2x2 part of the row-major 3x3 CTM
    double det = trafo_matrix[0]*trafo_matrix[4] - trafo_matrix[1]*trafo_matrix[3];
    double scale = std::sqrt(std::abs(det));

    return (scale>0.0)? scale : 1.0;
  }

  bool pdf_state<SHAPE>::keep_shape(page_item<PAGE_SHAPE>& shape)
  {
    if(shape.size()<2)
      {
        return false;
      }

    double d=0;
    for(int l=0; l<shape.size()-1; l++)
      {
        auto p0 = shape[l+0];
        auto p1 = shape[l+1];

        double dx = p0.first-p1.first;
        double dy = p0.second-p1.second;

        d = std::max(d, dx*dx+dy*dy);
      }

    if(d<1.e-3)
      {
        return false;
      }

    return true;
  }

  void pdf_state<SHAPE>::close_last_path()
  {
    if(curr_shapes.size()==0)
      {
        LOG_S(WARNING) << "can not close non-existing shape";
        return;
      }

    auto& shape = curr_shapes.back();

    if(shape.size()>0)
      {
        // close back to the subpath start; the closing edge is expressed
        // through closing_type (the renderer emits path.close()), so no
        // line segment is recorded
        auto front = shape.front();
        auto back  = shape.back();

        if(front != back)
          {
            shape.append(front.first, front.second);
          }

        shape.set_closing_type(CLOSED);
      }
    else
      {
        LOG_S(WARNING) << "can not close empty shape";
      }
  }

  void pdf_state<SHAPE>::register_paths(shape_paint_mode paint_mode,
                                        shape_fill_rule fill_rule)
  {
    // NOTE: the clip paths in `clippings` are already in page space (they
    // are transformed once when captured); only the current path gets the
    // CTM applied here.

    std::vector<shape_subpath> subpaths;

    for(int i=0; i<curr_shapes.size(); i++)
      {
        curr_shapes[i].transform(trafo_matrix);

        if(keep_shape(curr_shapes[i]))
          {
            // the per-subpath page item (JSON output) keeps the flattened
            // polyline and the untransformed graphics-state values
            curr_shapes[i].set_graphics_state(
              grph_state.get_line_width(),
              grph_state.get_miter_limit(),
              grph_state.get_line_cap(),
              grph_state.get_line_join(),
              grph_state.get_dash_phase(),
              grph_state.get_dash_array(),
              grph_state.get_flatness(),
              grph_state.get_rgb_stroking_ops(),
              grph_state.get_rgb_filling_ops());

            page_shapes.push_back(curr_shapes[i]);

            auto& shape = curr_shapes[i];
            subpaths.emplace_back(shape.get_x().front(),
                                  shape.get_y().front(),
                                  shape.get_seg_ops(),
                                  shape.get_seg_x(),
                                  shape.get_seg_y(),
                                  shape.get_closing_type(),
                                  shape.get_shape_type());
          }
        else
          {
            LOG_S(WARNING) << "ignoring a degenerate shape";
          }
      }

    if(not subpaths.empty())
      {
        // line width and dash lengths are user-space quantities; the path
        // coordinates were just transformed, so these must scale with the
        // CTM too (a width of 0 means hairline and stays 0)
        double scale = trafo_scale();

        double line_width = grph_state.get_line_width();
        if(line_width > 0)
          {
            line_width *= scale;
          }

        std::vector<double> dash_array = grph_state.get_dash_array();
        for(auto& d : dash_array)
          {
            d *= scale;
          }
        double dash_phase = grph_state.get_dash_phase()*scale;

        LOG_S(INFO) << "shape instruction: #-subpaths: " << subpaths.size()
                    << ", paint-mode: " << static_cast<int>(paint_mode)
                    << ", fill-rule: " << static_cast<int>(fill_rule)
                    << ", line-width: " << line_width
                    << ", fill: ("
                    << grph_state.get_rgb_filling_ops()[0] << ", "
                    << grph_state.get_rgb_filling_ops()[1] << ", "
                    << grph_state.get_rgb_filling_ops()[2] << ")"
                    << " alpha: " << grph_state.get_fill_alpha()
                    << ", stroke: ("
                    << grph_state.get_rgb_stroking_ops()[0] << ", "
                    << grph_state.get_rgb_stroking_ops()[1] << ", "
                    << grph_state.get_rgb_stroking_ops()[2] << ")"
                    << " alpha: " << grph_state.get_stroke_alpha();

        shape_instruction shpinstr(std::move(subpaths),
                                   paint_mode,
                                   fill_rule,
                                   line_width,
                                   grph_state.get_line_cap(),
                                   grph_state.get_line_join(),
                                   grph_state.get_miter_limit(),
                                   std::move(dash_array),
                                   dash_phase,
                                   grph_state.get_rgb_stroking_ops(),
                                   grph_state.get_rgb_filling_ops(),
                                   grph_state.get_stroke_alpha(),
                                   grph_state.get_fill_alpha(),
                                   get_clip_state());
        instructions.add_shape_instruction(std::move(shpinstr));
      }

    // a pending W/W* clip takes effect after *any* painting operator (not
    // only after `n`); it must not affect the instruction emitted above,
    // hence the capture happens last. The shapes are already in page space.
    capture_pending_clip(true);

    curr_shapes.clear();
  }

  void pdf_state<SHAPE>::m(double x, double y)
  {
    if(not config.keep_shapes) { return; }

    page_item<PAGE_SHAPE> shape;
    shape.set_closing_type(OPEN);
    shape.set_shape_type(LINE);
    curr_shapes.push_back(shape);

    this->l(x,y);
  }

  void pdf_state<SHAPE>::l(double x, double y)
  {
    if(not config.keep_shapes) { return; }

    if(curr_shapes.size()==0)
      {
        LOG_S(WARNING) << "applying 'l' on empty shapes";
        return;
      }

    auto& shape = curr_shapes.back();

    // records both the flattened polyline point and the exact line segment
    // (the subpath's first point is stored as the move-to, without an op)
    shape.append_line_segment(x, y);
  }

  void pdf_state<SHAPE>::h()
  {
    if(not config.keep_shapes) { return; }

    if(curr_shapes.size()==0)
      {
        LOG_S(WARNING) << "applying 'h' on empty shapes";
        return;
      }

    // first close
    auto& shape = curr_shapes.back();

    if(shape.size()==0)
      {
        LOG_S(WARNING) << "applying 'h' on empty shape";
        return;
      }

    std::pair<double, double> coor = shape.front();

    shape.append(coor.first, coor.second);
    shape.set_closing_type(CLOSED);

    // add new shape segment
    page_item<PAGE_SHAPE> shape_;
    shape_.append(coor.first, coor.second);

    curr_shapes.push_back(shape_);
  }

  void pdf_state<SHAPE>::re(double x, double y,
                            double w, double h)
  {
    if(not config.keep_shapes) { return; }

    this->m(x, y);
    curr_shapes.back().set_shape_type(RECTANGLE);

    this->l(x+w, y);

    this->l(x+w, y+h);

    this->l(x, y+h);

    this->h();
  }

  void pdf_state<SHAPE>::interpolate(page_item<PAGE_SHAPE>& shape,
                                     double x0, double y0,
                                     double x1, double y1,
                                     double x2, double y2,
                                     double x3, double y3,
                                     int N)
  {
    for(int l=1; l<N; l++)
      {
        double t = l/double(N-1);

        double x = (1.-t)*(1.-t)*(1.-t)*x0 + 3.*t*(1.-t)*(1.-t)*x1 + 3.*t*t*(1.-t)*x2 + t*t*t*x3;
        double y = (1.-t)*(1.-t)*(1.-t)*y0 + 3.*t*(1.-t)*(1.-t)*y1 + 3.*t*t*(1.-t)*y2 + t*t*t*y3;

        shape.append(x, y);
      }
  }

}

#endif
