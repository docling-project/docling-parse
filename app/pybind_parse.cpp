//-*-C++-*-

#include <optional>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/buffer_info.h>

#include <pybind/utils/pybind11_json.h>

#include <pybind/docling_parser.h>
#include <pybind/docling_sanitizer.h>

PYBIND11_MODULE(pdf_parsers, m) {

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
    
    .def("set_loglevel",
	 [](docling::docling_parser &self, int level) -> void {
	   self.set_loglevel(level);
	 },
	 pybind11::arg("level"),
	 R"(
    Set the log level using an integer.

    Parameters:
        level (int): Logging level as an integer.
                     One of [`fatal`=0, `error`=1, `warning`=2, `info`=3])")
    
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

    .def("unload_document_pages",
	 [](docling::docling_parser &self, const std::string &key) -> bool {
	   return self.unload_document_pages(key);
	 },
	 pybind11::arg("key"),
	 R"(
    Unload the only the cached pages of the document by its unique key.

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
    
    .def("parse_pdf_from_key",
	 [](docling::docling_parser &self,
	    const std::string &key,
	    const std::string &page_boundary,
	    bool do_sanitization
	    ) -> nlohmann::json {
	   return self.parse_pdf_from_key(key, page_boundary, do_sanitization);
	 },
	 pybind11::arg("key"),
	 pybind11::arg("page_boundary") = "crop_box", // media_box
	 pybind11::arg("do_sanitization") = true, // media_box
	 R"(
    Parse the PDF document identified by its unique key and return a JSON representation.

    Parameters:
        key (str): The unique key of the document.
        page_boundary (str): The page boundary specification for parsing. One of [`crop_box`, `media_box`].
        do_sanitization: Sanitize the chars into lines [default=true].

    Returns:
        dict: A JSON representation of the parsed PDF document.)")

    .def("parse_pdf_from_key_on_page",
	 [](docling::docling_parser &self,
	    const std::string &key,
	    int page,
	    const std::string &page_boundary,
	    bool do_sanitization,
	    bool keep_char_cells,
	    bool keep_lines,
	    bool keep_bitmaps,
	    bool create_word_cells,
	    bool create_line_cells) -> nlohmann::json {
    return self.parse_pdf_from_key_on_page(key,
					   page,
					   page_boundary,
					   do_sanitization,
					   keep_char_cells,
					   keep_lines,
					   keep_bitmaps,
					   create_word_cells,
					   create_line_cells);
	 },
    pybind11::arg("key"),
    pybind11::arg("page"),
    pybind11::arg("page_boundary") = "crop_box", // media_box
    pybind11::arg("do_sanitization") = true,
    pybind11::arg("keep_char_cells") = true,
    pybind11::arg("keep_lines") = true,
    pybind11::arg("keep_bitmaps") = true,
    pybind11::arg("create_word_cells") = true,
    pybind11::arg("create_line_cells") = true,
	 R"(
    Parse a specific page of the PDF document identified by its unique key and return a JSON representation.

    Parameters:
        key (str): The unique key of the document.
        page (int): The page number to parse.
        page_boundary (str): The page boundary specification for parsing [choices: crop_box, media_box].
        do_sanitization: Sanitize the chars into lines [default=true].
        keep_char_cells: keep all the individual char's
        keep_lines: keep all the lines
        keep_bitmaps: keep all the bitmap resources
        create_word_cells: create words from the char-cells
        create_line_cells: create lines from the char-cells

    Returns:
        dict: A JSON representation of the parsed page.)")

    .def("sanitize_cells",
	 [](docling::docling_parser &self,
	    nlohmann::json &original_cells,
	    nlohmann::json &page_dim,
	    nlohmann::json &page_lines,
	    double horizontal_cell_tolerance,
	    bool enforce_same_font,
	    double space_width_factor_for_merge,
	    double space_width_factor_for_merge_with_space) -> nlohmann::json {
	   return self.sanitize_cells(original_cells,
				      page_dim,
				      page_lines,
				      horizontal_cell_tolerance,
				      enforce_same_font,
				      space_width_factor_for_merge,
				      space_width_factor_for_merge_with_space
				      );
	 },	 
	 pybind11::arg("original_cells"),
	 pybind11::arg("page_dimension"),
	 pybind11::arg("page_lines"),
	 pybind11::arg("horizontal_cell_tolerance")=1.0,
	 pybind11::arg("enforce_same_font")=true,
	 pybind11::arg("space_width_factor_for_merge")=1.5,
	 pybind11::arg("space_width_factor_for_merge_with_space")=0.33,
	 R"(
Sanitize table cells with specified parameters and return the processed JSON.

    Parameters:
        original_cells (dict): The original table cells as a JSON object.
        page_dim (dict): Page dimensions as a JSON object.
        page_lines (dict): Page lines as a JSON object.
        horizontal_cell_tolerance (float): Vertical adjustment parameter to judge if two cells need to be merged (yes if abs(cell_i.r_y1-cell_i.r_y0)<horizontal_cell_tolerance), default = 1.0.
        enforce_same_font (bool): Whether to enforce the same font across cells. Default = True.
        space_width_factor_for_merge (float): Factor for merging cells based on space width. Default = 1.5.
        space_width_factor_for_merge_with_space (float): Factor for merging cells with space width. Default = 0.33.

    Returns:
        dict: A JSON object representing the sanitized table cells.)")

    .def("sanitize_cells_in_bbox",
	 [](docling::docling_parser &self,
	    nlohmann::json &page,
	    const std::array<double, 4> &bbox,
	    double cell_overlap,
	    double horizontal_cell_tolerance,
	    bool enforce_same_font,
	    double space_width_factor_for_merge = 1.5,
	    double space_width_factor_for_merge_with_space = 0.33) -> nlohmann::json {
	   return self.sanitize_cells_in_bbox(page,
					      bbox,
					      cell_overlap,
					      horizontal_cell_tolerance,
					      enforce_same_font,
					      space_width_factor_for_merge,
					      space_width_factor_for_merge_with_space
					      );
	 },
	 pybind11::arg("page"),
	 pybind11::arg("bbox"),
	 pybind11::arg("cell_overlap")=0.99,
	 pybind11::arg("horizontal_cell_tolerance")=1.0,
	 pybind11::arg("enforce_same_font")=true,
	 pybind11::arg("space_width_factor_for_merge")=1.5,
	 pybind11::arg("space_width_factor_for_merge_with_space")=0.33,
	 R"(
    Sanitize table cells in a given bounding box with specified parameters and return the processed JSON.

    Parameters:
        page (dict): The JSON object representing the page.
        bbox (Tuple[float, float, float, float]): Bounding box specified as [x_min, y_min, x_max, y_max].
        cell_overlap (float: 0.0-1.0): Overlap of cell (%) with bounding-box.
        horizontal_cell_tolerance (float): Vertical adjustment parameter to judge if two cells need to be merged (yes if abs(cell_i.r_y1-cell_i.r_y0)<horizontal_cell_tolerance), default = 1.0.
        enforce_same_font (bool): Whether to enforce the same font across cells. Default is True
        space_width_factor_for_merge (float): Factor for merging cells based on space width. Default is 1.5.
        space_width_factor_for_merge_with_space (float): Factor for merging cells with space width. Default is 0.33.

    Returns:
        dict: A JSON object representing the sanitized table cells in the bounding box.)");  


  // purely for backward compatibility 
  pybind11::class_<docling::docling_sanitizer>(m, "pdf_sanitizer")
    .def(pybind11::init())

    .def(pybind11::init<const std::string&>(),
	 pybind11::arg("level"),
	 R"(
    Construct docling_sanitizer with logging level.

    Parameters:
        level (str): Logging level as a string.
                     One of ['fatal', 'error', 'warning', 'info'])")
    
    .def("set_loglevel",
	 [](docling::docling_sanitizer &self, int level) -> void {
	   self.set_loglevel(level);
	 },
	 pybind11::arg("level"),
	 R"(
    Set the log level using an integer.

    Parameters:
        level (int): Logging level as an integer.
                     One of [`fatal`=0, `error`=1, `warning`=2, `info`=3])")
    
    .def("set_loglevel_with_label",
	 [](docling::docling_sanitizer &self, const std::string &level) -> void {
            self.set_loglevel_with_label(level);
	 },
	 pybind11::arg("level"),
	 R"(
    Set the log level using a string label.

    Parameters:
        level (str): Logging level as a string.
                     One of ['fatal', 'error', 'warning', 'info']
           )")

    .def("set_char_cells",
	 [](docling::docling_sanitizer &self,
	    nlohmann::json& data) -> nlohmann::json {
	   return self.set_char_cells(data);
	 },
	 pybind11::arg("data"),
	 R"(
    Set char cells

    Parameters:
        data: A JSON object (for with data and header) or a list or records

    Returns:
        dict: A JSON object representing the sanitized word cells in the bounding box.)")
    
    .def("create_word_cells",
	 [](docling::docling_sanitizer &self,
	    double horizontal_cell_tolerance,
	    bool enforce_same_font,
	    double space_width_factor_for_merge/* = 1.0*/) -> nlohmann::json {
	   return self.create_word_cells(horizontal_cell_tolerance,
					 enforce_same_font,
					 space_width_factor_for_merge);
	 },
	 pybind11::arg("horizontal_cell_tolerance"), // =1.0,
	 pybind11::arg("enforce_same_font"), //=true,
	 pybind11::arg("space_width_factor_for_merge"), //=0.33,
	 R"(
    Create word cells

    Parameters:
        horizontal_cell_tolerance (float): Vertical adjustment parameter to judge if two cells need to be merged (yes if abs(cell_i.r_y1-cell_i.r_y0)<horizontal_cell_tolerance), default = 1.0.
        enforce_same_font (bool): Whether to enforce the same font across cells. Default is True
        space_width_factor_for_merge (float): Factor for merging cells based on space width. Default is 0.33.

    Returns:
        dict: A JSON object representing the sanitized word cells in the bounding box.)")
    
    .def("create_line_cells",
	 [](docling::docling_sanitizer &self,
	    double horizontal_cell_tolerance,
	    bool enforce_same_font,
	    double space_width_factor_for_merge /*= 1.0*/,
	    double space_width_factor_for_merge_with_space /*= 0.33*/) -> nlohmann::json {
	   return self.create_line_cells(horizontal_cell_tolerance,
					 enforce_same_font,
					 space_width_factor_for_merge,
					 space_width_factor_for_merge_with_space);
	 },
	 pybind11::arg("horizontal_cell_tolerance"), //=1.0,
	 pybind11::arg("enforce_same_font"), //=true,
	 pybind11::arg("space_width_factor_for_merge"), //=1.0,
	 pybind11::arg("space_width_factor_for_merge_with_space"), //=0.33,
	 R"(
    Create line cells

    Parameters:
        horizontal_cell_tolerance (float): Vertical adjustment parameter to judge if two cells need to be merged (yes if abs(cell_i.r_y1-cell_i.r_y0)<horizontal_cell_tolerance), default = 1.0.
        enforce_same_font (bool): Whether to enforce the same font across cells. Default is True
        space_width_factor_for_merge (float): Factor for merging cells based on space width. Default is 1.0.
        space_width_factor_for_merge_with_space (float): Factor for merging cells with space width. Default is 0.33.

    Returns:
        dict: A JSON object representing the sanitized line cells in the bounding box.)");  
    
    
}
