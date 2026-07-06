//-*-C++-*-

#ifndef PAGE_ITEM_RENDER_INSTRUCTION_H
#define PAGE_ITEM_RENDER_INSTRUCTION_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <utility>

#include <parse/enums.h>
#include <parse/page_items/embedded_font_blob.h>

namespace pdflib
{
  enum pixel_format {
    PIXEL_FORMAT_UNKNOWN,
    PIXEL_FORMAT_GRAY,   // 1 channel  (/DeviceGray)
    PIXEL_FORMAT_RGB,    // 3 channels (/DeviceRGB)
    PIXEL_FORMAT_CMYK,   // 4 channels (/DeviceCMYK)
  };

  enum cmyk_convention {
    CMYK_CONVENTION_UNKNOWN,
    CMYK_CONVENTION_ADOBE_INVERTED,
    CMYK_CONVENTION_PROCESS,
  };

  enum clip_rule {
    CLIP_RULE_NONE,
    CLIP_RULE_NONZERO,
    CLIP_RULE_EVEN_ODD,
  };

  class clip_path_instruction
  {
  public:
    clip_path_instruction();
    clip_path_instruction(std::vector<double> x,
                          std::vector<double> y,
                          page_shape_closing_type closing_type,
                          page_shape_type shape_type);

    const std::vector<double>& get_x() const { return x; }
    const std::vector<double>& get_y() const { return y; }
    page_shape_closing_type get_closing_type() const { return closing_type; }
    page_shape_type get_shape_type() const { return shape_type; }

    bool empty() const { return x.empty() or y.empty(); }
    size_t size() const { return std::min(x.size(), y.size()); }

    // Copy shifted by (dx, dy); used to lift widget appearance-stream
    // geometry from AP-local into page coordinates.
    clip_path_instruction translated(double dx, double dy) const
    {
      std::vector<double> x_(x), y_(y);
      for(auto& v : x_) { v += dx; }
      for(auto& v : y_) { v += dy; }
      return clip_path_instruction(std::move(x_), std::move(y_),
                                   closing_type, shape_type);
    }

  private:
    std::vector<double> x;
    std::vector<double> y;
    page_shape_closing_type closing_type;
    page_shape_type shape_type;
  };

  inline clip_path_instruction::clip_path_instruction():
    x(),
    y(),
    closing_type(CLOSING_UNDEFINED),
    shape_type(SHAPE_UNDEFINED)
  {}

  inline clip_path_instruction::clip_path_instruction(std::vector<double> x_,
                                                      std::vector<double> y_,
                                                      page_shape_closing_type closing_type_,
                                                      page_shape_type shape_type_):
    x(std::move(x_)),
    y(std::move(y_)),
    closing_type(closing_type_),
    shape_type(shape_type_)
  {}

  class clip_state_instruction
  {
  public:
    clip_state_instruction();
    clip_state_instruction(clip_rule rule,
                           std::vector<clip_path_instruction> paths);

    clip_rule get_rule() const { return rule; }
    const std::vector<clip_path_instruction>& get_paths() const { return paths; }

    bool has_clip() const
    {
      return rule != CLIP_RULE_NONE and not paths.empty();
    }

    clip_state_instruction translated(double dx, double dy) const
    {
      std::vector<clip_path_instruction> paths_;
      paths_.reserve(paths.size());
      for(const auto& path : paths)
        {
          paths_.push_back(path.translated(dx, dy));
        }
      return clip_state_instruction(rule, std::move(paths_));
    }

  private:
    clip_rule rule;
    std::vector<clip_path_instruction> paths;
  };

  inline clip_state_instruction::clip_state_instruction():
    rule(CLIP_RULE_NONE),
    paths()
  {}

  inline clip_state_instruction::clip_state_instruction(
    clip_rule rule_,
    std::vector<clip_path_instruction> paths_):
    rule(rule_),
    paths(std::move(paths_))
  {}

