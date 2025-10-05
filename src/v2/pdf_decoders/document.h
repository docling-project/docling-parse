//-*-C++-*-

#ifndef PDF_DOCUMENT_DECODER_H
#define PDF_DOCUMENT_DECODER_H

#include <optional>
#include <qpdf/QPDF.hh>
//#include <qpdf/QPDFPageObjectHelper.hh>

namespace pdflib
{

  template<>
  class pdf_decoder<DOCUMENT>
  {
  public:

    pdf_decoder();
    pdf_decoder(std::map<std::string, double>& timings_);
    ~pdf_decoder();

    nlohmann::json get();

    int get_number_of_pages() { return number_of_pages; }

    nlohmann::json get_annotations() { return json_annots; }

    nlohmann::json get_meta_xml() { return json_annots["meta_xml"]; }
    nlohmann::json get_table_of_contents() { return json_annots["table_of_contents"]; }
    
    bool process_document_from_file(std::string& _filename, std::optional<std::string>& password);
    bool process_document_from_bytesio(std::string& _buffer);
    
    void decode_document(std::string page_boundary, bool do_sanitization);

    void decode_document(std::vector<int>& page_numbers,
			 std::string page_boundary,
			 bool do_sanitization,
			 bool keep_char_cells,
			 bool keep_lines,
			 bool keep_bitmaps,
			 bool create_word_cells,
			 bool create_line_cells);

    bool unload_pages();

    bool unload_page(int page_number);
    
  private:

    void update_qpdf_logger();
    
    void update_timings(std::map<std::string, double>& timings_, bool set_timer);

  private:

    std::string filename;
    std::string buffer; // keep a local copy, in order to not let it expire
    
    std::map<std::string, double> timings;

    QPDF qpdf_document;

    QPDFObjectHandle qpdf_root;
    QPDFObjectHandle qpdf_pages;

    int number_of_pages;    

    //nlohmann::json json_toc; // table-of-contents
    nlohmann::json json_annots;
    nlohmann::json json_document;
  };

  pdf_decoder<DOCUMENT>::pdf_decoder():
    filename(""),
    buffer(""),
    
    timings({}),
    qpdf_document(),
    
    // have compatibulity between QPDF v10 and v11
    qpdf_root(),
    qpdf_pages(),
    
    number_of_pages(-1),

    json_annots(nlohmann::json::value_t::null),
    json_document(nlohmann::json::value_t::null)
  {
    update_qpdf_logger();
  }
  
  pdf_decoder<DOCUMENT>::pdf_decoder(std::map<std::string, double>& timings_):
    filename(""),
    buffer(""),
    
    timings(timings_),
    qpdf_document(),

    // have compatibulity between QPDF v10 and v11
    qpdf_root(),
    qpdf_pages(),
    
    number_of_pages(-1),

    json_annots(nlohmann::json::value_t::null),
    json_document(nlohmann::json::value_t::null)
  {
    update_qpdf_logger();
  }

  pdf_decoder<DOCUMENT>::~pdf_decoder()
  {}

  void pdf_decoder<DOCUMENT>::update_qpdf_logger()
  {
    if(loguru::g_stderr_verbosity==loguru::Verbosity_INFO or
       loguru::g_stderr_verbosity==loguru::Verbosity_WARNING)
      {
	// ignore ...	
      }
    else if(loguru::g_stderr_verbosity==loguru::Verbosity_ERROR or
	    loguru::g_stderr_verbosity==loguru::Verbosity_FATAL)
      {
	qpdf_document.setSuppressWarnings(true);
	//qpdf_document.setMaxWarnings(0); only for later versions ...
      }
    else
      {

      }
  }
  
  nlohmann::json pdf_decoder<DOCUMENT>::get()
  {
    LOG_S(INFO) << "get() [in pdf_decoder<DOCUMENT>]";
    
    {
      json_document["annotations"] = json_annots;
    }
    
    {
      nlohmann::json& timings_ = json_document["timings"];

      for(auto itr=timings.begin(); itr!=timings.end(); itr++)
	{
	  timings_[itr->first] = itr->second;
	}
    }

    return json_document;
  }

