//-*-C++-*-

#ifndef PAGE_ITEM_RENDER_INSTRUCTION_H
#define PAGE_ITEM_RENDER_INSTRUCTION_H

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>

namespace pdflib
{
  enum RENDER_INSTRUCTION_NAME {
    SIZE_INSTRUCTION, // set the size of the canvas on which we render
    TEXT_RENDER_INSTRUCTION, // render text on the canvas
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
                     double r_x0, double r_y0,
                     double r_x1, double r_y1,
                     double r_x2, double r_y2,
                     double r_x3, double r_y3):
      text(std::move(text)),
      font_enc(std::move(font_enc)),
      font_key(std::move(font_key)),
      r_x0(r_x0), r_y0(r_y0),
      r_x1(r_x1), r_y1(r_y1),
      r_x2(r_x2), r_y2(r_y2),
      r_x3(r_x3), r_y3(r_y3) {}

    const std::string& get_text() const { return text; }

    const std::string& get_font_enc() const { return font_enc; }
    const std::string& get_font_key() const { return font_key; }

    double get_r_x0() const { return r_x0; }
    double get_r_y0() const { return r_y0; }
    double get_r_x1() const { return r_x1; }
    double get_r_y1() const { return r_y1; }
    double get_r_x2() const { return r_x2; }
    double get_r_y2() const { return r_y2; }
    double get_r_x3() const { return r_x3; }
    double get_r_y3() const { return r_y3; }

  private:

    const std::string text;

    const std::string font_enc;
    const std::string font_key;

    const double r_x0;
    const double r_y0;
    const double r_x1;
    const double r_y1;
    const double r_x2;
    const double r_y2;
    const double r_x3;
    const double r_y3;
  };

  class bitmap_instruction
  {
  public:
    const static RENDER_INSTRUCTION_NAME instr = BITMAP_RENDER_INSTRUCTION;

    bitmap_instruction(std::string xobject_key,
		       std::shared_ptr<std::vector<uint8_t> > data,
                       std::array<int, 3> shape,
                       double r_x0, double r_y0,
                       double r_x1, double r_y1,
                       double r_x2, double r_y2,
                       double r_x3, double r_y3):
      xobject_key(xobject_key),
      data(std::move(data)),
      shape(shape),
      r_x0(r_x0), r_y0(r_y0),
      r_x1(r_x1), r_y1(r_y1),
      r_x2(r_x2), r_y2(r_y2),
      r_x3(r_x3), r_y3(r_y3) {}

    const std::string& get_key() const { return xobject_key; }
    
    const std::shared_ptr<std::vector<uint8_t> >& get_data() const { return data; }
    const std::array<int, 3>& get_shape() const { return shape; }

    double get_r_x0() const { return r_x0; }
    double get_r_y0() const { return r_y0; }
    double get_r_x1() const { return r_x1; }
    double get_r_y1() const { return r_y1; }
    double get_r_x2() const { return r_x2; }
    double get_r_y2() const { return r_y2; }
    double get_r_x3() const { return r_x3; }
    double get_r_y3() const { return r_y3; }

  private:

    const std::string xobject_key;
    
    const std::shared_ptr<std::vector<uint8_t> > data;
    const std::array<int, 3> shape;

    const double r_x0;
    const double r_y0;
    const double r_x1;
    const double r_y1;
    const double r_x2;
    const double r_y2;
    const double r_x3;
    const double r_y3;
  };

  class shape_instruction
  {
  public:
    const static RENDER_INSTRUCTION_NAME instr = SHAPE_RENDER_INSTRUCTION;

    shape_instruction(std::vector<double> x,
                      std::vector<double> y):
      x(std::move(x)),
      y(std::move(y)) {}

    const std::vector<double>& get_x() const { return x; }
    const std::vector<double>& get_y() const { return y; }
    size_t size() const { return x.size(); }

  private:

    const std::vector<double> x;
    const std::vector<double> y;
  };

  class pdf_render_instructions
  {
    typedef instruction instruction_type;

    typedef text_instruction text_instruction_type;
    typedef bitmap_instruction bitmap_instruction_type;
    typedef shape_instruction shape_instruction_type;

  public:

    // add instruction methods

    void set_size_instruction(std::array<double, 4> media_bbox,
                              std::array<double, 4> crop_bbox);

    void add_size_instruction(size_instruction& instr);
    void add_text_instruction(text_instruction_type instr);
    void add_bitmap_instruction(bitmap_instruction_type instr);
    void add_shape_instruction(shape_instruction_type instr);

    // render method
    template<typename renderer_type>
    void iterate_over_instructions(renderer_type& renderer);

  private:

    size_instruction size_instr;

    std::vector<instruction_type> instructions;

    std::vector<text_instruction_type> text_instructions;
    std::vector<bitmap_instruction_type> bitmap_instructions;
    std::vector<shape_instruction_type> shape_instructions;

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