  enum RENDER_INSTRUCTION_NAME {
    SIZE_INSTRUCTION, // set the size of the canvas on which we render
    TEXT_RENDER_INSTRUCTION, // render text on the canvas
    TEXT_WIDGET_RENDER_INSTRUCTION, // render a fillable-field widget (bbox + value text)
    BITMAP_RENDER_INSTRUCTION, // paste bitmap image on the canvas
    SHAPE_RENDER_INSTRUCTION, // draw shapes (lines, shapes, filling, etc)
  };

  class instruction
  {
  public:

    instruction(const RENDER_INSTRUCTION_NAME name,
                const int32_t index):
      name(name), index(index) {}

    const RENDER_INSTRUCTION_NAME name; // instruction type
    const int32_t index; // index of instruction in its typed vector
  };

  class size_instruction
  {

  public:
    const static RENDER_INSTRUCTION_NAME instr = SIZE_INSTRUCTION;

    std::array<int, 4> media_bbox;
    std::array<int, 4> crop_bbox;
  };


  class text_instruction
  {
  public:
    const static RENDER_INSTRUCTION_NAME instr = TEXT_RENDER_INSTRUCTION;

    text_instruction(std::string text,
                     std::string font_enc,
                     std::string font_key,
                     std::string font_name,
                     std::string encoding_name,
                     std::string base_font,
                     double font_size,
                     double r_x0, double r_y0,
                     double r_x1, double r_y1,
                     double r_x2, double r_y2,
                     double r_x3, double r_y3,
                     double font_ascent_norm, double font_descent_norm,
		     double base_x0, double base_y0,
                     bool has_glyph_bbox = false,
                     double g_x0 = 0.0, double g_y0 = 0.0,
                     double g_x1 = 0.0, double g_y1 = 0.0):
      text(std::move(text)),
      font_enc(std::move(font_enc)),
      font_key(std::move(font_key)),
      font_name(std::move(font_name)),
      encoding_name(std::move(encoding_name)),
      base_font(std::move(base_font)),
      font_size(font_size),
      r_x0(r_x0), r_y0(r_y0),
      r_x1(r_x1), r_y1(r_y1),
      r_x2(r_x2), r_y2(r_y2),
      r_x3(r_x3), r_y3(r_y3),
      font_ascent_norm(font_ascent_norm),
      font_descent_norm(font_descent_norm),
      base_x0(base_x0), base_y0(base_y0),
      has_glyph_bbox_(has_glyph_bbox),
      g_x0_(g_x0), g_y0_(g_y0),
      g_x1_(g_x1), g_y1_(g_y1)
    {}

    const std::string& get_text() const { return text; }

    const std::string& get_font_enc() const { return font_enc; }
    const std::string& get_font_key() const { return font_key; }
    const std::string& get_font_name() const { return font_name; }
    const std::string& get_encoding_name() const { return encoding_name; }
    const std::string& get_base_font() const { return base_font; }
    double get_font_size() const { return font_size; }

    double get_r_x0() const { return r_x0; }
    double get_r_y0() const { return r_y0; }
    double get_r_x1() const { return r_x1; }
    double get_r_y1() const { return r_y1; }
    double get_r_x2() const { return r_x2; }
    double get_r_y2() const { return r_y2; }
    double get_r_x3() const { return r_x3; }
    double get_r_y3() const { return r_y3; }

    double get_font_ascent_norm()  const { return font_ascent_norm; }
    double get_font_descent_norm() const { return font_descent_norm; }

    double get_base_x0() const { return base_x0; }
    double get_base_y0() const { return base_y0; }
    bool has_glyph_bbox() const { return has_glyph_bbox_; }
    double get_g_x0() const { return g_x0_; }
    double get_g_y0() const { return g_y0_; }
    double get_g_x1() const { return g_x1_; }
    double get_g_y1() const { return g_y1_; }

    // Optional embedded font program shared by all text instructions of one
    // PDF font resource. Set after construction to keep the (already long)
    // constructor signature stable; may be null.
    void set_embedded_font(std::shared_ptr<const embedded_font_blob> blob);
    const std::shared_ptr<const embedded_font_blob>& get_embedded_font() const;
    bool has_embedded_font() const;

