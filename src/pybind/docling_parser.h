//-*-C++-*-

#ifndef PYBIND_PDF_PARSER_H
#define PYBIND_PDF_PARSER_H

#include <optional>
#ifdef _WIN32
#include <locale>
#include <codecvt>
#endif

#include <pybind/docling_resources.h>

#include <parse.h>

namespace docling
{
  class docling_parser: public docling_resources
  {
    typedef pdflib::pdf_decoder<pdflib::DOCUMENT> decoder_type;
    typedef std::shared_ptr<decoder_type> decoder_ptr_type;
    
  public:

    docling_parser();

    docling_parser(std::string level);

    void set_loglevel(int level=0);
    void set_loglevel_with_label(std::string level="error");

    bool is_loaded(std::string key);
    std::vector<std::string> list_loaded_keys();

    bool load_document(std::string key, std::string filename, std::optional<std::string> password);
    bool load_document_from_bytesio(std::string key, pybind11::object bytes_io);

    bool unload_document(std::string key);
    bool unload_document_pages(std::string key);
    bool unload_document_page(std::string key, int page_num);

    void unload_documents();

    int number_of_pages(std::string key);

    nlohmann::json get_annotations(std::string key);

    nlohmann::json get_meta_xml(std::string key);
    nlohmann::json get_table_of_contents(std::string key);
    
    nlohmann::json parse_pdf_from_key(std::string key,
				      std::string page_boundary,
				      bool do_sanitization);

    nlohmann::json parse_pdf_from_key_on_page(std::string key,
					      int page,
					      std::string page_boundary,
					      bool do_sanitization,
					      bool keep_char_cells,
					      bool keep_lines,
					      bool keep_bitmaps,
					      bool create_word_cells,
					      bool create_line_cells);

    nlohmann::json sanitize_cells(nlohmann::json& original_cells,
				  nlohmann::json& page_dim,
				  nlohmann::json& page_lines,
				  double horizontal_cell_tolerance,
				  bool enforce_same_font,
				  double space_width_factor_for_merge, //=1.5,
				  double space_width_factor_for_merge_with_space); //=0.33);
    
    nlohmann::json sanitize_cells_in_bbox(nlohmann::json& page,
					  std::array<double, 4> bbox,
					  double cell_overlap,
					  double horizontal_cell_tolerance,
					  bool enforce_same_font,
					  double space_width_factor_for_merge, //=1.5,
					  double space_width_factor_for_merge_with_space); //=0.33);

  private:

    bool verify_page_boundary(std::string page_boundary);
    
  private:

    std::string pdf_resources_dir;

    std::map<std::string, decoder_ptr_type> key2doc;
  };

  docling_parser::docling_parser():
    docling_resources(),
    pdf_resources_dir(resource_utils::get_resources_dir(true).string()),
    key2doc({})
  {
    LOG_S(WARNING) << "pdf_resources_dir: " << pdf_resources_dir;

    auto RESOURCE_DIR_KEY = pdflib::pdf_resource<pdflib::PAGE_FONT>::RESOURCE_DIR_KEY;
    
    nlohmann::json data = nlohmann::json::object({});
    data[RESOURCE_DIR_KEY] = pdf_resources_dir;

    std::map<std::string, double> timings = {};
    pdflib::pdf_resource<pdflib::PAGE_FONT>::initialise(data, timings);
  }

  docling_parser::docling_parser(std::string level):
    docling_resources(),
    pdf_resources_dir(resource_utils::get_resources_dir(true).string()),
    key2doc({})
  {
    set_loglevel_with_label(level);    

    LOG_S(WARNING) << "pdf_resources_dir: " << pdf_resources_dir;

    auto RESOURCE_DIR_KEY = pdflib::pdf_resource<pdflib::PAGE_FONT>::RESOURCE_DIR_KEY;
    
    nlohmann::json data = nlohmann::json::object({});
    data[RESOURCE_DIR_KEY] = pdf_resources_dir;

    std::map<std::string, double> timings = {};
    pdflib::pdf_resource<pdflib::PAGE_FONT>::initialise(data, timings);
  }
  
