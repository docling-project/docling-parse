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
    typedef std::shared_ptr<pdf_decoder<PAGE>> page_decoder_ptr;

  public:

    pdf_decoder();
    pdf_decoder(pdf_timings& timings_);
    ~pdf_decoder();

    nlohmann::json get();

    int get_number_of_pages() { return number_of_pages; }

    nlohmann::json get_annotations() { return json_annots; }

    nlohmann::json get_meta_xml() { return json_annots["meta_xml"]; }
    nlohmann::json get_table_of_contents() { return json_annots["table_of_contents"]; }

    bool process_document_from_file(std::string& _filename,
				    std::optional<std::string>& password);

    bool process_document_from_bytesio(std::string& _buffer,
				       std::optional<std::string>& password,
				       std::string description = "processing buffer");

    void decode_document(const decode_page_config& config);

    void decode_document(std::vector<int>& page_numbers,
			 const decode_page_config& config);

    // New: Direct access to page decoders (typed API)
    bool has_page_decoder(int page_number);
    page_decoder_ptr get_page_decoder(int page_number);

    // Decode a single page and return the page decoder directly
    page_decoder_ptr decode_page(int page_number,
				 const decode_page_config& config);

    bool unload_pages();

    bool unload_page(int page_number);

  private:

    void update_qpdf_logger();

    void update_timings(pdf_timings& timings_, bool set_timer);

  private:

    std::string filename;
    std::string buffer; // keep a local copy, in order to not let it expire

    pdf_timings timings;

    QPDF qpdf_document;

    QPDFObjectHandle qpdf_root;
    QPDFObjectHandle qpdf_pages;

    int number_of_pages;

    nlohmann::json json_annots;
    nlohmann::json json_document;  // Keep for backward compatibility

    // New: Persistent page decoders for typed API
    std::map<int, page_decoder_ptr> page_decoders;
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
    json_document(nlohmann::json::value_t::null),
    page_decoders({})
  {
    update_qpdf_logger();
  }

  pdf_decoder<DOCUMENT>::pdf_decoder(pdf_timings& timings_):
    filename(""),
    buffer(""),

    timings(timings_),
    qpdf_document(),

    // have compatibulity between QPDF v10 and v11
    qpdf_root(),
    qpdf_pages(),

    number_of_pages(-1),

    json_annots(nlohmann::json::value_t::null),
    json_document(nlohmann::json::value_t::null),
    page_decoders({})
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

      // Serialize timings as sums for backward compatibility
      auto sum_map = timings.to_sum_map();
      for(auto itr=sum_map.begin(); itr!=sum_map.end(); itr++)
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

    timings.add_timing(pdf_timings::KEY_PROCESS_DOCUMENT_FROM_FILE, timer.get_time());

    return true;
  }

  bool pdf_decoder<DOCUMENT>::process_document_from_bytesio(std::string& _buffer,
							    std::optional<std::string>& password,
							    std::string description)
  {
    buffer = _buffer;    
    LOG_S(INFO) << "start processing buffer of size " << buffer.size() << " by qpdf ...";

    utils::timer timer;
    
    try
      {
	//std::string description = "processing buffer";
        if (password.has_value())
	  {	
	    qpdf_document.processMemoryFile(description.c_str(),
					    buffer.c_str(),
					    buffer.size(),
					    password.value().c_str());
	  }
	else
	  {
	    qpdf_document.processMemoryFile(description.c_str(),
					    buffer.c_str(),
					    buffer.size());
	  }	
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

    timings.add_timing(pdf_timings::KEY_PROCESS_DOCUMENT_FROM_BYTESIO, timer.get_time());

    return true;
  }

  void pdf_decoder<DOCUMENT>::decode_document(const decode_page_config& config)
  {
    LOG_S(INFO) << "start decoding all pages ...";
    utils::timer timer;

    nlohmann::json& json_pages = json_document["pages"];
    json_pages = nlohmann::json::array({});

    bool set_timer=true;

    int page_number=0;
    for(QPDFObjectHandle page : qpdf_document.getAllPages())
      {
	utils::timer page_timer;

	auto page_decoder = std::make_shared<pdf_decoder<PAGE>>(page, page_number);

        page_decoder->decode_page(config);
	update_timings(page_decoder->get_timings(), set_timer);
	set_timer = false;

        json_pages.push_back(page_decoder->get(config));

	page_decoders[page_number] = page_decoder;

	std::stringstream ss;
	ss << pdf_timings::PREFIX_DECODING_PAGE << page_number++;

	timings.add_timing(ss.str(), page_timer.get_time());
      }

    timings.add_timing(pdf_timings::KEY_DECODE_DOCUMENT, timer.get_time());
  }

  void pdf_decoder<DOCUMENT>::decode_document(std::vector<int>& page_numbers,
					      const decode_page_config& config)
  {
    LOG_S(INFO) << "start decoding selected pages:\n" << config.to_string();

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

	    auto page_decoder = std::make_shared<pdf_decoder<PAGE>>(pages.at(page_number), page_number);

	    {
	      page_decoder->decode_page(config);

	      update_timings(page_decoder->get_timings(), set_timer);
	      set_timer=false;
	    }

	    nlohmann::json page = page_decoder->get(config);

	    pdf_sanitator<PAGE_CELLS> sanitizer;
	    if(config.create_word_cells)
	      {
		LOG_S(INFO) << "creating word-cells in `original` (2)";

		pdf_resource<PAGE_CELLS> word_cells = sanitizer.create_word_cells(page_decoder->get_page_cells(),
										  config);

		// quadratic: might be slower ...
		sanitizer.remove_duplicate_cells(word_cells, 0.5, true);

		page["original"]["word_cells"] = word_cells.get();
	      }

	    if(config.create_line_cells)
	      {
		LOG_S(INFO) << "creating line-cells in `original` (2)";

		pdf_resource<PAGE_CELLS> line_cells = sanitizer.create_line_cells(page_decoder->get_page_cells(),
										  config);
		// quadratic: might be slower ...
		sanitizer.remove_duplicate_cells(line_cells, 0.5, true);

		page["original"]["line_cells"] = line_cells.get();
	      }

	    json_pages.push_back(page);

	    page_decoders[page_number] = page_decoder;

	    std::stringstream ss;
	    ss << pdf_timings::PREFIX_DECODING_PAGE << page_number;

	    timings.add_timing(ss.str(), page_timer.get_time());
	  }
	else
	  {
	    LOG_S(WARNING) << "page " << page_number << " is out of bounds ...";

	    nlohmann::json none;
	    json_pages.push_back(none);
	  }
      }

    timings.add_timing(pdf_timings::KEY_DECODE_DOCUMENT, timer.get_time());
  }

  void pdf_decoder<DOCUMENT>::update_timings(pdf_timings& timings_,
					     bool set_timer)
  {
    if(set_timer)
      {
	// Clear existing timings when starting a new batch
	timings.clear();
      }
    // Merge all timings from the page decoder
    timings.merge(timings_);
  }

  bool pdf_decoder<DOCUMENT>::has_page_decoder(int page_number)
  {
    return page_decoders.count(page_number) > 0;
  }

  pdf_decoder<DOCUMENT>::page_decoder_ptr pdf_decoder<DOCUMENT>::get_page_decoder(int page_number)
  {
    auto itr = page_decoders.find(page_number);
    if(itr != page_decoders.end())
      {
	return itr->second;
      }
    return nullptr;
  }

  pdf_decoder<DOCUMENT>::page_decoder_ptr pdf_decoder<DOCUMENT>::decode_page(
      int page_number,
      const decode_page_config& config)
  {
    LOG_S(INFO) << __FUNCTION__ << " for page: " << page_number;
    utils::timer timer;

    // Check bounds
    if(page_number < 0 || page_number >= number_of_pages)
      {
	LOG_S(ERROR) << "page " << page_number << " is out of bounds (0-" << number_of_pages-1 << ")";
	return nullptr;
      }

    // Return cached decoder if already decoded
    if(has_page_decoder(page_number))
      {
	LOG_S(INFO) << "returning cached page decoder for page: " << page_number;
	return page_decoders[page_number];
      }

    // Get the QPDF page
    std::vector<QPDFObjectHandle> pages = qpdf_document.getAllPages();
    QPDFObjectHandle qpdf_page = pages.at(page_number);

    // Create and decode the page
    auto page_decoder = std::make_shared<pdf_decoder<PAGE>>(qpdf_page, page_number);

    bool set_timer = (timings.empty());
    page_decoder->decode_page(config);
    update_timings(page_decoder->get_timings(), set_timer);

    // Create word and line cells if requested
    if(config.create_word_cells)
      {
	LOG_S(INFO) << "creating word-cells for page: " << page_number;
	page_decoder->create_word_cells(config);
      }

    if(config.create_line_cells)
      {
	LOG_S(INFO) << "creating line-cells for page: " << page_number;
	page_decoder->create_line_cells(config);
      }

    // Store in cache
    page_decoders[page_number] = page_decoder;

    std::stringstream ss;
    ss << pdf_timings::PREFIX_DECODE_PAGE << page_number;
    timings.add_timing(ss.str(), timer.get_time());

    return page_decoder;
  }

  bool pdf_decoder<DOCUMENT>::unload_page(int page_number)
  {
    // Clear from page_decoders cache
    if(page_decoders.count(page_number) > 0)
      {
	page_decoders.erase(page_number);
	LOG_S(INFO) << "unloaded page decoder for page: " << page_number;
      }

    // Also clear from json_document for backward compatibility
    if(json_document.contains("pages"))
      {
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
      }

    return true;
  }

  bool pdf_decoder<DOCUMENT>::unload_pages()
  {
    // Clear all page decoders
    page_decoders.clear();
    LOG_S(INFO) << "unloaded all page decoders";

    // Also clear json_document for backward compatibility
    if(json_document.contains("pages"))
      {
	json_document["pages"] = nlohmann::json::array({});
      }

    return true;
  }
    
}

#endif