    // Optional decoded PDF character code (or CID for composite fonts) of a
    // single-character text cell; -1 when unknown or multi-character. Needed
    // later for glyph-identity rendering of symbolic/subset fonts.
    void set_char_code(int64_t char_code);
    int64_t get_char_code() const;

    // Optional glyph name assigned to the character code by the PDF's
    // /Encoding /Differences; empty when none. This is the authoritative
    // glyph identity inside the embedded font program.
    void set_glyph_name(std::string glyph_name);
    const std::string& get_glyph_name() const;

    // Fill color of the graphics state at emission time (defaults: opaque
    // black) and the Tr text rendering mode (3/7 paint no glyphs).
    void set_fill_color(const std::array<int, 3>& rgb, double alpha);
    const std::array<int, 3>& get_rgb_filling() const { return rgb_filling_; }
    double get_fill_alpha() const { return fill_alpha_; }

    void set_rendering_mode(int mode) { rendering_mode_ = mode; }
    int get_rendering_mode() const { return rendering_mode_; }
    bool is_invisible() const { return rendering_mode_ == 3 or rendering_mode_ == 7; }

    // Copy shifted by (dx, dy); used to lift widget appearance-stream text
    // from AP-local into page coordinates. The glyph bbox (g_*) lives in
    // font units and is copied unshifted.
    text_instruction translated(double dx, double dy) const
    {
      text_instruction copy(text, font_enc, font_key, font_name,
                            encoding_name, base_font, font_size,
                            r_x0 + dx, r_y0 + dy,
                            r_x1 + dx, r_y1 + dy,
                            r_x2 + dx, r_y2 + dy,
                            r_x3 + dx, r_y3 + dy,
                            font_ascent_norm, font_descent_norm,
                            base_x0 + dx, base_y0 + dy,
                            has_glyph_bbox_,
                            g_x0_, g_y0_, g_x1_, g_y1_);

      copy.embedded_font_  = embedded_font_;
      copy.char_code_      = char_code_;
      copy.glyph_name_     = glyph_name_;
      copy.rgb_filling_    = rgb_filling_;
      copy.fill_alpha_     = fill_alpha_;
      copy.rendering_mode_ = rendering_mode_;

      return copy;
    }

  private:

    const std::string text;

    const std::string font_enc;
    const std::string font_key;
    const std::string font_name;
    const std::string encoding_name;
    const std::string base_font;
    const double font_size;

    const double r_x0;
    const double r_y0;
    const double r_x1;
    const double r_y1;
    const double r_x2;
    const double r_y2;
    const double r_x3;
    const double r_y3;

    const double font_ascent_norm;
    const double font_descent_norm;

    const double base_x0;
    const double base_y0;

    const bool has_glyph_bbox_;
    const double g_x0_;
    const double g_y0_;
    const double g_x1_;
    const double g_y1_;

    std::shared_ptr<const embedded_font_blob> embedded_font_;
    int64_t char_code_ = -1;
    std::string glyph_name_;

    std::array<int, 3> rgb_filling_ = {0, 0, 0};
    double fill_alpha_ = 1.0;
    int rendering_mode_ = 0;
  };

  inline void text_instruction::set_embedded_font(std::shared_ptr<const embedded_font_blob> blob)
  {
    embedded_font_ = std::move(blob);
  }

  inline const std::shared_ptr<const embedded_font_blob>& text_instruction::get_embedded_font() const
  {
    return embedded_font_;
  }

  inline bool text_instruction::has_embedded_font() const
  {
    return embedded_font_ != nullptr and embedded_font_->has_bytes();
  }

  inline void text_instruction::set_char_code(int64_t char_code)
  {
    char_code_ = char_code;
  }

  inline int64_t text_instruction::get_char_code() const
  {
    return char_code_;
  }

