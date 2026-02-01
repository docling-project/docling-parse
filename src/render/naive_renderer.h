//-*-C++-*-

#ifndef PDF_NAIVE_RENDERER_H
#define PDF_NAIVE_RENDERER_H

#include <render/template_renderer.h>

namespace pdflib
{
  template<>
  class renderer<NAIVE>
  {

  public:

    renderer();

    void set_size(size_instruction& instr);

    void render_text(text_instruction& instr);

    void render_bitmap(bitmap_instruction& instr);

    void render_shape(shape_instruction& instr);

  private:

    std::shared_ptr<std::vector<uint8_t> > canvas;
    std::array<int, 3> shape;
  };

  inline renderer<NAIVE>::renderer():
    canvas(std::make_shared<std::vector<uint8_t> >()),
    shape({0, 0, 3})
  {}

  inline void renderer<NAIVE>::set_size(size_instruction& instr)
  {
    auto& bbox = instr.crop_bbox;

    int width  = bbox[2] - bbox[0];
    int height = bbox[3] - bbox[1];

    shape = {height, width, 3};
    canvas->assign(height * width * 3, 255);
  }

  inline void renderer<NAIVE>::render_text(text_instruction& instr)
  {
    LOG_S(INFO) << __FUNCTION__
                << "  text='" << instr.get_text() << "'"
      //<< "\tfont_enc='" << instr.get_font_enc() << "'"
                << "  font_key='" << instr.get_font_key() << "'"
                << "  rect=[("
                << instr.get_r_x0() << ", " << instr.get_r_y0() << "), ("
                << instr.get_r_x1() << ", " << instr.get_r_y1() << "), ("
                << instr.get_r_x2() << ", " << instr.get_r_y2() << "), ("
                << instr.get_r_x3() << ", " << instr.get_r_y3() << ")]";
  }

  inline void renderer<NAIVE>::render_bitmap(bitmap_instruction& instr)
  {
    LOG_S(INFO) << __FUNCTION__
		<< "  key='" << instr.get_key() << "'"
                << "  rect=[("
                << instr.get_r_x0() << ", " << instr.get_r_y0() << "), ("
                << instr.get_r_x1() << ", " << instr.get_r_y1() << "), ("
                << instr.get_r_x2() << ", " << instr.get_r_y2() << "), ("
                << instr.get_r_x3() << ", " << instr.get_r_y3() << ")]";    
  }

  inline void renderer<NAIVE>::render_shape(shape_instruction& instr)
  {
    LOG_S(INFO) << __FUNCTION__
                << "  #-points=" << instr.size();
  }

}

#endif
