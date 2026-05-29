//-*-C++-*-

#ifndef PYBIND_DOCLING_THREADED_RESULTS_H
#define PYBIND_DOCLING_THREADED_RESULTS_H

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <parse.h>

namespace docling
{
  struct page_decode_timings
  {
    double make_page_decoder_s = 0.0;
    double decode_page_s = 0.0;
    double create_word_cells_s = 0.0;
    double create_line_cells_s = 0.0;
    double total_s = 0.0;
  };

  struct page_render_timings : page_decode_timings
  {
    double render_page_s = 0.0;
  };

  struct page_task_result
  {
    std::string doc_key;
    int page_number = 0;
    bool success = false;
    std::string error_message;
    std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE>> page_decoder;
  };

  struct page_decode_result : page_task_result
  {
    page_decode_timings timings;
  };

  struct page_render_result : page_task_result
  {
    page_render_timings timings;

    // RGBA pixel data laid out as {height, width, 4} row-major top-to-bottom.
    // Suitable for direct consumption by PIL:
    //   Image.frombuffer("RGBA", (w, h), data, "raw", "RGBA", 0, 1)
    std::shared_ptr<std::vector<unsigned char>> image_data;
    std::array<int, 3> image_shape{0, 0, 4}; // {height, width, channels}
  };
}

#endif
