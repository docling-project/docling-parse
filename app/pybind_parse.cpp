//-*-C++-*-

#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/buffer_info.h>

#include <pybind/utils/pybind11_json.h>

#include <pybind/docling_parser.h>
#include <pybind/docling_threaded_parser.h>
#include <pybind/docling_threaded_renderer.h>
#include <render/blend2d_renderer.h>

// Include parse headers for typed bindings
#include <parse.h>

namespace
{
  const char* pixel_format_name(pdflib::pixel_format fmt)
  {
    switch(fmt)
      {
      case pdflib::PIXEL_FORMAT_GRAY: return "gray";
      case pdflib::PIXEL_FORMAT_RGB: return "rgb";
      case pdflib::PIXEL_FORMAT_CMYK: return "cmyk";
      default: return "unknown";
      }
  }

  const char* instruction_name(pdflib::RENDER_INSTRUCTION_NAME name)
  {
    switch(name)
      {
      case pdflib::SIZE_INSTRUCTION: return "size";
      case pdflib::TEXT_RENDER_INSTRUCTION: return "text";
      case pdflib::TEXT_WIDGET_RENDER_INSTRUCTION: return "widget";
      case pdflib::BITMAP_RENDER_INSTRUCTION: return "bitmap";
      case pdflib::SHAPE_RENDER_INSTRUCTION: return "shape";
      default: return "unknown";
      }
  }

  pybind11::dict make_quad_dict(double x0, double y0,
                                double x1, double y1,
                                double x2, double y2,
                                double x3, double y3)
  {
    pybind11::dict quad;
    quad["r_x0"] = x0;
    quad["r_y0"] = y0;
    quad["r_x1"] = x1;
    quad["r_y1"] = y1;
    quad["r_x2"] = x2;
    quad["r_y2"] = y2;
    quad["r_x3"] = x3;
    quad["r_y3"] = y3;
    return quad;
  }

  struct render_instruction_export_visitor
  {
    pybind11::dict root;
    pybind11::list instructions;

    render_instruction_export_visitor()
    {
      root["instructions"] = instructions;
    }

    void set_size(pdflib::size_instruction& instr)
    {
      pybind11::dict size;
      size["type"] = instruction_name(pdflib::SIZE_INSTRUCTION);
      size["media_bbox"] = instr.media_bbox;
      size["crop_bbox"] = instr.crop_bbox;
      root["size_instruction"] = size;
    }

    void render_text(pdflib::text_instruction& instr)
    {
      pybind11::dict row;
      row["type"] = instruction_name(pdflib::TEXT_RENDER_INSTRUCTION);
      row["text"] = instr.get_text();
      row["font_enc"] = instr.get_font_enc();
      row["font_key"] = instr.get_font_key();
      row["font_name"] = instr.get_font_name();
      row["encoding_name"] = instr.get_encoding_name();
      row["base_font"] = instr.get_base_font();
      row["font_size"] = instr.get_font_size();
      row["font_ascent_norm"] = instr.get_font_ascent_norm();
      row["font_descent_norm"] = instr.get_font_descent_norm();
      row["base_x0"] = instr.get_base_x0();
      row["base_y0"] = instr.get_base_y0();
      row["char_code"] = instr.get_char_code();
      row["glyph_name"] = instr.get_glyph_name();
      // Embedded font metadata only — never the raw font bytes.
      row["has_embedded_font"] = instr.has_embedded_font();
      if(instr.has_embedded_font())
        {
          const auto& blob = instr.get_embedded_font();
          pybind11::dict embedded;
          embedded["source_key"] = blob->get_source_key();
          embedded["format"] = pdflib::to_string(blob->get_format());
          embedded["byte_size"] = blob->byte_size();
          embedded["cache_key"] = blob->get_cache_key();
          embedded["is_cid_font"] = blob->get_is_cid_font();
          embedded["cid_to_gid_identity"] = blob->get_cid_to_gid_identity();
          row["embedded_font"] = embedded;
        }
      row["quad"] = make_quad_dict(
        instr.get_r_x0(), instr.get_r_y0(),
        instr.get_r_x1(), instr.get_r_y1(),
        instr.get_r_x2(), instr.get_r_y2(),
        instr.get_r_x3(), instr.get_r_y3());
      instructions.append(row);
    }

    void render_widget(pdflib::text_widget_instruction& instr)
    {
      pybind11::dict row;
      row["type"] = instruction_name(pdflib::TEXT_WIDGET_RENDER_INSTRUCTION);
      row["text"] = instr.get_text();
      row["bbox"] = std::array<double, 4>{
        instr.get_x0(), instr.get_y0(), instr.get_x1(), instr.get_y1()};
      row["quad"] = make_quad_dict(
        instr.get_r_x0(), instr.get_r_y0(),
        instr.get_r_x1(), instr.get_r_y1(),
        instr.get_r_x2(), instr.get_r_y2(),
        instr.get_r_x3(), instr.get_r_y3());
      instructions.append(row);
    }

    void render_bitmap(pdflib::bitmap_instruction& instr)
    {
      pybind11::dict row;
      row["type"] = instruction_name(pdflib::BITMAP_RENDER_INSTRUCTION);
      row["xobject_key"] = instr.get_key();
      row["shape"] = instr.get_shape();
      row["pixel_format"] = pixel_format_name(instr.get_pixel_format());
      row["image_mask"] = instr.is_image_mask();
      row["has_soft_mask"] = instr.has_alpha_data();
      row["rgb_filling"] = instr.get_rgb_filling();
      row["quad"] = make_quad_dict(
        instr.get_r_x0(), instr.get_r_y0(),
        instr.get_r_x1(), instr.get_r_y1(),
        instr.get_r_x2(), instr.get_r_y2(),
        instr.get_r_x3(), instr.get_r_y3());
      instructions.append(row);
    }

    void render_shape(pdflib::shape_instruction& instr)
    {
      pybind11::dict row;
      row["type"] = instruction_name(pdflib::SHAPE_RENDER_INSTRUCTION);
      row["x"] = instr.get_x();
      row["y"] = instr.get_y();
      row["closing_type"] = static_cast<int>(instr.get_closing_type());
      row["shape_type"] = static_cast<int>(instr.get_shape_type());
      row["line_width"] = instr.get_line_width();
      row["rgb_stroking"] = instr.get_rgb_stroking();
      row["rgb_filling"] = instr.get_rgb_filling();
      instructions.append(row);
    }
  };

  struct bitmap_artifact_export_visitor
  {
    pybind11::list artifacts;
    int bitmap_index = 0;

    void set_size(pdflib::size_instruction&) {}
    void render_text(pdflib::text_instruction&) {}
    void render_widget(pdflib::text_widget_instruction&) {}
    void render_shape(pdflib::shape_instruction&) {}

