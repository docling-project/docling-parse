//-*-C++-*-

#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/buffer_info.h>

#include <pybind/utils/pybind11_json.h>

#include <pybind/docling_parser.h>

// Include parse headers for typed bindings
#include <parse.h>

PYBIND11_MODULE(pdf_parsers, m) {

  // ============= Decode Page Config =============

  pybind11::class_<pdflib::decode_page_config>(m, "DecodePageConfig",
    R"(
    Configuration parameters for page decoding.

    Attributes:
        page_boundary (str): The page boundary specification [choices: crop_box, media_box].
        do_sanitization (bool): Sanitize the chars into lines [default=true].
        keep_char_cells (bool): Keep all the individual char cells [default=true].
        keep_shapes (bool): Keep all the graphic shapes [default=true].
        keep_bitmaps (bool): Keep all the bitmap resources [default=true].
        max_num_lines (int): Maximum number of lines to keep (-1 means no cap) [default=-1].
        max_num_bitmaps (int): Maximum number of bitmaps to keep (-1 means no cap) [default=-1].
    )")
    .def(pybind11::init<>())
    .def_readwrite("page_boundary", &pdflib::decode_page_config::page_boundary)
    .def_readwrite("do_sanitization", &pdflib::decode_page_config::do_sanitization)
    .def_readwrite("keep_char_cells", &pdflib::decode_page_config::keep_char_cells)
    .def_readwrite("keep_shapes", &pdflib::decode_page_config::keep_shapes)
    .def_readwrite("keep_bitmaps", &pdflib::decode_page_config::keep_bitmaps)
    .def_readwrite("max_num_lines", &pdflib::decode_page_config::max_num_lines)
    .def_readwrite("max_num_bitmaps", &pdflib::decode_page_config::max_num_bitmaps)
    .def_readwrite("create_word_cells", &pdflib::decode_page_config::create_word_cells)
    .def_readwrite("create_line_cells", &pdflib::decode_page_config::create_line_cells)
    .def_readwrite("enforce_same_font", &pdflib::decode_page_config::enforce_same_font)
    .def_readwrite("horizontal_cell_tolerance", &pdflib::decode_page_config::horizontal_cell_tolerance)
    .def_readwrite("word_space_width_factor_for_merge", &pdflib::decode_page_config::word_space_width_factor_for_merge)
    .def_readwrite("line_space_width_factor_for_merge", &pdflib::decode_page_config::line_space_width_factor_for_merge)
    .def_readwrite("line_space_width_factor_for_merge_with_space", &pdflib::decode_page_config::line_space_width_factor_for_merge_with_space);

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
	 "Recompute line cells from char cells with the given config");

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
	 [](
        docling::docling_parser &self,
        const std::string &key,
        const std::string &filename,
        std::optional<std::string>& password
     ) -> bool {
	   return self.load_document(key, filename, password);
	 },
	 pybind11::arg("key"),
	 pybind11::arg("filename"),
     pybind11::arg("password") = pybind11::none(),
	 R"(
    Load a document by key and filename.

    Parameters:
        key (str): The unique key to identify the document.
        filename (str): The path to the document file to load.
        password (str, optional): Optional password for password-protected files

    Returns:
        bool: True if the document was successfully loaded, False otherwise.)")
    
    .def("load_document_from_bytesio",
	 [](docling::docling_parser &self, const std::string &key, pybind11::object bytes_io) -> bool {
	   return self.load_document_from_bytesio(key, bytes_io);
	 },
	 pybind11::arg("key"),
	 pybind11::arg("bytes_io"),
	 R"(
    Load a document by key from a BytesIO-like object.

    Parameters:
        key (str): The unique key to identify the document.
        bytes_io (Any): A BytesIO-like object containing the document data.

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
	    const pdflib::decode_page_config &config) -> std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE>> {
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
}
