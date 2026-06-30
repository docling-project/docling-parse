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
  };

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

  class shape_instruction
  {
  public:
    const static RENDER_INSTRUCTION_NAME instr = SHAPE_RENDER_INSTRUCTION;

    shape_instruction(std::vector<double> x,
                      std::vector<double> y,
                      page_shape_closing_type closing_type,
                      page_shape_type         shape_type,
                      double                  line_width,
                      std::array<int, 3>      rgb_stroking,
                      std::array<int, 3>      rgb_filling):
      x(std::move(x)),
      y(std::move(y)),
      closing_type(closing_type),
      shape_type(shape_type),
      line_width(line_width),
      rgb_stroking(rgb_stroking),
      rgb_filling(rgb_filling) {}

    const std::vector<double>& get_x() const { return x; }
    const std::vector<double>& get_y() const { return y; }
    size_t size() const { return x.size(); }

    page_shape_closing_type     get_closing_type()  const { return closing_type; }
    page_shape_type             get_shape_type()    const { return shape_type; }
    double                      get_line_width()    const { return line_width; }
    const std::array<int, 3>&   get_rgb_stroking()  const { return rgb_stroking; }
    const std::array<int, 3>&   get_rgb_filling()   const { return rgb_filling; }

  private:

    const std::vector<double> x;
    const std::vector<double> y;

    const page_shape_closing_type closing_type;
    const page_shape_type         shape_type;
    const double                  line_width;
    const std::array<int, 3>      rgb_stroking;
    const std::array<int, 3>      rgb_filling;
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