  void docling_parser::set_loglevel(int level)
  {
    if(level>=3)
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
      }
    else if(level==2)
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
      }
    else if(level==1)
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
      }
    else if(level==0)
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_FATAL;
      }
    else
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
      }
  }

  void docling_parser::set_loglevel_with_label(std::string level)
  {
    if(level=="info")
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
      }
    else if(level=="warning" or level=="warn")
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
      }
    else if(level=="error")
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
      }
    else if(level=="fatal")
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_FATAL;
      }
    else
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
      }
  }

  std::vector<std::string> docling_parser::list_loaded_keys()
  {
    std::vector<std::string> keys={};

    // Add the key (which is the first element of the pair)
    for (const auto& pair : key2doc)
      {
        keys.push_back(pair.first);
      }

    return keys;
  }

  bool docling_parser::is_loaded(std::string key)
  {
    return (key2doc.count(key)==1);
  }

  bool docling_parser::load_document(std::string key, std::string filename, std::optional<std::string> password)
  {
#ifdef _WIN32
    // Convert UTF-8 string to UTF-16 wstring
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    std::wstring wide_filename = converter.from_bytes(filename);
    std::filesystem::path path_filename(wide_filename);
#else
    std::filesystem::path path_filename(filename);
#endif

    if (std::filesystem::exists(path_filename))
      {
        //key2doc[key] = std::filesystem::path(filename);
	key2doc[key] = std::make_shared<decoder_type>();
	key2doc.at(key)->process_document_from_file(filename, password);
	return true;
      }

    LOG_S(ERROR) << "File not found: " << filename;
    return false;
  }

  bool docling_parser::load_document_from_bytesio(std::string key, pybind11::object bytes_io)
  {
    // logging_lib::info("pdf-parser") << __FILE__ << ":" << __LINE__ << "\t" << __FUNCTION__;
    LOG_S(INFO) << __FILE__ << ":" << __LINE__ << "\t" << __FUNCTION__;
      
    // Check if the object is a BytesIO object
    if (!pybind11::hasattr(bytes_io, "read")) {

      throw std::runtime_error("Expected a BytesIO object");
    }

    // Seek to the beginning of the BytesIO stream
    bytes_io.attr("seek")(0);

    // Read the entire content of the BytesIO stream
    pybind11::bytes data = bytes_io.attr("read")();

    // Get a pointer to the data
    std::string data_str = data.cast<std::string>();

    try
      {
	key2doc[key] = std::make_shared<decoder_type>();
	key2doc.at(key)->process_document_from_bytesio(data_str);

	return true;
      }
    catch(const std::exception& exc)
      {
	LOG_S(ERROR) << "could not docode bytesio object for key="<<key;
	return false;
      }

    return false;
  }

  bool docling_parser::unload_document(std::string key)
  {
    if(key2doc.count(key)==1)
      {
	key2doc.erase(key);
	return true;
      }
    else
      {
	LOG_S(ERROR) << "key not found: " << key;
      }
    
    return false;    
  }

  bool docling_parser::unload_document_page(std::string key, int page_num)
  {
    auto itr = key2doc.find(key);

    if(itr!=key2doc.end())
      {
	decoder_ptr_type decoder_ptr = itr->second;
	decoder_ptr->unload_page(page_num);
      }
    else
      {
	LOG_S(ERROR) << "key not found: " << key;	
      }
    
    return false;    
  }

  bool docling_parser::unload_document_pages(std::string key)
  {
    auto itr = key2doc.find(key);

    if(itr!=key2doc.end())
      {
	decoder_ptr_type decoder_ptr = itr->second;
	decoder_ptr->unload_pages();
      }
    else
      {
	LOG_S(ERROR) << "key not found: " << key;	
      }
    
    return false;    
  }
  
  void docling_parser::unload_documents()
  {
    key2doc.clear();
  }

  int docling_parser::number_of_pages(std::string key)
  {
    auto itr = key2doc.find(key);

    if(itr!=key2doc.end())
      {
	return (itr->second)->get_number_of_pages();
      }
    else
      {
	LOG_S(ERROR) << "key not found: " << key;	
      }

    return -1;
  }

  nlohmann::json docling_parser::get_annotations(std::string key)
  {
    LOG_S(INFO) << __FUNCTION__;

    auto itr = key2doc.find(key);

    if(itr==key2doc.end())
      {
	LOG_S(ERROR) << "key not found: " << key;
	return nlohmann::json::value_t::null;	
      }

    return (itr->second)->get_annotations();
  }

  nlohmann::json docling_parser::get_meta_xml(std::string key)
  {
    LOG_S(INFO) << __FUNCTION__;

    auto itr = key2doc.find(key);

    if(itr==key2doc.end())
      {
	LOG_S(ERROR) << "key not found: " << key;
	return nlohmann::json::value_t::null;	
      }

    return (itr->second)->get_meta_xml();
  }
  
  nlohmann::json docling_parser::get_table_of_contents(std::string key)
  {
    LOG_S(INFO) << __FUNCTION__;

    auto itr = key2doc.find(key);

    if(itr==key2doc.end())
      {
	LOG_S(ERROR) << "key not found: " << key;
	return nlohmann::json::value_t::null;	
      }

    return (itr->second)->get_table_of_contents();
  }

  nlohmann::json docling_parser::parse_pdf_from_key(std::string key,
						       std::string page_boundary,
						       bool do_sanitization)
  {
    LOG_S(INFO) << __FUNCTION__;
    
    auto itr = key2doc.find(key);
    if(itr==key2doc.end())
      {
	LOG_S(ERROR) << "key not found: " << key;
	return nlohmann::json::value_t::null;	
      }
    
    auto& decoder = itr->second;
    decoder->decode_document(page_boundary, do_sanitization);

    LOG_S(INFO) << "decoding done for key: " << key;

    //{
    //auto result = decoder->get();
    //LOG_S(ERROR) << result.dump(2);
    //}
    
    return decoder->get();
  }

  nlohmann::json docling_parser::parse_pdf_from_key_on_page(std::string key,
							       int page,
							       std::string page_boundary,
							       bool do_sanitization,
							       bool keep_char_cells,
							       bool keep_lines,
							       bool keep_bitmaps,
							       bool create_word_cells,
							       bool create_line_cells)
  {
    LOG_S(INFO) << __FUNCTION__;
    
    auto itr = key2doc.find(key);
    if(itr==key2doc.end())
      {
	LOG_S(ERROR) << "key not found: " << key << " " << key2doc.count(key);
	return nlohmann::json::value_t::null;	
      }

    auto& decoder = itr->second;
    
    std::vector<int> pages = {page};
    decoder->decode_document(pages,
			     page_boundary,
			     do_sanitization,
			     keep_char_cells,
			     keep_lines,
			     keep_bitmaps,
			     create_word_cells,
			     create_line_cells);

    LOG_S(INFO) << "decoding done for for key: " << key << " and page: " << page;

    //{
    //auto result = decoder->get();
    //LOG_S(ERROR) << "`" << result.dump(2) << "`";
    //}
    
    return decoder->get();
  }

  nlohmann::json docling_parser::sanitize_cells(nlohmann::json& json_cells,
						   nlohmann::json& json_dim,
						   nlohmann::json& json_lines,
						   double horizontal_cell_tolerance,
						   bool enforce_same_font,
						   double space_width_factor_for_merge, //=1.5,
						   double space_width_factor_for_merge_with_space) //=0.33);
  {
    pdflib::pdf_resource<pdflib::PAGE_DIMENSION> dim;
    dim.init_from(json_dim);
    
    pdflib::pdf_resource<pdflib::PAGE_LINES> lines;
    lines.init_from(json_lines);
    
    pdflib::pdf_resource<pdflib::PAGE_CELLS> cells;
    cells.init_from(json_cells);
    
    pdflib::pdf_sanitator<pdflib::PAGE_CELLS> sanitizer;//(dim, lines);
    sanitizer.sanitize_bbox(cells, horizontal_cell_tolerance, enforce_same_font,
			    space_width_factor_for_merge,
			    space_width_factor_for_merge_with_space);

    sanitizer.sanitize_text(cells);
    
    return cells.get();
  }

  nlohmann::json docling_parser::sanitize_cells_in_bbox(nlohmann::json& page,
							   std::array<double, 4> bbox,
							   double cell_overlap,
							   double horizontal_cell_tolerance,
							   bool enforce_same_font,
							   double space_width_factor_for_merge, //=1.5,
							   double space_width_factor_for_merge_with_space) //=0.33);
  {
    LOG_S(INFO) << __FUNCTION__
		<< ", cell_overlap: " << cell_overlap
		<< ", horizontal_cell_tolerance: " << horizontal_cell_tolerance
		<< ", enforce_same_font: " << enforce_same_font;
    
    // empty array
    nlohmann::json sanitized_cells = nlohmann::json::array({});
    
    double x0 = bbox[0];
    double y0 = bbox[1];
    
    double x1 = bbox[2];
    double y1 = bbox[3];

    pdflib::pdf_resource<pdflib::PAGE_DIMENSION> dim;
    if(not dim.init_from(page["original"]["dimension"]))
      {
	LOG_S(WARNING) << "could not init dim";
	return sanitized_cells;
      }
    
    pdflib::pdf_resource<pdflib::PAGE_LINES> lines;
    if(not lines.init_from(page["original"]["lines"]))
      {
	LOG_S(WARNING) << "could not init lines";
	return sanitized_cells;
      }
    
    pdflib::pdf_resource<pdflib::PAGE_CELLS> cells;
    if(not cells.init_from(page["original"]["cells"]["data"]))
      {
	LOG_S(WARNING) << "could not init cells";
	return sanitized_cells;
      }

    LOG_S(INFO) << "init done ... --> #-cells: " << cells.size();
    
    // get all cells with an overlap over cell_overlap
    pdflib::pdf_resource<pdflib::PAGE_CELLS> selected_cells;    
    for(int i=0; i<cells.size(); i++)
      {
	double overlap = utils::values::compute_overlap(cells[i].x0, cells[i].y0, cells[i].x1, cells[i].y1,
							x0, y0, x1, y1);

	if(overlap>cell_overlap-1.e-3)
	  {
	    selected_cells.push_back(cells[i]);
	  }
      }

    if(selected_cells.size()==0)
      {
	return sanitized_cells;
      }
    
    pdflib::pdf_sanitator<pdflib::PAGE_CELLS> sanitizer;
    sanitizer.sanitize_bbox(selected_cells,
			    horizontal_cell_tolerance,
			    enforce_same_font,
			    space_width_factor_for_merge,
			    space_width_factor_for_merge_with_space);

    sanitizer.sanitize_text(selected_cells);
    
    return selected_cells.get();
  }

  
  
}

#endif