  inline void text_instruction::set_glyph_name(std::string glyph_name)
  {
    glyph_name_ = std::move(glyph_name);
  }

  inline const std::string& text_instruction::get_glyph_name() const
  {
    return glyph_name_;
  }

  inline void text_instruction::set_fill_color(const std::array<int, 3>& rgb,
                                               double alpha)
  {
    rgb_filling_ = rgb;
    fill_alpha_ = alpha;
  }

  class text_widget_instruction
  {
  public:
    const static RENDER_INSTRUCTION_NAME instr = TEXT_WIDGET_RENDER_INSTRUCTION;

    text_widget_instruction(std::string text,
                            double x0,  double y0,
                            double x1,  double y1,
                            double r_x0, double r_y0,
                            double r_x1, double r_y1,
                            double r_x2, double r_y2,
                            double r_x3, double r_y3):
      text_(std::move(text)),
      x0_(x0),   y0_(y0),
      x1_(x1),   y1_(y1),
      r_x0_(r_x0), r_y0_(r_y0),
      r_x1_(r_x1), r_y1_(r_y1),
      r_x2_(r_x2), r_y2_(r_y2),
      r_x3_(r_x3), r_y3_(r_y3)
    {}

    const std::string& get_text() const { return text_; }

    double get_x0() const { return x0_; }
    double get_y0() const { return y0_; }
    double get_x1() const { return x1_; }
    double get_y1() const { return y1_; }

    double get_r_x0() const { return r_x0_; }
    double get_r_y0() const { return r_y0_; }
    double get_r_x1() const { return r_x1_; }
    double get_r_y1() const { return r_y1_; }
    double get_r_x2() const { return r_x2_; }
    double get_r_y2() const { return r_y2_; }
    double get_r_x3() const { return r_x3_; }
    double get_r_y3() const { return r_y3_; }

  private:
    const std::string text_;
    const double x0_,   y0_,   x1_,   y1_;
    const double r_x0_, r_y0_, r_x1_, r_y1_;
    const double r_x2_, r_y2_, r_x3_, r_y3_;
  };

  class bitmap_instruction
  {
  public:
    const static RENDER_INSTRUCTION_NAME instr = BITMAP_RENDER_INSTRUCTION;

    bitmap_instruction(std::string xobject_key,
		       std::shared_ptr<std::vector<uint8_t> > data,
                       std::shared_ptr<std::vector<uint8_t> > alpha_data,
                       cmyk_convention cmyk_conv,
                       std::array<int, 3> shape,
                       pixel_format fmt,
                       bool image_mask,
                       std::array<int, 3> rgb_filling,
                       double r_x0, double r_y0,
                       double r_x1, double r_y1,
                       double r_x2, double r_y2,
                       double r_x3, double r_y3,
                       clip_state_instruction clip_state = clip_state_instruction()):
      xobject_key(xobject_key),
      data(std::move(data)),
      alpha_data(std::move(alpha_data)),
      cmyk_conv(cmyk_conv),
      shape(shape),
      fmt(fmt),
      image_mask(image_mask),
      rgb_filling(rgb_filling),
      r_x0(r_x0), r_y0(r_y0),
      r_x1(r_x1), r_y1(r_y1),
      r_x2(r_x2), r_y2(r_y2),
      r_x3(r_x3), r_y3(r_y3),
      clip_state(std::move(clip_state)) {}

    const std::string& get_key() const { return xobject_key; }

    const std::shared_ptr<std::vector<uint8_t> >& get_data() const { return data; }
    const std::shared_ptr<std::vector<uint8_t> >& get_alpha_data() const { return alpha_data; }
    cmyk_convention get_cmyk_convention() const { return cmyk_conv; }
    const std::array<int, 3>& get_shape() const { return shape; }
    pixel_format get_pixel_format() const { return fmt; }
    bool is_image_mask() const { return image_mask; }
    const std::array<int, 3>& get_rgb_filling() const { return rgb_filling; }

