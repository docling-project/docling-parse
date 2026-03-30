//-*-C++-*-

#ifndef PDF_RENDERERS_H
#define PDF_RENDERERS_H

#include <cstdint>
#include <stdexcept>
#include <vector>
#include <array>
#include <memory>

#include <parse/page_items/render_instructions.h>
#include <render/config.h>

namespace pdflib
{

  enum RENDERER_NAME {
    NAIVE,
    BLEND2D,
    // SKIA,	
    // OPENCV
  };

  template<RENDERER_NAME name>
  class renderer
  {

  public:

    renderer() {}
    explicit renderer(render_config config) {}

    void set_size(size_instruction& instr) { throw std::logic_error(__FUNCTION__); }

    void render_text(text_instruction& instr) { throw std::logic_error(__FUNCTION__); }

    void render_bitmap(bitmap_instruction& instr) { throw std::logic_error(__FUNCTION__); }

    void render_shape(shape_instruction& instr) { throw std::logic_error(__FUNCTION__); }

  private:

    std::shared_ptr<std::vector<uint8_t> > canvas;
    std::array<int, 3> shape;
  };

}

#endif
