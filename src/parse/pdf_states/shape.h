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

    pdf_state(const decode_page_config& config,
              const pdf_state<GRPH>& grph_state_,
              std::array<double, 9>&    trafo_matrix_,
              page_item<PAGE_SHAPES>& page_shapes_);

    pdf_state(const pdf_state<SHAPE>& other);

    ~pdf_state();

    pdf_state<SHAPE>& operator=(const pdf_state<SHAPE>& other);

    void m(std::vector<qpdf_instruction>& instructions);
    void l(std::vector<qpdf_instruction>& instructions);

    void c(std::vector<qpdf_instruction>& instructions);
    void v(std::vector<qpdf_instruction>& instructions);
    void y(std::vector<qpdf_instruction>& instructions);

    void h(std::vector<qpdf_instruction>& instructions);
    void re(std::vector<qpdf_instruction>& instructions);

    void s(std::vector<qpdf_instruction>& instructions);
    void S(std::vector<qpdf_instruction>& instructions);

    void f(std::vector<qpdf_instruction>& instructions);
    void fStar(std::vector<qpdf_instruction>& instructions);

    void F(std::vector<qpdf_instruction>& instructions);

    void B(std::vector<qpdf_instruction>& instructions);
    void BStar(std::vector<qpdf_instruction>& instructions);

    void b(std::vector<qpdf_instruction>& instructions);
    void bStar(std::vector<qpdf_instruction>& instructions);

    void W(std::vector<qpdf_instruction>& instructions);
    void WStar(std::vector<qpdf_instruction>& instructions);

    void n(std::vector<qpdf_instruction>& instructions);

  private:

    bool verify(std::vector<qpdf_instruction>& instructions,
                std::size_t num_instr, std::string name);

    bool keep_shape(page_item<PAGE_SHAPE>& shape);

    void close_last_path();

    void register_paths();

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

    const decode_page_config& config;
    const pdf_state<GRPH>& grph_state;

    std::array<double, 9>&    trafo_matrix;

    page_item<PAGE_SHAPES>& page_shapes;

    page_item<PAGE_SHAPES>  curr_shapes;
    page_item<PAGE_SHAPES>  clippings;

    clipping_path_mode_type clipping_path_mode;
  };

  pdf_state<SHAPE>::pdf_state(const decode_page_config& config_,
                              const pdf_state<GRPH>& grph_state_,
                              std::array<double, 9>&    trafo_matrix_,
                              page_item<PAGE_SHAPES>& page_shapes_):
    config(config_),
    grph_state(grph_state_),

    trafo_matrix(trafo_matrix_),

    page_shapes(page_shapes_),

    curr_shapes(),
    clippings(),

    clipping_path_mode(NO_CLIPPING_PATH_RULE)
  {
    //LOG_S(INFO) << "pdf_state<SHAPE>";
  }

  pdf_state<SHAPE>::pdf_state(const pdf_state<SHAPE>& other):
    config(other.config),
    grph_state(other.grph_state),

    trafo_matrix(other.trafo_matrix),

    page_shapes(other.page_shapes)
  {
    *this = other;
  }

  pdf_state<SHAPE>::~pdf_state()
  {
    if(curr_shapes.size()>0 and curr_shapes[0].size()>0)
      {
        //LOG_S(ERROR) << "~pdf_state<SHAPE>: " << curr_shapes.size();

        for(int i=0; i<curr_shapes.size(); i++)
          {
            curr_shapes[i].transform(trafo_matrix);

            /*
              LOG_S(INFO) << "shape-" << i << " --> len: " << curr_shapes[i].size();
              for(int j=0; j<curr_shapes[i].size(); j++)
              {
              LOG_S(INFO) << "\t("
              << curr_shapes[i][j].first << ", "
              << curr_shapes[i][j].second << ")";
              }
            */
          }
      }
  }

  pdf_state<SHAPE>& pdf_state<SHAPE>::operator=(const pdf_state<SHAPE>& other)
  {
    this->curr_shapes = other.curr_shapes;
    this->clippings  = other.clippings;

    return *this;
  }

  void pdf_state<SHAPE>::m(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    if(not verify(instructions, 2, __FUNCTION__) ) { return; }

    double x = instructions[0].to_double();
    double y = instructions[1].to_double();

    this->m(x,y);
  }

  void pdf_state<SHAPE>::l(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    if(not verify(instructions, 2, __FUNCTION__) ) { return; }

    double x = instructions[0].to_double();
    double y = instructions[1].to_double();

    this->l(x,y);
  }

  void pdf_state<SHAPE>::c(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    if(not verify(instructions, 6, __FUNCTION__) ) { return; }

    /*
      if(curr_shapes.size()==0)
      {
      LOG_S(ERROR) << "applying 'c' on empty shapes";
      return;
      }
    */

    auto& shape = curr_shapes.back();

    /*
      if(shape.size()==0)
      {
      LOG_S(ERROR) << "applying 'c' on empty shape";
      return;
      }
    */

    std::pair<double, double> coor = shape.back();

    double x0 = coor.first;
    double y0 = coor.second;

    double x1 = instructions[0].to_double();
    double y1 = instructions[1].to_double();

    double x2 = instructions[2].to_double();
    double y2 = instructions[3].to_double();

    double x3 = instructions[4].to_double();
    double y3 = instructions[5].to_double();

    this->interpolate(shape, x0,y0, x1,y1, x2,y2, x3, y3, 8);
  }

  void pdf_state<SHAPE>::v(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    //assert(instructions.size()==4);
    if(not verify(instructions, 4, __FUNCTION__) ) { return; }

    auto& shape = curr_shapes.back();
    std::pair<double, double> coor = shape.back();

    double x0 = coor.first;
    double y0 = coor.second;

    double x1 = x0;
    double y1 = y0;

    double x2 = instructions[0].to_double();
    double y2 = instructions[1].to_double();

    double x3 = instructions[2].to_double();
    double y3 = instructions[3].to_double();

    this->interpolate(shape, x0,y0, x1,y1, x2,y2, x3, y3, 8);
  }

  void pdf_state<SHAPE>::y(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    //assert(instructions.size()==4);
    if(not verify(instructions, 4, __FUNCTION__) ) { return; }

    auto& shape = curr_shapes.back();
    std::pair<double, double> coor = shape.back();

    double x0 = coor.first;
    double y0 = coor.second;

    double x1 = instructions[0].to_double();
    double y1 = instructions[1].to_double();

    double x3 = instructions[2].to_double();
    double y3 = instructions[3].to_double();

    double x2 = x3;
    double y2 = y3;

    this->interpolate(shape, x0,y0, x1,y1, x2,y2, x3, y3, 8);
  }

  void pdf_state<SHAPE>::h(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    if(not verify(instructions, 0, __FUNCTION__) ) { return; }

    this->h();
  }

  void pdf_state<SHAPE>::re(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    if(not verify(instructions, 4, __FUNCTION__) ) { return; }

    double x = instructions[0].to_double();
    double y = instructions[1].to_double();

    double w = instructions[2].to_double();
    double h = instructions[3].to_double();

    this->re(x,y, w,h);
  }

  void pdf_state<SHAPE>::s(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    close_last_path();

    register_paths();
  }

  void pdf_state<SHAPE>::S(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    register_paths();
  }

  void pdf_state<SHAPE>::f(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    close_last_path();

    register_paths();
  }

  void pdf_state<SHAPE>::F(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    this->f(instructions);
  }

  void pdf_state<SHAPE>::fStar(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    close_last_path();

    register_paths();
  }

  void pdf_state<SHAPE>::B(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    close_last_path();

    register_paths();
  }

  void pdf_state<SHAPE>::BStar(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    close_last_path();

    register_paths();
  }

  void pdf_state<SHAPE>::b(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    close_last_path();

    register_paths();
  }

  void pdf_state<SHAPE>::bStar(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    close_last_path();

    register_paths();
  }

  void pdf_state<SHAPE>::W(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    clipping_path_mode = NONZERO_WINDING_NUMBER_RULE;
  }

  void pdf_state<SHAPE>::WStar(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    clipping_path_mode = EVEN_ODD_RULE;
  }

  void pdf_state<SHAPE>::n(std::vector<qpdf_instruction>& instructions)
  {
    if(not config.keep_shapes) { return; }

    clippings.clear();

    for(int l=0; l<curr_shapes.size(); l++)
      {
        auto& shape = curr_shapes[l];

        if(shape.size()>0)
          {
            clippings.push_back(shape);
          }
        else
          {
            LOG_S(WARNING) << "ignoring a shape of size 0";
          }
      }

    curr_shapes.clear();

    page_item<PAGE_SHAPE> shape;
    curr_shapes.push_back(shape);
  }

  /**************************************
   ***
   ***   Private methods
   ***
   *************************************/

  bool pdf_state<SHAPE>::verify(std::vector<qpdf_instruction>& instructions,
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
        auto front = shape.back();
        shape.append(front.first, front.second);
      }
    else
      {
        LOG_S(WARNING) << "can not close empty shape";
      }
  }

  void pdf_state<SHAPE>::register_paths()
  {
    //LOG_S(INFO) << "--------------------------------------------------";
    //LOG_S(INFO) << __FUNCTION__ << "\t #-paths: " << curr_shapes.size();

    for(int i=0; i<clippings.size(); i++)
      {
        clippings[i].transform(trafo_matrix);

        /*
          LOG_S(INFO) << "clippings: " << clippings.size();
          for(int j=0; j<clippings[i].size(); j++)
          {
          LOG_S(INFO) << "\t("
          << clippings[i][j].first << ", "
          << clippings[i][j].second << ")";
          }
        */
      }

    for(int i=0; i<curr_shapes.size(); i++)
      {
        curr_shapes[i].transform(trafo_matrix);

        /*
          LOG_S(INFO) << "shape-" << i << " --> len: " << curr_shapes[i].size();
          for(int j=0; j<curr_shapes[i].size(); j++)
          {
          LOG_S(INFO) << "\t("
          << curr_shapes[i][j].first << ", "
          << curr_shapes[i][j].second << ")";
          }
        */

        if(keep_shape(curr_shapes[i]))
          {
            //LOG_S(INFO) << " --> keeping shape";
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
          }
        else
          {
            //LOG_S(WARNING) << " --> ignoring shape";
          }
      }
    //LOG_S(INFO) << "--------------------------------------------------";

    curr_shapes.clear();
  }

  void pdf_state<SHAPE>::m(double x, double y)
  {
    if(not config.keep_shapes) { return; }

    page_item<PAGE_SHAPE> shape;
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

    shape.append(x, y);
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
