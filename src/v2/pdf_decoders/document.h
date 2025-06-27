//-*-C++-*-

#ifndef PDF_DOCUMENT_DECODER_H
#define PDF_DOCUMENT_DECODER_H

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

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
    
    bool process_document_from_file(std::string& _filename);
    bool process_document_from_bytesio(std::string& _buffer);

    void decode_document(std::string page_boundary, bool do_sanitization);

    void decode_document(std::vector<int>& page_numbers, std::string page_boundary, bool do_sanitization);

  private:

    void initialise_individual_pages();
    
    void update_qpdf_logger();
    
    void update_timings(std::map<std::string, double>& timings_, bool set_timer);
    
  private:

    std::mutex mtx;
    
    std::string filename;
    std::string buffer; // keep a local copy, in order to not let it expire
    
    std::map<std::string, double> timings;

    QPDF qpdf_document;

    QPDFObjectHandle qpdf_root;
    QPDFObjectHandle qpdf_pages;
    
    std::map<int, QPDF> individual_pages;
    
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
    individual_pages(),
    
    number_of_pages(-1),

    json_annots(nlohmann::json::value_t::null),
    json_document(nlohmann::json::value_t::null)
  {
    LOG_S(INFO) << "pdf_decoder<DOCUMENT> constuctor";
    
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
    LOG_S(INFO) << "pdf_decoder<DOCUMENT> constuctor";
    
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
    LOG_S(INFO) << "get() in pdf_decoder<DOCUMENT>";
    
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

  bool pdf_decoder<DOCUMENT>::process_document_from_file(std::string& _filename)
  {
    filename = _filename; // save it    
    LOG_S(INFO) << "start processing '" << filename << "' by qpdf ...";        

    utils::timer timer;
    
    try
      {
	std::lock_guard<std::mutex> lock(mtx);
	
        qpdf_document.processFile(filename.c_str());
        LOG_S(INFO) << "filename: " << filename << " processed by qpdf!";        

        qpdf_root  = qpdf_document.getRoot();
        qpdf_pages = qpdf_root.getKey("/Pages");

	json_annots = extract_document_annotations_in_json(qpdf_document, qpdf_root);
	
        number_of_pages = qpdf_pages.getKey("/Count").getIntValue();    
        LOG_S(INFO) << "#-pages: " << number_of_pages;

	initialise_individual_pages();
	
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

	initialise_individual_pages();
	
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

  // got inspiration from https://github.com/qpdf/qpdf/blob/main/examples/pdf-split-pages.cc
  void pdf_decoder<DOCUMENT>::initialise_individual_pages()
  {
    LOG_S(INFO) << "initialise individual pages";
    std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf_document).getAllPages();

    for(int page_no=0; page_no<pages.size(); page_no++)
      {	
        QPDF& pdf_page = individual_pages[page_no];
        pdf_page.emptyPDF();
	
        QPDFPageDocumentHelper(pdf_page).addPage(pages.at(page_no), false);
	LOG_S(INFO) << " -> page: " << page_no << " is initialised!";
      }
  }
  
  void pdf_decoder<DOCUMENT>::decode_document(std::string page_boundary,
					      bool do_sanitization)
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
	
        pdf_decoder<PAGE> page_decoder(page);

        auto timings_ = page_decoder.decode_page(page_boundary, do_sanitization);
	update_timings(timings_, set_timer);
	set_timer = false;

        json_pages.push_back(page_decoder.get());

	std::stringstream ss;
	ss << "decoding page " << page_number++;

	timings[ss.str()] = page_timer.get_time();
      }

    timings[__FUNCTION__] = timer.get_time();
  }
  
  void pdf_decoder<DOCUMENT>::decode_document(std::vector<int>& page_numbers,
					      std::string page_boundary,
					      bool do_sanitization)
  {
    LOG_S(INFO) << "start decoding selected pages ...";        
    utils::timer timer;

    // make sure that we only return the page from the page-numbers
    nlohmann::json& json_pages = json_document["pages"];
    json_pages = nlohmann::json::array({});
    for(int l=0; l<page_numbers.size(); l++)
      {
	json_pages.push_back(nlohmann::json::value_t::null);
      }
    
    //std::vector<QPDFObjectHandle> pages = qpdf_document.getAllPages();
    //std::vector<QPDFObjectHandle> pages = {};
    //{
    //std::lock_guard<std::mutex> lock(mtx);
    //pages = qpdf_document.getAllPages();
    //}
    
    bool set_timer=true; // make sure we override all timings for this page-set
    
    //for(auto page_number:page_numbers)
    for(int l=0; l<page_numbers.size(); l++)
      {
	int page_number = page_numbers.at(l);
	LOG_S(INFO) << "start parsing page: " << page_number;
	
	utils::timer timer;
	if(0<=page_number and page_number<number_of_pages)
	  {
	    utils::timer page_timer;
	    
	    //pdf_decoder<PAGE> page_decoder(pages.at(page_number));
	    
	    std::vector<QPDFObjectHandle> page_handles = individual_pages.at(page_number).getAllPages();
	    //QPDFObjectHandle page_handle = page_handels.at(0).
	    {
	      //std::lock_guard<std::mutex> lock(mtx);
	      
	    }
	    
	    //pdf_decoder<PAGE> page_decoder(pages.at(page_number));
	    pdf_decoder<PAGE> page_decoder(page_handles.at(0));
	    
	    auto timings_ = page_decoder.decode_page(page_boundary, do_sanitization);
	    
	    update_timings(timings_, set_timer);
	    set_timer=false;

	    {
	      std::lock_guard<std::mutex> lock(mtx);
	      json_pages[l] = page_decoder.get();

	      std::stringstream ss;
	      ss << "decoding page " << page_number;
	      
	      timings[ss.str()] = page_timer.get_time();
	    }
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

}

#endif