    void render_bitmap(pdflib::bitmap_instruction& instr)
    {
      ++bitmap_index;

      pybind11::dict row;
      row["index"] = bitmap_index;
      row["xobject_key"] = instr.get_key();
      row["shape"] = instr.get_shape();
      row["pixel_format"] = pixel_format_name(instr.get_pixel_format());
      row["image_mask"] = instr.is_image_mask();
      row["has_soft_mask"] = instr.has_alpha_data();
      row["rgb_filling"] = instr.get_rgb_filling();
      row["quad"] = make_quad_dict(
        instr.get_r_x0(), instr.get_r_y0(),
        instr.get_r_x1(), instr.get_r_y1(),
        instr.get_r_x2(), instr.get_r_y2(),
        instr.get_r_x3(), instr.get_r_y3());

      std::vector<uint8_t> encoded;
      std::string extension = ".bin";

      auto const& data = instr.get_data();
      auto const& alpha_data = instr.get_alpha_data();
      if(data)
        {
          row["raw_data"] = pybind11::bytes(
            reinterpret_cast<char const*>(data->data()),
            data->size());
        }
      else
        {
          row["raw_data"] = pybind11::bytes();
        }

      if(instr.has_data())
        {
          auto const& shape = instr.get_shape();
          const int height = shape[0];
          const int width = shape[1];

          if(instr.get_pixel_format() == pdflib::PIXEL_FORMAT_GRAY)
            {
              encoded = pdflib::ccitt::encode_debug_png(*data, width, height);
              extension = ".png";
            }
          else if(instr.get_pixel_format() == pdflib::PIXEL_FORMAT_RGB
                  or instr.get_pixel_format() == pdflib::PIXEL_FORMAT_CMYK)
            {
              std::vector<uint8_t> composited;
              auto const* export_data = data.get();
              if(instr.get_pixel_format() == pdflib::PIXEL_FORMAT_RGB
                 and instr.has_alpha_data()
                 and alpha_data->size() >= static_cast<size_t>(width) * height)
                {
                  composited.resize(static_cast<size_t>(width) * height * 3);
                  for(int i = 0; i < width * height; ++i)
                    {
                      const uint8_t alpha = alpha_data->at(i);
                      for(int c = 0; c < 3; ++c)
                        {
                          const uint8_t src = data->at(static_cast<size_t>(i) * 3 + c);
                          composited[static_cast<size_t>(i) * 3 + c] =
                            static_cast<uint8_t>((static_cast<unsigned int>(src) * alpha
                                                  + 255u * (255u - alpha)) / 255u);
                        }
                    }
                  export_data = &composited;
                }

              pdflib::jpeg::jpeg_parameters params;
              params.width = width;
              params.height = height;
              params.bits_per_component = 8;
              params.color_space =
                (instr.get_pixel_format() == pdflib::PIXEL_FORMAT_RGB)
                ? pdflib::jpeg::ColorSpace::RGB
                : pdflib::jpeg::ColorSpace::CMYK;
              encoded = pdflib::jpeg::write_jpeg_from_raw_pixels_to_memory(
                reinterpret_cast<unsigned char const*>(export_data->data()),
                export_data->size(),
                params);
              extension = ".jpg";
            }

          if(encoded.empty())
            {
              encoded.assign(data->begin(), data->end());
              extension = ".bin";
            }
        }

      row["extension"] = extension;
      row["encoded_data"] = pybind11::bytes(
        reinterpret_cast<char const*>(encoded.data()),
        encoded.size());
      artifacts.append(row);
    }
  };
}