  bool pdf_decoder<DOCUMENT>::process_document_from_file(std::string& _filename, std::optional<std::string>& password)
  {
    filename = _filename; // save it    
    LOG_S(INFO) << "start processing '" << filename << "' by qpdf ...";        

    utils::timer timer;
    
    try
      {
        if (password.has_value()) {
          qpdf_document.processFile(filename.c_str(), password.value().c_str());
        } else {
          qpdf_document.processFile(filename.c_str());
        }
        LOG_S(INFO) << "filename: " << filename << " processed by qpdf!";        

        qpdf_root  = qpdf_document.getRoot();
        qpdf_pages = qpdf_root.getKey("/Pages");

	json_annots = extract_document_annotations_in_json(qpdf_document, qpdf_root);
	
        number_of_pages = qpdf_pages.getKey("/Count").getIntValue();    
        LOG_S(INFO) << "#-pages: " << number_of_pages;

	nlohmann::json& info = json_document["info"];
	{
	  info["filename"] = filename;
	  info["#-pages"] = number_of_pages;
	}
      }
    catch(const std::exception& exc)
      {
        LOG_S(ERROR) << "filename: " << filename << " can not be processed by qpdf: " << exc.what();        
        return false;
      }

    timings[__FUNCTION__] = timer.get_time();

    return true;
  }
  
  bool pdf_decoder<DOCUMENT>::process_document_from_bytesio(std::string& _buffer)
  {
    buffer = _buffer;    
    LOG_S(INFO) << "start processing buffer of size " << buffer.size() << " by qpdf ...";

    utils::timer timer;
    
    try
      {
	std::string description = "processing buffer";	
        qpdf_document.processMemoryFile(description.c_str(),
					buffer.c_str(), buffer.size());

        LOG_S(INFO) << "buffer processed by qpdf!";        

        qpdf_root  = qpdf_document.getRoot();
        qpdf_pages = qpdf_root.getKey("/Pages");

	json_annots = extract_document_annotations_in_json(qpdf_document, qpdf_root);
	
        number_of_pages = qpdf_pages.getKey("/Count").getIntValue();    
        LOG_S(INFO) << "#-pages: " << number_of_pages;

	nlohmann::json& info = json_document["info"];
	{
	  info["filename"] = filename;
	  info["#-pages"] = number_of_pages;
	}
      }
    catch(const std::exception & exc)
      {
        LOG_S(ERROR) << "filename: " << filename << " can not be processed by qpdf: " << exc.what();        
        return false;
      }

    timings[__FUNCTION__] = timer.get_time();

    return true;
  }
  
  void pdf_decoder<DOCUMENT>::decode_document(std::string page_boundary,
					      bool do_sanitization)
  {
    LOG_S(INFO) << "start decoding all pages ...";        
    utils::timer timer;

    bool keep_char_cells = true;
    bool keep_lines = true; 
    bool keep_bitmaps = true;
    
    nlohmann::json& json_pages = json_document["pages"];
    json_pages = nlohmann::json::array({});
    
    bool set_timer=true;
    
    int page_number=0;
    for(QPDFObjectHandle page : qpdf_document.getAllPages())
      {
	utils::timer page_timer;
	
        pdf_decoder<PAGE> page_decoder(page, page_number);

        auto timings_ = page_decoder.decode_page(page_boundary, do_sanitization);
	update_timings(timings_, set_timer);
	set_timer = false;

        json_pages.push_back(page_decoder.get(keep_char_cells, keep_lines, keep_bitmaps, do_sanitization));

	std::stringstream ss;
	ss << "decoding page " << page_number++;

	timings[ss.str()] = page_timer.get_time();
      }

    timings[__FUNCTION__] = timer.get_time();
  }