    bool has_data() const { return (data) and (not data->empty()); }
    bool has_alpha_data() const { return (alpha_data) and (not alpha_data->empty()); }

    double get_r_x0() const { return r_x0; }
    double get_r_y0() const { return r_y0; }
    double get_r_x1() const { return r_x1; }
    double get_r_y1() const { return r_y1; }
    double get_r_x2() const { return r_x2; }
    double get_r_y2() const { return r_y2; }
    double get_r_x3() const { return r_x3; }
    double get_r_y3() const { return r_y3; }

    const clip_state_instruction& get_clip_state() const { return clip_state; }
    bool has_clip_state() const { return clip_state.has_clip(); }

  private:

    const std::string xobject_key;
    
    const std::shared_ptr<std::vector<uint8_t> > data;
    const std::shared_ptr<std::vector<uint8_t> > alpha_data;
    const cmyk_convention cmyk_conv;
    const std::array<int, 3> shape;
    const pixel_format fmt;
    const bool image_mask;
    const std::array<int, 3> rgb_filling;

    const double r_x0;
    const double r_y0;
    const double r_x1;
    const double r_y1;
    const double r_x2;
    const double r_y2;
    const double r_x3;
    const double r_y3;

    const clip_state_instruction clip_state;
  };

  // One subpath of a painted path: an implicit move-to (x0, y0) followed by
  // a run of segment ops. SEGMENT_LINE_TO consumes one coordinate pair from
  // px/py, SEGMENT_CUBIC_TO consumes three (ctrl1, ctrl2, end) — mirroring
  // BLPath's command/vertex-array model, so the renderer can rebuild true
  // curves instead of flattened polylines.
  class shape_subpath
  {
  public:
    shape_subpath();
    shape_subpath(double x0, double y0,
                  std::vector<shape_segment_op> ops,
                  std::vector<double> px,
                  std::vector<double> py,
                  page_shape_closing_type closing_type,
                  page_shape_type shape_type);

    double get_x0() const { return x0; }
    double get_y0() const { return y0; }

    const std::vector<shape_segment_op>& get_ops() const { return ops; }
    const std::vector<double>& get_px() const { return px; }
    const std::vector<double>& get_py() const { return py; }

    page_shape_closing_type get_closing_type() const { return closing_type; }
    page_shape_type         get_shape_type()   const { return shape_type; }

    bool empty() const { return ops.empty(); }

    shape_subpath translated(double dx, double dy) const
    {
      std::vector<double> px_(px), py_(py);
      for(auto& v : px_) { v += dx; }
      for(auto& v : py_) { v += dy; }
      return shape_subpath(x0 + dx, y0 + dy, ops,
                           std::move(px_), std::move(py_),
                           closing_type, shape_type);
    }

  private:

    double x0; // subpath start (move-to)
    double y0;

    std::vector<shape_segment_op> ops;
    std::vector<double> px; // op-consumed points,
    std::vector<double> py; // in op order

    page_shape_closing_type closing_type;
    page_shape_type         shape_type;
  };

  inline shape_subpath::shape_subpath():
    x0(0.0),
    y0(0.0),
    ops(),
    px(),
    py(),
    closing_type(CLOSING_UNDEFINED),
    shape_type(SHAPE_UNDEFINED)
  {}

  inline shape_subpath::shape_subpath(double x0_, double y0_,
                                      std::vector<shape_segment_op> ops_,
                                      std::vector<double> px_,
                                      std::vector<double> py_,
                                      page_shape_closing_type closing_type_,
                                      page_shape_type shape_type_):
    x0(x0_),
    y0(y0_),
    ops(std::move(ops_)),
    px(std::move(px_)),
    py(std::move(py_)),
    closing_type(closing_type_),
    shape_type(shape_type_)
  {}

  // One path-painting operator (S, s, f, F, f*, B, B*, b, b*): all subpaths
  // of the painted path plus the graphics-state parameters needed to color
  // it. Fill rules operate on the whole path, so the subpaths must stay in
  // one instruction (a rectangle-with-hole is two subpaths of one fill).
  class shape_instruction
  {
  public:
    const static RENDER_INSTRUCTION_NAME instr = SHAPE_RENDER_INSTRUCTION;