PYBIND11_MODULE(pdf_parsers, m) {

  // ============= Decode Page Config =============

  pybind11::class_<pdflib::decode_config>(m, "DecodePageConfig",
    R"(
    Configuration parameters for page decoding.

    Attributes:
        page_boundary (str): The page boundary specification [choices: crop_box, media_box].
        do_sanitization (bool): Sanitize the chars into lines [default=true].
        keep_char_cells (bool): Expose individual char cells in the decoded output. Internal base text cells are still built when words or lines are requested [default=true].
        keep_shapes (bool): Keep all the graphic shapes [default=true].
        keep_bitmaps (bool): Keep all the bitmap resources [default=true].
        max_num_lines (int): Maximum number of lines to keep (-1 means no cap) [default=-1].
        max_num_bitmaps (int): Maximum number of bitmaps to keep (-1 means no cap) [default=-1].
        min_visible_clip_extent (float): Minimum clip width/height treated as a usable image clip [default=1e-3].
        keep_glyphs (bool): If true, keep GLYPH<...> fallback strings in output; if false, replace them with a space [default=false].
        keep_qpdf_warnings (bool): If true, QPDF warnings are emitted; if false, they are suppressed [default=false].
    )")
    .def(pybind11::init<>())
    .def_readwrite("page_boundary", &pdflib::decode_config::page_boundary)
    .def_readwrite("do_sanitization", &pdflib::decode_config::do_sanitization)
    .def_readwrite("keep_char_cells", &pdflib::decode_config::keep_char_cells)
    .def_readwrite("keep_shapes", &pdflib::decode_config::keep_shapes)
    .def_readwrite("keep_bitmaps", &pdflib::decode_config::keep_bitmaps)
    .def_readwrite("max_num_lines", &pdflib::decode_config::max_num_lines)
    .def_readwrite("max_num_bitmaps", &pdflib::decode_config::max_num_bitmaps)
    .def_readwrite("min_visible_clip_extent", &pdflib::decode_config::min_visible_clip_extent)
    .def_readwrite("create_word_cells", &pdflib::decode_config::create_word_cells)
    .def_readwrite("create_line_cells", &pdflib::decode_config::create_line_cells)
    .def_readwrite("enforce_same_font", &pdflib::decode_config::enforce_same_font)
    .def_readwrite("horizontal_cell_tolerance", &pdflib::decode_config::horizontal_cell_tolerance)
    .def_readwrite("word_space_width_factor_for_merge", &pdflib::decode_config::word_space_width_factor_for_merge)
    .def_readwrite("line_space_width_factor_for_merge", &pdflib::decode_config::line_space_width_factor_for_merge)
    .def_readwrite("line_space_width_factor_for_merge_with_space", &pdflib::decode_config::line_space_width_factor_for_merge_with_space)
    .def_readwrite("do_thread_safe", &pdflib::decode_config::do_thread_safe)
    .def_readwrite("release_native_memory_every_n_pages", &pdflib::decode_config::release_native_memory_every_n_pages)
    .def_readwrite("keep_glyphs", &pdflib::decode_config::keep_glyphs)
    .def_readwrite("keep_qpdf_warnings", &pdflib::decode_config::keep_qpdf_warnings)
    .def_readwrite("extract_font_programs", &pdflib::decode_config::extract_font_programs)
    .def("__copy__", [](const pdflib::decode_config& self) { return self; })
    .def("__deepcopy__", [](const pdflib::decode_config& self, pybind11::dict) { return self; });

  // ============= Typed Resource Bindings (for zero-copy access) =============

  // PdfCell - individual text cell with bounding box and text content
  pybind11::class_<pdflib::page_item<pdflib::PAGE_CELL>>(m, "PdfCell")
    .def_readonly("x0", &pdflib::page_item<pdflib::PAGE_CELL>::x0)
    .def_readonly("y0", &pdflib::page_item<pdflib::PAGE_CELL>::y0)
    .def_readonly("x1", &pdflib::page_item<pdflib::PAGE_CELL>::x1)
    .def_readonly("y1", &pdflib::page_item<pdflib::PAGE_CELL>::y1)
    .def_readonly("r_x0", &pdflib::page_item<pdflib::PAGE_CELL>::r_x0)
    .def_readonly("r_y0", &pdflib::page_item<pdflib::PAGE_CELL>::r_y0)
    .def_readonly("r_x1", &pdflib::page_item<pdflib::PAGE_CELL>::r_x1)
    .def_readonly("r_y1", &pdflib::page_item<pdflib::PAGE_CELL>::r_y1)
    .def_readonly("r_x2", &pdflib::page_item<pdflib::PAGE_CELL>::r_x2)
    .def_readonly("r_y2", &pdflib::page_item<pdflib::PAGE_CELL>::r_y2)
    .def_readonly("r_x3", &pdflib::page_item<pdflib::PAGE_CELL>::r_x3)
    .def_readonly("r_y3", &pdflib::page_item<pdflib::PAGE_CELL>::r_y3)
    .def_readonly("text", &pdflib::page_item<pdflib::PAGE_CELL>::text)
    .def_readonly("rendering_mode", &pdflib::page_item<pdflib::PAGE_CELL>::rendering_mode)
    .def_readonly("space_width", &pdflib::page_item<pdflib::PAGE_CELL>::space_width)
    .def_readonly("enc_name", &pdflib::page_item<pdflib::PAGE_CELL>::enc_name)
    .def_readonly("font_enc", &pdflib::page_item<pdflib::PAGE_CELL>::font_enc)
    .def_readonly("font_key", &pdflib::page_item<pdflib::PAGE_CELL>::font_key)
    .def_readonly("font_name", &pdflib::page_item<pdflib::PAGE_CELL>::font_name)
    .def_readonly("widget", &pdflib::page_item<pdflib::PAGE_CELL>::widget)
    .def_readonly("left_to_right", &pdflib::page_item<pdflib::PAGE_CELL>::left_to_right);

  // PdfShape - graphic shape with coordinates
  pybind11::class_<pdflib::page_item<pdflib::PAGE_SHAPE>>(m, "PdfShape")
    .def("get_x", &pdflib::page_item<pdflib::PAGE_SHAPE>::get_x,
	 pybind11::return_value_policy::reference_internal,
	 "Get x coordinates of shape points")
    .def("get_y", &pdflib::page_item<pdflib::PAGE_SHAPE>::get_y,
	 pybind11::return_value_policy::reference_internal,
	 "Get y coordinates of shape points")
    .def("get_i", &pdflib::page_item<pdflib::PAGE_SHAPE>::get_i,
	 pybind11::return_value_policy::reference_internal,
	 "Get segment indices")
    .def("__len__", &pdflib::page_item<pdflib::PAGE_SHAPE>::size)
    .def("get_has_graphics_state", &pdflib::page_item<pdflib::PAGE_SHAPE>::get_has_graphics_state,
	 "Check if graphics state has been set")
    .def("get_line_width", &pdflib::page_item<pdflib::PAGE_SHAPE>::get_line_width,
	 "Get line width")
    .def("get_miter_limit", &pdflib::page_item<pdflib::PAGE_SHAPE>::get_miter_limit,
	 "Get miter limit")
    .def("get_line_cap", &pdflib::page_item<pdflib::PAGE_SHAPE>::get_line_cap,
	 "Get line cap style")
    .def("get_line_join", &pdflib::page_item<pdflib::PAGE_SHAPE>::get_line_join,
	 "Get line join style")
    .def("get_dash_phase", &pdflib::page_item<pdflib::PAGE_SHAPE>::get_dash_phase,
	 "Get dash phase")
    .def("get_dash_array", &pdflib::page_item<pdflib::PAGE_SHAPE>::get_dash_array,
	 pybind11::return_value_policy::reference_internal,
	 "Get dash array")
    .def("get_flatness", &pdflib::page_item<pdflib::PAGE_SHAPE>::get_flatness,
	 "Get flatness tolerance")
    .def("get_rgb_stroking_ops", &pdflib::page_item<pdflib::PAGE_SHAPE>::get_rgb_stroking_ops,
	 pybind11::return_value_policy::reference_internal,
	 "Get RGB stroking color")
    .def("get_rgb_filling_ops", &pdflib::page_item<pdflib::PAGE_SHAPE>::get_rgb_filling_ops,
	 pybind11::return_value_policy::reference_internal,
	 "Get RGB filling color");

  // PdfImage - bitmap resource with bounding box and image data
  pybind11::class_<pdflib::page_item<pdflib::PAGE_IMAGE>>(m, "PdfImage")
    .def_readonly("x0", &pdflib::page_item<pdflib::PAGE_IMAGE>::x0)
    .def_readonly("y0", &pdflib::page_item<pdflib::PAGE_IMAGE>::y0)
    .def_readonly("x1", &pdflib::page_item<pdflib::PAGE_IMAGE>::x1)
    .def_readonly("y1", &pdflib::page_item<pdflib::PAGE_IMAGE>::y1)
    .def_readonly("is_visible", &pdflib::page_item<pdflib::PAGE_IMAGE>::is_visible)
    .def_readonly("has_visible_bbox", &pdflib::page_item<pdflib::PAGE_IMAGE>::has_visible_bbox)
    .def_readonly("visible_x0", &pdflib::page_item<pdflib::PAGE_IMAGE>::visible_x0)
    .def_readonly("visible_y0", &pdflib::page_item<pdflib::PAGE_IMAGE>::visible_y0)
    .def_readonly("visible_x1", &pdflib::page_item<pdflib::PAGE_IMAGE>::visible_x1)
    .def_readonly("visible_y1", &pdflib::page_item<pdflib::PAGE_IMAGE>::visible_y1)
    .def_readonly("image_width", &pdflib::page_item<pdflib::PAGE_IMAGE>::image_width)
    .def_readonly("image_height", &pdflib::page_item<pdflib::PAGE_IMAGE>::image_height)
    .def("get_image_format", &pdflib::page_item<pdflib::PAGE_IMAGE>::get_image_format,
	 "Get image format hint: 'jpeg', 'jp2', 'jbig2', or 'raw'")
    .def("get_pil_mode", &pdflib::page_item<pdflib::PAGE_IMAGE>::get_pil_mode,
	 "Get PIL-compatible mode string: 'L', 'RGB', 'CMYK', or '1'")
    .def("get_image_as_bytes",
	 [](pdflib::page_item<pdflib::PAGE_IMAGE> const& self) {
	   auto data = self.get_image_as_bytes();
	   return pybind11::bytes(reinterpret_cast<char const*>(data.data()),
				  data.size());
	 },
	 "Get image data as bytes (corrected JPEG, raw JP2, or decoded pixels)");

  // PdfPageDimension - page geometry and bounding boxes
  pybind11::class_<pdflib::page_item<pdflib::PAGE_DIMENSION>>(m, "PdfPageDimension")
    .def("get_angle", &pdflib::page_item<pdflib::PAGE_DIMENSION>::get_angle,
	 "Get page rotation angle in degrees")
    .def("get_crop_bbox", &pdflib::page_item<pdflib::PAGE_DIMENSION>::get_crop_bbox,
	 "Get crop box as [x0, y0, x1, y1]")
    .def("get_media_bbox", &pdflib::page_item<pdflib::PAGE_DIMENSION>::get_media_bbox,
	 "Get media box as [x0, y0, x1, y1]");

  // PdfWidget - form field widget with bounding box and field info
  pybind11::class_<pdflib::page_item<pdflib::PAGE_WIDGET>>(m, "PdfWidget")
    .def_readonly("x0", &pdflib::page_item<pdflib::PAGE_WIDGET>::x0)
    .def_readonly("y0", &pdflib::page_item<pdflib::PAGE_WIDGET>::y0)
    .def_readonly("x1", &pdflib::page_item<pdflib::PAGE_WIDGET>::x1)
    .def_readonly("y1", &pdflib::page_item<pdflib::PAGE_WIDGET>::y1)
    .def_readonly("text", &pdflib::page_item<pdflib::PAGE_WIDGET>::text)
    .def_readonly("description", &pdflib::page_item<pdflib::PAGE_WIDGET>::description)
    .def_readonly("field_name", &pdflib::page_item<pdflib::PAGE_WIDGET>::field_name)
    .def_readonly("field_type", &pdflib::page_item<pdflib::PAGE_WIDGET>::field_type);

  // PdfHyperlink - hyperlink annotation with bounding box and URI
  pybind11::class_<pdflib::page_item<pdflib::PAGE_HYPERLINK>>(m, "PdfHyperlink")
    .def_readonly("x0", &pdflib::page_item<pdflib::PAGE_HYPERLINK>::x0)
    .def_readonly("y0", &pdflib::page_item<pdflib::PAGE_HYPERLINK>::y0)
    .def_readonly("x1", &pdflib::page_item<pdflib::PAGE_HYPERLINK>::x1)
    .def_readonly("y1", &pdflib::page_item<pdflib::PAGE_HYPERLINK>::y1)
    .def_readonly("uri", &pdflib::page_item<pdflib::PAGE_HYPERLINK>::uri);

  // ============= Container Type Bindings =============

  // PdfCells - iterable container of PdfCell objects
  pybind11::class_<pdflib::page_item<pdflib::PAGE_CELLS>>(m, "PdfCells")
    .def("__len__", &pdflib::page_item<pdflib::PAGE_CELLS>::size)
    .def("__getitem__", [](pdflib::page_item<pdflib::PAGE_CELLS>& self, size_t i)
	 -> pdflib::page_item<pdflib::PAGE_CELL>& {
	   if (i >= self.size()) {
	     throw pybind11::index_error("index out of range");
	   }
	   return self[i];
	 }, pybind11::return_value_policy::reference_internal)
    .def("__iter__", [](pdflib::page_item<pdflib::PAGE_CELLS>& self) {
	   return pybind11::make_iterator(self.begin(), self.end());
	 }, pybind11::keep_alive<0, 1>());

  // PdfShapes - iterable container of PdfShape objects
  pybind11::class_<pdflib::page_item<pdflib::PAGE_SHAPES>>(m, "PdfShapes")
    .def("__len__", &pdflib::page_item<pdflib::PAGE_SHAPES>::size)
    .def("__getitem__", [](pdflib::page_item<pdflib::PAGE_SHAPES>& self, size_t i)
	 -> pdflib::page_item<pdflib::PAGE_SHAPE>& {
	   if (i >= self.size()) {
	     throw pybind11::index_error("index out of range");
	   }
	   return self[i];
	 }, pybind11::return_value_policy::reference_internal)
    .def("__iter__", [](pdflib::page_item<pdflib::PAGE_SHAPES>& self) {
	   return pybind11::make_iterator(self.begin(), self.end());
	 }, pybind11::keep_alive<0, 1>());

  // PdfImages - iterable container of PdfImage objects
  pybind11::class_<pdflib::page_item<pdflib::PAGE_IMAGES>>(m, "PdfImages")
    .def("__len__", &pdflib::page_item<pdflib::PAGE_IMAGES>::size)
    .def("__getitem__", [](pdflib::page_item<pdflib::PAGE_IMAGES>& self, size_t i)
	 -> pdflib::page_item<pdflib::PAGE_IMAGE>& {
	   if (i >= self.size()) {
	     throw pybind11::index_error("index out of range");
	   }
	   return self[i];
	 }, pybind11::return_value_policy::reference_internal)
    .def("__iter__", [](pdflib::page_item<pdflib::PAGE_IMAGES>& self) {
	   return pybind11::make_iterator(self.begin(), self.end());
	 }, pybind11::keep_alive<0, 1>());

  // PdfWidgets - iterable container of PdfWidget objects
  pybind11::class_<pdflib::page_item<pdflib::PAGE_WIDGETS>>(m, "PdfWidgets")
    .def("__len__", &pdflib::page_item<pdflib::PAGE_WIDGETS>::size)
    .def("__getitem__", [](pdflib::page_item<pdflib::PAGE_WIDGETS>& self, size_t i)
	 -> pdflib::page_item<pdflib::PAGE_WIDGET>& {
	   if (i >= self.size()) {
	     throw pybind11::index_error("index out of range");
	   }
	   return self[i];
	 }, pybind11::return_value_policy::reference_internal)
    .def("__iter__", [](pdflib::page_item<pdflib::PAGE_WIDGETS>& self) {
	   return pybind11::make_iterator(self.begin(), self.end());
	 }, pybind11::keep_alive<0, 1>());

  // PdfHyperlinks - iterable container of PdfHyperlink objects
  pybind11::class_<pdflib::page_item<pdflib::PAGE_HYPERLINKS>>(m, "PdfHyperlinks")
    .def("__len__", &pdflib::page_item<pdflib::PAGE_HYPERLINKS>::size)
    .def("__getitem__", [](pdflib::page_item<pdflib::PAGE_HYPERLINKS>& self, size_t i)
	 -> pdflib::page_item<pdflib::PAGE_HYPERLINK>& {
	   if (i >= self.size()) {
	     throw pybind11::index_error("index out of range");
	   }
	   return self[i];
	 }, pybind11::return_value_policy::reference_internal)
    .def("__iter__", [](pdflib::page_item<pdflib::PAGE_HYPERLINKS>& self) {
	   return pybind11::make_iterator(self.begin(), self.end());
	 }, pybind11::keep_alive<0, 1>());

  // ============= Page Decoder Binding =============

  // PdfPageDecoder - provides typed access to decoded page data
  pybind11::class_<pdflib::pdf_decoder<pdflib::PAGE>,
		   std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE>>>(m, "PdfPageDecoder")
    .def("get_page_number", &pdflib::pdf_decoder<pdflib::PAGE>::get_page_number,
	 "Get the page number (0-indexed)")
    .def("get_page_dimension", &pdflib::pdf_decoder<pdflib::PAGE>::get_page_dimension,
	 pybind11::return_value_policy::reference_internal,
	 "Get page dimension/geometry")
    .def("get_char_cells", &pdflib::pdf_decoder<pdflib::PAGE>::get_char_cells,
	 pybind11::return_value_policy::reference_internal,
	 "Get individual character cells")
    .def("get_word_cells", &pdflib::pdf_decoder<pdflib::PAGE>::get_word_cells,
	 pybind11::return_value_policy::reference_internal,
	 "Get word cells (aggregated from char cells)")
    .def("get_line_cells", &pdflib::pdf_decoder<pdflib::PAGE>::get_line_cells,
	 pybind11::return_value_policy::reference_internal,
	 "Get line cells (aggregated from char cells)")
    .def("get_page_shapes", &pdflib::pdf_decoder<pdflib::PAGE>::get_page_shapes,
	 pybind11::return_value_policy::reference_internal,
	 "Get graphic shapes on the page")
    .def("get_page_images", &pdflib::pdf_decoder<pdflib::PAGE>::get_page_images,
	 pybind11::return_value_policy::reference_internal,
	 "Get bitmap/image resources on the page")
    .def("get_page_widgets", &pdflib::pdf_decoder<pdflib::PAGE>::get_page_widgets,
	 pybind11::return_value_policy::reference_internal,
	 "Get form field widgets on the page")
    .def("get_page_hyperlinks", &pdflib::pdf_decoder<pdflib::PAGE>::get_page_hyperlinks,
	 pybind11::return_value_policy::reference_internal,
	 "Get hyperlink annotations on the page")
    .def("has_word_cells", &pdflib::pdf_decoder<pdflib::PAGE>::has_word_cells,
	 "Check if word cells have been created")
    .def("has_line_cells", &pdflib::pdf_decoder<pdflib::PAGE>::has_line_cells,
	 "Check if line cells have been created")
    .def("get_timings", [](pdflib::pdf_decoder<pdflib::PAGE>& self) {
	   // Return as Dict[str, float] (sums) for backward compatibility
	   return self.get_timings().to_sum_map();
	 },
	 "Get timing information for page decoding as Dict[str, float]")
    .def("get_timings_raw", [](pdflib::pdf_decoder<pdflib::PAGE>& self) {
	   // Return as Dict[str, List[float]] for detailed timing data
	   return self.get_timings().get_raw_data();
	 },
	 "Get detailed timing information as Dict[str, List[float]]")
    .def("get_static_timings", [](pdflib::pdf_decoder<pdflib::PAGE>& self) {
	   return self.get_timings().get_static_timings();
	 },
	 "Get only static (constant) timing keys as Dict[str, float]")
    .def("get_dynamic_timings", [](pdflib::pdf_decoder<pdflib::PAGE>& self) {
	   return self.get_timings().get_dynamic_timings();
	 },
	 "Get only dynamic timing keys as Dict[str, float]")
    .def("create_word_cells", &pdflib::pdf_decoder<pdflib::PAGE>::create_word_cells,
	 pybind11::arg("config"),
	 "Recompute word cells from char cells with the given config")
    .def("create_line_cells", &pdflib::pdf_decoder<pdflib::PAGE>::create_line_cells,
	 pybind11::arg("config"),
	 "Recompute line cells from char cells with the given config")
    .def("export_render_instructions_json",
         [](pdflib::pdf_decoder<pdflib::PAGE>& self) -> pybind11::dict {
           render_instruction_export_visitor visitor;
           self.get_instructions().iterate_over_instructions(visitor);
           return visitor.root;
         },
         "Export render instructions in deterministic decode order")
    .def("export_bitmap_artifacts",
         [](pdflib::pdf_decoder<pdflib::PAGE>& self) -> pybind11::list {
           bitmap_artifact_export_visitor visitor;
           self.get_instructions().iterate_over_instructions(visitor);
           return visitor.artifacts;
         },
         "Export bitmap artifacts as inspectable image bytes plus raw payload bytes")
    .def("render_image",
         [](pdflib::pdf_decoder<pdflib::PAGE>& self,
            const pdflib::render_config& config) -> pybind11::tuple {
           pdflib::renderer<pdflib::BLEND2D> rnd(config);
           {
             pybind11::gil_scoped_release release;
             self.get_instructions().iterate_over_instructions(rnd);
           }

           auto canvas = rnd.get_canvas();
           const auto& shape = rnd.get_shape();
           pybind11::bytes image_bytes("");
           if(canvas and not canvas->empty())
             {
               image_bytes = pybind11::bytes(
                   reinterpret_cast<const char*>(canvas->data()),
                   canvas->size());
             }
           return pybind11::make_tuple(image_bytes, shape);
         },
         pybind11::arg("config"),
         "Render the decoded page to RGBA bytes using the provided RenderConfig");

  // ============= Timing Keys Constants =============

  m.attr("TIMING_KEY_DECODE_PAGE") = pdflib::pdf_timings::KEY_DECODE_PAGE;
  m.attr("TIMING_KEY_DECODE_DIMENSIONS") = pdflib::pdf_timings::KEY_DECODE_DIMENSIONS;
  m.attr("TIMING_KEY_DECODE_RESOURCES") = pdflib::pdf_timings::KEY_DECODE_RESOURCES;
  m.attr("TIMING_KEY_DECODE_GRPHS") = pdflib::pdf_timings::KEY_DECODE_GRPHS;
  m.attr("TIMING_KEY_DECODE_FONTS") = pdflib::pdf_timings::KEY_DECODE_FONTS;
  m.attr("TIMING_KEY_DECODE_XOBJECTS") = pdflib::pdf_timings::KEY_DECODE_XOBJECTS;
  m.attr("TIMING_KEY_DECODE_CONTENTS") = pdflib::pdf_timings::KEY_DECODE_CONTENTS;
  m.attr("TIMING_KEY_DECODE_ANNOTS") = pdflib::pdf_timings::KEY_DECODE_ANNOTS;
  m.attr("TIMING_KEY_SANITISE_CONTENTS") = pdflib::pdf_timings::KEY_SANITISE_CONTENTS;
  m.attr("TIMING_KEY_CREATE_WORD_CELLS") = pdflib::pdf_timings::KEY_CREATE_WORD_CELLS;
  m.attr("TIMING_KEY_CREATE_LINE_CELLS") = pdflib::pdf_timings::KEY_CREATE_LINE_CELLS;
  m.attr("TIMING_KEY_DECODE_FONTS_TOTAL") = pdflib::pdf_timings::KEY_DECODE_FONTS_TOTAL;
  m.attr("TIMING_KEY_DECODE_XOBJECTS_TOTAL") = pdflib::pdf_timings::KEY_DECODE_XOBJECTS_TOTAL;
  m.attr("TIMING_KEY_DECODE_GRPHS_TOTAL") = pdflib::pdf_timings::KEY_DECODE_GRPHS_TOTAL;

  // Additional decode_page step keys
  m.attr("TIMING_KEY_TO_JSON_PAGE") = pdflib::pdf_timings::KEY_TO_JSON_PAGE;
  m.attr("TIMING_KEY_EXTRACT_ANNOTS_JSON") = pdflib::pdf_timings::KEY_EXTRACT_ANNOTS_JSON;
  m.attr("TIMING_KEY_ROTATE_CONTENTS") = pdflib::pdf_timings::KEY_ROTATE_CONTENTS;
  m.attr("TIMING_KEY_SANITIZE_ORIENTATION") = pdflib::pdf_timings::KEY_SANITIZE_ORIENTATION;
  m.attr("TIMING_KEY_SANITIZE_CELLS") = pdflib::pdf_timings::KEY_SANITIZE_CELLS;

  m.attr("TIMING_KEY_PROCESS_DOCUMENT_FROM_FILE") = pdflib::pdf_timings::KEY_PROCESS_DOCUMENT_FROM_FILE;
  m.attr("TIMING_KEY_PROCESS_DOCUMENT_FROM_BYTESIO") = pdflib::pdf_timings::KEY_PROCESS_DOCUMENT_FROM_BYTESIO;
  m.attr("TIMING_KEY_QPDF_PROCESS") = pdflib::pdf_timings::KEY_QPDF_PROCESS;
  m.attr("TIMING_KEY_EXTRACT_DOC_ANNOTATIONS") = pdflib::pdf_timings::KEY_EXTRACT_DOC_ANNOTATIONS;
  m.attr("TIMING_KEY_DECODE_DOCUMENT") = pdflib::pdf_timings::KEY_DECODE_DOCUMENT;

  m.attr("TIMING_PREFIX_DECODE_FONT") = pdflib::pdf_timings::PREFIX_DECODE_FONT;
  m.attr("TIMING_PREFIX_DECODE_XOBJECT") = pdflib::pdf_timings::PREFIX_DECODE_XOBJECT;
  m.attr("TIMING_PREFIX_DECODE_GRPH") = pdflib::pdf_timings::PREFIX_DECODE_GRPH;
  m.attr("TIMING_PREFIX_DECODING_PAGE") = pdflib::pdf_timings::PREFIX_DECODING_PAGE;
  m.attr("TIMING_PREFIX_DECODE_PAGE") = pdflib::pdf_timings::PREFIX_DECODE_PAGE;

  m.def("get_static_timing_keys", &pdflib::pdf_timings::get_static_keys,
	"Get all static timing keys as Set[str]");
  m.def("is_static_timing_key", &pdflib::pdf_timings::is_static_key,
	pybind11::arg("key"),
	"Check if a timing key is static (constant)");
  m.def("get_decode_page_timing_keys", &pdflib::pdf_timings::get_decode_page_keys,
	"Get timing keys used in decode_page method (in order, excluding global timer) as List[str]");

  // ============= PDF Parser =============

  // next generation parser, 10x faster with more finegrained output
  pybind11::class_<docling::docling_parser>(m, "pdf_parser")
    .def(pybind11::init())

    .def(pybind11::init<const std::string&>(),
	 pybind11::arg("level"),
	 R"(
    Construct pdf_parser with logging level.

    Parameters:
        level (str): Logging level as a string.
                     One of ['fatal', 'error', 'warning', 'info'])")
    
    .def("set_loglevel_with_label",
	 [](docling::docling_parser &self, const std::string &level) -> void {
            self.set_loglevel_with_label(level);
	 },
	 pybind11::arg("level"),
	 R"(
    Set the log level using a string label.

    Parameters:
        level (str): Logging level as a string.
                     One of ['fatal', 'error', 'warning', 'info']
           )")

    .def("is_loaded",
	 [](docling::docling_parser &self, const std::string &key) -> bool {
	   return self.is_loaded(key);
	 },
	 pybind11::arg("key"),
	 R"(
    Check if a document with the given key is loaded.

    Parameters:
        key (str): The unique key of the document to check.

    Returns:
        bool: True if the document is loaded, False otherwise.)")
    
    .def("list_loaded_keys",
	 [](docling::docling_parser &self) -> std::vector<std::string> {
	   return self.list_loaded_keys();
	 },
	 R"(
    List the keys of the loaded documents.

    Returns:
        List[str]: A list of keys for the currently loaded documents.)")
    
    .def("load_document",
	 [](docling::docling_parser &self,
	    const std::string &key,
	    const std::string &filename,
	    std::optional<std::string>& password,
	    bool keep_qpdf_warnings
	    ) -> bool {
	   return self.load_document(key, filename, password, keep_qpdf_warnings);
	 },
	 pybind11::arg("key"),
	 pybind11::arg("filename"),
	 pybind11::arg("password") = pybind11::none(),
	 pybind11::arg("keep_qpdf_warnings") = false,
	 R"(
    Load a document by key and filename.

    Parameters:
        key (str): The unique key to identify the document.
        filename (str): The path to the document file to load.
        password (str, optional): Optional password for password-protected files

    Returns:
        bool: True if the document was successfully loaded, False otherwise.)")
    
    .def("load_document_from_bytesio",
	 [](docling::docling_parser &self,
	    const std::string &key,
	    pybind11::object bytes_io,
	    std::optional<std::string>& password,
	    bool keep_qpdf_warnings
	    ) -> bool {
	   return self.load_document_from_bytesio(key,
                                                  bytes_io,
                                                  password,
                                                  keep_qpdf_warnings);
	 },
	 pybind11::arg("key"),
	 pybind11::arg("bytes_io"),
	 pybind11::arg("password") = pybind11::none(),
	 pybind11::arg("keep_qpdf_warnings") = false,
	 R"(
    Load a document by key from a BytesIO-like object.

    Parameters:
        key (str): The unique key to identify the document.
        bytes_io (Any): A BytesIO-like object containing the document data.
        password (str, optional): Optional password for password-protected files

    Returns:
        bool: True if the document was successfully loaded, False otherwise.)")
    
    .def("unload_document",
	 [](docling::docling_parser &self, const std::string &key) -> bool {
	   return self.unload_document(key);
	 },
	 R"(
    Unload a document by its unique key.

    Parameters:
        key (str): The unique key of the document to unload.

    Returns:
        bool: True if the document was successfully unloaded, False otherwise.)")

    .def("unload_document_page",
	 [](docling::docling_parser &self, const std::string &key, int page) -> bool {
	   return self.unload_document_page(key, page);
	 },
	 pybind11::arg("key"),
	 pybind11::arg("page"),	 
	 R"(
    Unload a single page of the document by its unique key and page_number.

    Parameters:
        key (str): The unique key of the document to unload.
        page (int): The page number of the document to unload.

    Returns:
        bool: True if the document was successfully unloaded, False otherwise.)")
    
    .def("number_of_pages",
	 [](docling::docling_parser &self, const std::string &key) -> int {
	   return self.number_of_pages(key);
	 },
	 pybind11::arg("key"),
	 R"(
    Get the number of pages in the document identified by its unique key.

    Parameters:
        key (str): The unique key of the document.

    Returns:
        int: The number of pages in the document.)")

    .def("get_annotations",
	 [](docling::docling_parser &self, const std::string &key) -> nlohmann::json {
	   return self.get_annotations(key);
	 },
	 pybind11::arg("key"),
	 R"(
    Retrieve annotations for the document identified by its unique key and return them as JSON.

    Parameters:
        key (str): The unique key of the document.

    Returns:
        dict: A JSON object containing the annotations for the document.)")
    
    .def("get_table_of_contents",
	 [](docling::docling_parser &self, const std::string &key) -> nlohmann::json {
	   return self.get_table_of_contents(key);
	 },
	 pybind11::arg("key"),
	 R"(
    Retrieve the table of contents for the document identified by its unique key and return it as JSON.

    Parameters:
        key (str): The unique key of the document.

    Returns:
        dict: A JSON object representing the table of contents of the document.)")

    .def("get_meta_xml",
	 [](docling::docling_parser &self, const std::string &key) -> nlohmann::json {
	   return self.get_meta_xml(key);
	 },
	 pybind11::arg("key"),
	 R"(
    Retrieve the meta data in string or None.

    Parameters:
        key (str): The unique key of the document.

    Returns:
        dict: A None or string of the metadata in xml of the document.)")
    
    .def("get_page_decoder",
	 [](docling::docling_parser &self,
	    const std::string &key,
	    int page,
	    const pdflib::decode_config &config) -> std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE>> {
	   return self.get_page_decoder(key, page, config);
	 },
	 pybind11::arg("key"),
	 pybind11::arg("page"),
	 pybind11::arg("config"),
	 R"(
    Get a typed page decoder using a DecodePageConfig object.

    Parameters:
        key (str): The unique key of the document.
        page (int): The page number to parse (0-indexed).
        config (DecodePageConfig): Configuration object for page decoding.

    Returns:
        PdfPageDecoder: A typed page decoder object.)");

  // ============= Threaded PDF Parser =============

  pybind11::class_<docling::page_decode_timings>(m, "_PageDecodeTimings",
    R"(
    Top-level timing breakdown for a threaded page decode task.
    )")
    .def_readonly("make_page_decoder_s", &docling::page_decode_timings::make_page_decoder_s)
    .def_readonly("decode_page_s", &docling::page_decode_timings::decode_page_s)
    .def_readonly("create_word_cells_s", &docling::page_decode_timings::create_word_cells_s)
    .def_readonly("create_line_cells_s", &docling::page_decode_timings::create_line_cells_s)
    .def_readonly("total_s", &docling::page_decode_timings::total_s);

  pybind11::class_<docling::page_render_timings, docling::page_decode_timings>(m, "_PageRenderTimings",
    R"(
    Top-level timing breakdown for a threaded page render task.
    )")
    .def_readonly("render_page_s", &docling::page_render_timings::render_page_s);

  pybind11::class_<docling::page_task_result>(m, "_PageTaskResult")
    .def_readonly("doc_key", &docling::page_task_result::doc_key)
    .def_readonly("page_number", &docling::page_task_result::page_number)
    .def_readonly("success", &docling::page_task_result::success);

  // _PageDecodeResult - internal result of a threaded page decode task
  pybind11::class_<docling::page_decode_result, docling::page_task_result>(m, "_PageDecodeResult",
    R"(
    Internal result of a threaded page decoding task.

    Attributes:
        doc_key (str): The document key this page belongs to.
        page_number (int): The page number (0-indexed).
        success (bool): Whether the decoding succeeded.
    )")
    .def_readonly("timings", &docling::page_decode_result::timings)
    .def("get", [](docling::page_decode_result& self)
         -> std::pair<std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE>>,
                      std::unordered_map<std::string, double>> {
           if(!self.success)
             {
               throw std::runtime_error("Cannot get result from failed task: " + self.error_message);
             }
           auto timings_map = self.page_decoder->get_timings().to_sum_map();
           return std::make_pair(self.page_decoder, timings_map);
         },
         R"(
    Get the page decoder and timing information.

    Returns:
        Tuple[PdfPageDecoder, Dict[str, float]]: The page decoder and timing data.

    Raises:
        RuntimeError: If the task was not successful.)")
    .def("error", [](docling::page_decode_result& self) -> std::string {
           return self.error_message;
         },
         R"(
    Get the error message if the task failed.

    Returns:
        str: The error message.)");

  // _threaded_pdf_parser - internal parallel PDF parser with bounded result queue
  pybind11::class_<docling::docling_threaded_parser>(m, "_threaded_pdf_parser",
    R"(
    Internal threaded PDF parser that processes pages in parallel.

    Loads multiple documents and decodes their pages using a thread pool.
    Results are available via a bounded queue to control memory usage.
    )")
    .def(pybind11::init<const std::string&, int, int, pdflib::decode_config>(),
         pybind11::arg("loglevel") = "fatal",
         pybind11::arg("num_threads") = 4,
         pybind11::arg("max_concurrent_results") = 32,
         pybind11::arg("config") = pdflib::decode_config(),
         R"(
    Construct a threaded PDF parser.

    Parameters:
        loglevel (str): Logging level ('fatal', 'error', 'warning', 'info').
        num_threads (int): Number of worker threads.
        max_concurrent_results (int): Maximum results buffered before workers pause.
        config (DecodePageConfig): Configuration for page decoding.)")

    .def("load_document",
         [](docling::docling_threaded_parser& self,
            const std::string& key,
            const std::string& filename,
            std::optional<std::string>& password,
            std::optional<std::vector<int>>& page_numbers) -> bool {
           return self.load_document(key, filename, password, page_numbers);
         },
         pybind11::arg("key"),
         pybind11::arg("filename"),
         pybind11::arg("password") = pybind11::none(),
         pybind11::arg("page_numbers") = pybind11::none(),
         R"(
    Load a document by key and filename.

    Parameters:
        key (str): The unique key to identify the document.
        filename (str): The path to the document file to load.
        password (str, optional): Optional password for password-protected files.
        page_numbers (Sequence[int], optional): Selected 1-indexed physical pages to schedule.

    Returns:
        bool: True if the document was successfully loaded.)")

    .def("load_document_from_bytesio",
         [](docling::docling_threaded_parser& self,
            const std::string& key,
            pybind11::object bytes_io,
            std::optional<std::string>& password,
            std::optional<std::vector<int>>& page_numbers) -> bool {
           return self.load_document_from_bytesio(key, bytes_io, password, page_numbers);
         },
         pybind11::arg("key"),
         pybind11::arg("bytes_io"),
         pybind11::arg("password") = pybind11::none(),
         pybind11::arg("page_numbers") = pybind11::none(),
         R"(
    Load a document from a BytesIO-like object.

    Parameters:
        key (str): The unique key to identify the document.
        bytes_io (Any): A BytesIO-like object containing the document data.
        password (str, optional): Optional password for password-protected files.
        page_numbers (Sequence[int], optional): Selected 1-indexed physical pages to schedule.

    Returns:
        bool: True if the document was successfully loaded.)")

    .def("number_of_pages",
         [](docling::docling_threaded_parser& self, const std::string& key) -> int {
           return self.number_of_pages(key);
         },
         pybind11::arg("key"),
         R"(
    Return the number of pages in a loaded document.

    Parameters:
        key (str): The unique key identifying the document.

    Returns:
        int: Number of pages in the loaded document.)")
    .def("scheduled_number_of_pages",
         [](docling::docling_threaded_parser& self, const std::string& key) -> int {
           return self.scheduled_number_of_pages(key);
         },
         pybind11::arg("key"),
         R"(
    Return the number of scheduled pages in a loaded document.

    Parameters:
        key (str): The unique key identifying the document.

    Returns:
        int: Number of pages that will be emitted by the threaded parser.)")
    .def("unload_document",
         [](docling::docling_threaded_parser& self, const std::string& key) -> bool {
           return self.unload_document(key);
         },
         pybind11::arg("key"),
         R"(
    Unload one document after threaded processing is complete.

    Returns:
        bool: True when document state existed and was removed.)")
    .def("unload_all_documents",
         [](docling::docling_threaded_parser& self) {
           self.unload_all_documents();
         },
         R"(
    Unload all documents after threaded processing is complete.)")

    .def("has_tasks",
         [](docling::docling_threaded_parser& self) -> bool {
           return self.has_tasks();
         },
         R"(
    Check if there are remaining tasks to consume.

    On first call, builds the task queue from all loaded documents and starts worker threads.

    Returns:
        bool: True if there are remaining results to consume.)")

    .def("get_task",
         [](docling::docling_threaded_parser& self) -> docling::page_decode_result {
           pybind11::gil_scoped_release release;
           return self.get_task();
         },
         R"(
    Get the next completed page decode result.

    Blocks until a result is available. Releases the GIL while waiting.

    Returns:
        _PageDecodeResult: The result of a page decoding task.)");

  // ============= Threaded PDF Renderer =============

  // RenderConfig - configuration for the renderer
  pybind11::class_<pdflib::render_config>(m, "RenderConfig",
    R"(
    Configuration parameters for page rendering.

    Attributes:
        render_text (bool): Render glyph outlines for text cells [default=true].
        draw_text_bbox (bool): Draw bounding quad for each text cell [default=false].
        draw_text_basepoint (bool): Draw the text baseline origin as a small red dot [default=false].
        fit_glyph_bbox_to_target (bool): Uniformly rescale measured glyph outlines so the rendered bbox fits inside the target glyph bbox, with either width or height matching exactly [default=false].
        resolve_fonts (bool): Resolve PDF font names to system fonts [default=true].
        use_embedded_fonts (bool): Prefer embedded font programs (TrueType/OpenType via Blend2D, Type 1/CFF via FreeType outlines) over system font resolution [default=true].
        font_similarity_cutoff (float): Minimum Jaccard similarity for fuzzy font matching; candidates below this threshold fall back to the default font [default=0.25].
        scale (float): Target render scale in multiples of the PDF page size; -1 disables scale-based sizing [default=-1].
        canvas_width (int): Target canvas width in pixels; -1 means use PDF page size [default=-1].
        canvas_height (int): Target canvas height in pixels; -1 means use PDF page size [default=-1].
    )")
    .def(pybind11::init<>())
    .def_readwrite("render_text",             &pdflib::render_config::render_text)
    .def_readwrite("draw_text_bbox",          &pdflib::render_config::draw_text_bbox)
    .def_readwrite("draw_text_basepoint",     &pdflib::render_config::draw_text_basepoint)
    .def_readwrite("fit_glyph_bbox_to_target",&pdflib::render_config::fit_glyph_bbox_to_target)
    .def_readwrite("resolve_fonts",           &pdflib::render_config::resolve_fonts)
    .def_readwrite("use_embedded_fonts",      &pdflib::render_config::use_embedded_fonts)
    .def_readwrite("font_similarity_cutoff",  &pdflib::render_config::font_similarity_cutoff)
    .def_readwrite("scale",                   &pdflib::render_config::scale)
    .def_readwrite("canvas_width",            &pdflib::render_config::canvas_width)
    .def_readwrite("canvas_height",           &pdflib::render_config::canvas_height);

  // _PageRenderResult - internal result of a threaded page render task
  pybind11::class_<docling::page_render_result, docling::page_task_result>(m, "_PageRenderResult",
    R"(
    Internal result of a threaded page rendering task.

    Attributes:
        doc_key (str): The document key this page belongs to.
        page_number (int): The page number (0-indexed).
        success (bool): Whether the rendering succeeded.
        timings: Top-level timing breakdown for decode and render stages.
        image_data: Raw RGBA bytes of the rendered page (height x width x 4, row-major).
        image_shape: Shape of the image as [height, width, channels].
    )")
    .def_readonly("timings", &docling::page_render_result::timings)
    .def("get", [](docling::page_render_result& self)
         -> std::pair<std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE>>,
                      std::unordered_map<std::string, double>> {
           if(!self.success)
             {
               throw std::runtime_error("Cannot get result from failed task: " + self.error_message);
             }
           auto timings_map = self.page_decoder->get_timings().to_sum_map();
           return std::make_pair(self.page_decoder, timings_map);
         },
         R"(
    Get the page decoder and decoder-internal timing information.

    Returns:
        Tuple[PdfPageDecoder, Dict[str, float]]: The page decoder and timing data.

    Raises:
        RuntimeError: If the task was not successful.)")
    .def("error", [](docling::page_render_result& self) -> std::string {
           return self.error_message;
         },
         R"(
    Get the error message if the task failed.

    Returns:
        str: The error message.)")
    .def_readonly("image_shape", &docling::page_render_result::image_shape)
    .def("get_image", [](docling::page_render_result& self)
         -> pybind11::bytes {
           if(not self.image_data or self.image_data->empty())
             {
               return pybind11::bytes();
             }
           return pybind11::bytes(
             reinterpret_cast<const char*>(self.image_data->data()),
             self.image_data->size());
         },
         R"(
    Return the raw RGBA pixel data as Python bytes.

    Use together with image_shape to reconstruct a PIL image:
        result = renderer.get_task()
        h, w, _ = result.image_shape
        img = Image.frombuffer("RGBA", (w, h), result.get_image(), "raw", "RGBA", 0, 1)

    Returns:
        bytes: Raw RGBA pixel data, or empty bytes on failure.)");

  // _threaded_pdf_renderer - internal parallel PDF renderer with bounded result queue
  pybind11::class_<docling::docling_threaded_renderer>(m, "_threaded_pdf_renderer",
    R"(
    Internal threaded PDF renderer that decodes and renders pages in parallel.

    Loads multiple documents and renders their pages using a thread pool.
    Each result contains both the decoded page data and the rendered RGBA image.
    Results are available via a bounded queue to control memory usage.
    )")
    .def(pybind11::init<const std::string&, int, int,
                        pdflib::decode_config, pdflib::render_config>(),
         pybind11::arg("loglevel")               = "fatal",
         pybind11::arg("num_threads")            = 4,
         pybind11::arg("max_concurrent_results") = 32,
         pybind11::arg("decode_config")          = pdflib::decode_config(),
         pybind11::arg("render_config")          = pdflib::render_config(),
         R"(
    Construct a threaded PDF renderer.

    Parameters:
        loglevel (str): Logging level ('fatal', 'error', 'warning', 'info').
        num_threads (int): Number of worker threads.
        max_concurrent_results (int): Maximum results buffered before workers pause.
        decode_config (DecodePageConfig): Configuration for page decoding.
        render_config (RenderConfig): Configuration for page rendering.)")

    .def("load_document",
         [](docling::docling_threaded_renderer& self,
            const std::string& key,
            const std::string& filename,
            std::optional<std::string>& password,
            std::optional<std::vector<int>>& page_numbers) -> bool {
           return self.load_document(key, filename, password, page_numbers);
         },
         pybind11::arg("key"),
         pybind11::arg("filename"),
         pybind11::arg("password") = pybind11::none(),
         pybind11::arg("page_numbers") = pybind11::none())

    .def("load_document_from_bytesio",
         [](docling::docling_threaded_renderer& self,
            const std::string& key,
            pybind11::object bytes_io,
            std::optional<std::string>& password,
            std::optional<std::vector<int>>& page_numbers) -> bool {
           return self.load_document_from_bytesio(key, bytes_io, password, page_numbers);
         },
         pybind11::arg("key"),
         pybind11::arg("bytes_io"),
         pybind11::arg("password") = pybind11::none(),
         pybind11::arg("page_numbers") = pybind11::none())

    .def("number_of_pages",
         [](docling::docling_threaded_renderer& self, const std::string& key) -> int {
           return self.number_of_pages(key);
         },
         pybind11::arg("key"))
    .def("scheduled_number_of_pages",
         [](docling::docling_threaded_renderer& self, const std::string& key) -> int {
           return self.scheduled_number_of_pages(key);
         },
         pybind11::arg("key"))
    .def("unload_document",
         [](docling::docling_threaded_renderer& self, const std::string& key) -> bool {
           return self.unload_document(key);
         },
         pybind11::arg("key"))
    .def("unload_all_documents",
         [](docling::docling_threaded_renderer& self) {
           self.unload_all_documents();
         })

    .def("has_tasks",
         [](docling::docling_threaded_renderer& self) -> bool {
           return self.has_tasks();
         })

    .def("get_task",
         [](docling::docling_threaded_renderer& self) -> docling::page_render_result {
           pybind11::gil_scoped_release release;
           return self.get_task();
         },
         R"(
    Get the next completed page render result.

    Blocks until a result is available. Releases the GIL while waiting.

    Returns:
        _PageRenderResult: The result of a page rendering task.)");
}