  void pdf_decoder<DOCUMENT>::decode_document(std::vector<int>& page_numbers,
					      std::string page_boundary,
					      bool do_sanitization,
					      bool keep_char_cells,
					      bool keep_lines,
					      bool keep_bitmaps,
					      bool create_word_cells,
					      bool create_line_cells)
  {
    LOG_S(INFO) << "start decoding selected pages ("
		<< "keep_char_cells: " << keep_char_cells << ", "
		<< "keep_lines: " << keep_lines << ", "
		<< "keep_bitmaps: " << keep_bitmaps << ", "
		<< "create_word_cells: " << create_word_cells << ", "
      		<< "create_line_cells: " << create_line_cells << ")";  
						   
    utils::timer timer;

    // make sure that we only return the page from the page-numbers
    nlohmann::json& json_pages = json_document["pages"];
    json_pages = nlohmann::json::array({});
      
    std::vector<QPDFObjectHandle> pages = qpdf_document.getAllPages();

    bool set_timer=true; // make sure we override all timings for this page-set
    for(auto page_number:page_numbers)
      {
	utils::timer timer;

	if(0<=page_number and page_number<pages.size())
	  {
	    utils::timer page_timer;
	    
	    pdf_decoder<PAGE> page_decoder(pages.at(page_number), page_number);

	    {
	      //utils::timer decode_timer;	      
	      auto timings_ = page_decoder.decode_page(page_boundary, do_sanitization);

	      //std::cout << "decode_timer: " << decode_timer.get_time() << "\n";
	      
	      update_timings(timings_, set_timer);
	      set_timer=false;
	    }

	    nlohmann::json page = page_decoder.get(keep_char_cells, keep_lines, keep_bitmaps, do_sanitization);

	    pdf_sanitator<PAGE_CELLS> sanitizer;
	    if(create_word_cells)
	      {
		LOG_S(INFO) << "creating word-cells in `original` (2)";        

		double horizontal_cell_tolerance=1.00;
		bool enforce_same_font=true;
		double space_width_factor_for_merge=0.33;
		
		pdf_resource<PAGE_CELLS> word_cells = sanitizer.create_word_cells(page_decoder.get_page_cells(),
										  horizontal_cell_tolerance,
										  enforce_same_font,
										  space_width_factor_for_merge);

		// quadratic: might be slower ...
		sanitizer.remove_duplicate_cells(word_cells, 0.5, true);
		
		page["original"]["word_cells"] = word_cells.get();
	      }

	    if(create_line_cells)
	      {
		//utils::timer line_cells_timer;
		
		LOG_S(INFO) << "creating line-cells in `original` (2)";        

		double horizontal_cell_tolerance=1.00;
		bool enforce_same_font=true;
		double space_width_factor_for_merge=1.00;
		double space_width_factor_for_merge_with_space=0.33;
		
		pdf_resource<PAGE_CELLS> line_cells = sanitizer.create_line_cells(page_decoder.get_page_cells(),
										  horizontal_cell_tolerance,
										  enforce_same_font,
										  space_width_factor_for_merge,
										  space_width_factor_for_merge_with_space);
		// quadratic: might be slower ...
		sanitizer.remove_duplicate_cells(line_cells, 0.5, true);
		
		page["original"]["line_cells"] = line_cells.get();
		//std::cout << "line_cells: " << line_cells_timer.get_time() << "\n";
	      }	    
	    
	    json_pages.push_back(page);

	    std::stringstream ss;
	    ss << "decoding page " << page_number;
	    
	    timings[ss.str()] = page_timer.get_time();	    
	  }
	else
	  {
	    LOG_S(WARNING) << "page " << page_number << " is out of bounds ...";        
	    
	    nlohmann::json none;
	    json_pages.push_back(none);
	  }
      }

    timings[__FUNCTION__] = timer.get_time();
  }

  void pdf_decoder<DOCUMENT>::update_timings(std::map<std::string, double>& timings_,
					     bool set_timer)
  {
    for(auto itr=timings_.begin(); itr!=timings_.end(); itr++)
      {
	if(timings.count(itr->first)==0 or set_timer)
	  {
	    timings[itr->first] = itr->second;
	  }
	else
	  {
	    timings[itr->first] += itr->second;
	  }
      }    
  }

  bool pdf_decoder<DOCUMENT>::unload_page(int page_number)
  {
    if(not json_document.contains("pages"))
      {
	LOG_S(WARNING) << "json_document does not have `pages`";        
	return false;
      }

    nlohmann::json& json_pages = json_document["pages"];
    
    for(int l=0; l<json_pages.size(); l++)
      {
	if((json_pages[l].is_object()) and
	   (json_pages[l].contains("page_number")) and 
	   (json_pages[l]["page_number"]==page_number))
	  {
	    json_pages[l].clear();

	    nlohmann::json none;
	    json_pages[l] = none;
	  }
      }

    return true;
  }

  bool pdf_decoder<DOCUMENT>::unload_pages()
  {
    if(not json_document.contains("pages"))
      {
	LOG_S(WARNING) << "json_document does not have `pages`";        
	return false;
      }

    json_document["pages"] = nlohmann::json::array({});

    return true;
  }
    
}

#endif