    shape_instruction(std::vector<shape_subpath> subpaths,
                      shape_paint_mode        paint_mode,
                      shape_fill_rule         fill_rule,
                      double                  line_width,
                      int                     line_cap,
                      int                     line_join,
                      double                  miter_limit,
                      std::vector<double>     dash_array,
                      double                  dash_phase,
                      std::array<int, 3>      rgb_stroking,
                      std::array<int, 3>      rgb_filling,
                      double                  stroke_alpha,
                      double                  fill_alpha,
                      clip_state_instruction  clip_state = clip_state_instruction()):
      subpaths(std::move(subpaths)),
      paint_mode(paint_mode),
      fill_rule(fill_rule),
      line_width(line_width),
      line_cap(line_cap),
      line_join(line_join),
      miter_limit(miter_limit),
      dash_array(std::move(dash_array)),
      dash_phase(dash_phase),
      rgb_stroking(rgb_stroking),
      rgb_filling(rgb_filling),
      stroke_alpha(stroke_alpha),
      fill_alpha(fill_alpha),
      clip_state(std::move(clip_state)) {}

    const std::vector<shape_subpath>& get_subpaths() const { return subpaths; }

    // total number of points (subpath starts + op-consumed points)
    size_t size() const
    {
      size_t n = 0;
      for(const auto& sp : subpaths)
        {
          n += 1 + sp.get_px().size();
        }
      return n;
    }

    shape_paint_mode            get_paint_mode()    const { return paint_mode; }
    shape_fill_rule             get_fill_rule()     const { return fill_rule; }
    double                      get_line_width()    const { return line_width; }
    int                         get_line_cap()      const { return line_cap; }
    int                         get_line_join()     const { return line_join; }
    double                      get_miter_limit()   const { return miter_limit; }
    const std::vector<double>&  get_dash_array()    const { return dash_array; }
    double                      get_dash_phase()    const { return dash_phase; }
    const std::array<int, 3>&   get_rgb_stroking()  const { return rgb_stroking; }
    const std::array<int, 3>&   get_rgb_filling()   const { return rgb_filling; }

    // ExtGState constant alpha (/CA, /ca); 1.0 = opaque, 0.0 = invisible
    double get_stroke_alpha() const { return stroke_alpha; }
    double get_fill_alpha()   const { return fill_alpha; }

    const clip_state_instruction& get_clip_state() const { return clip_state; }
    bool has_clip_state() const { return clip_state.has_clip(); }

    shape_instruction translated(double dx, double dy) const
    {
      std::vector<shape_subpath> subpaths_;
      subpaths_.reserve(subpaths.size());
      for(const auto& subpath : subpaths)
        {
          subpaths_.push_back(subpath.translated(dx, dy));
        }
      return shape_instruction(std::move(subpaths_), paint_mode, fill_rule,
                               line_width, line_cap, line_join, miter_limit,
                               dash_array, dash_phase,
                               rgb_stroking, rgb_filling,
                               stroke_alpha, fill_alpha,
                               clip_state.translated(dx, dy));
    }

  private:

    const std::vector<shape_subpath> subpaths;

    const shape_paint_mode        paint_mode;
    const shape_fill_rule         fill_rule;
    const double                  line_width;
    const int                     line_cap;
    const int                     line_join;
    const double                  miter_limit;
    const std::vector<double>     dash_array;
    const double                  dash_phase;
    const std::array<int, 3>      rgb_stroking;
    const std::array<int, 3>      rgb_filling;

    const double                  stroke_alpha;
    const double                  fill_alpha;

    const clip_state_instruction  clip_state;
  };

  class pdf_render_instructions
  {
    typedef instruction instruction_type;

    typedef text_instruction text_instruction_type;
    typedef text_widget_instruction text_widget_instruction_type;
    typedef bitmap_instruction bitmap_instruction_type;
    typedef shape_instruction shape_instruction_type;

  public:

    // add instruction methods

    void set_size_instruction(std::array<double, 4> media_bbox,
                              std::array<double, 4> crop_bbox);

    void add_size_instruction(size_instruction& instr);
    void add_text_instruction(text_instruction_type instr);
    void add_widget_instruction(text_widget_instruction_type instr);
    void add_bitmap_instruction(bitmap_instruction_type instr);
    void add_shape_instruction(shape_instruction_type instr);

    // Read access for re-emitting instructions of a sub-decode (widget
    // appearance streams) into the main instruction list.
    const std::vector<shape_instruction_type>& get_shape_instructions() const
    {
      return shape_instructions;
    }

    const std::vector<text_instruction_type>& get_text_instructions() const
    {
      return text_instructions;
    }

    // render method
    template<typename renderer_type>
    void iterate_over_instructions(renderer_type& renderer);

  private:

    size_instruction size_instr;

    std::vector<instruction_type> instructions;

    std::vector<text_instruction_type>        text_instructions;
    std::vector<text_widget_instruction_type> widget_instructions;
    std::vector<bitmap_instruction_type>      bitmap_instructions;
    std::vector<shape_instruction_type>       shape_instructions;

  };

  inline void pdf_render_instructions::set_size_instruction(std::array<double, 4> media_bbox,
                                                            std::array<double, 4> crop_bbox)
  {
    for(int i = 0; i < 4; i++)
      {
        size_instr.media_bbox[i] = static_cast<int>(media_bbox[i]);
        size_instr.crop_bbox[i] = static_cast<int>(crop_bbox[i]);
      }
  }

  inline void pdf_render_instructions::add_size_instruction(size_instruction& instr)
  {
    size_instr = instr;
  }

  inline void pdf_render_instructions::add_text_instruction(text_instruction instr)
  {
    instructions.emplace_back(TEXT_RENDER_INSTRUCTION, text_instructions.size());
    text_instructions.push_back(std::move(instr));
  }

  inline void pdf_render_instructions::add_widget_instruction(text_widget_instruction instr)
  {
    instructions.emplace_back(TEXT_WIDGET_RENDER_INSTRUCTION, widget_instructions.size());
    widget_instructions.push_back(std::move(instr));
  }

  inline void pdf_render_instructions::add_bitmap_instruction(bitmap_instruction instr)
  {
    instructions.emplace_back(BITMAP_RENDER_INSTRUCTION, bitmap_instructions.size());
    bitmap_instructions.push_back(std::move(instr));
  }

  inline void pdf_render_instructions::add_shape_instruction(shape_instruction instr)
  {
    instructions.emplace_back(SHAPE_RENDER_INSTRUCTION, shape_instructions.size());
    shape_instructions.push_back(std::move(instr));
  }

  template<typename renderer_type>
  void pdf_render_instructions::iterate_over_instructions(renderer_type& renderer)
  {
    renderer.set_size(size_instr);

    for(const auto& instr : instructions)
      {
	switch(instr.name)
	  {
	  case TEXT_RENDER_INSTRUCTION:
	    {
	      auto& text_instr = text_instructions.at(instr.index);
	      renderer.render_text(text_instr);
	    }
	    break;

	  case TEXT_WIDGET_RENDER_INSTRUCTION:
	    {
	      auto& widget_instr = widget_instructions.at(instr.index);
	      renderer.render_widget(widget_instr);
	    }
	    break;

	  case BITMAP_RENDER_INSTRUCTION:
	    {
	      auto& bmap_instr = bitmap_instructions.at(instr.index);
	      renderer.render_bitmap(bmap_instr);
	    }
	    break;

	  case SHAPE_RENDER_INSTRUCTION:
	    {
	      auto& shape_instr = shape_instructions.at(instr.index);
	      renderer.render_shape(shape_instr);
	    }
	    break;

	  default:
	    {}
	  }
      }
  }



}

#endif
