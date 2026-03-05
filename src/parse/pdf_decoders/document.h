//-*-C++-*-

#ifndef PDF_DOCUMENT_DECODER_H
#define PDF_DOCUMENT_DECODER_H

#include <fstream>
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

    int get_number_of_pages() { return number_of_pages; }

    std::string get_filename() { return filename; }

    nlohmann::json get_annotations();

    nlohmann::json get_meta_xml();
    nlohmann::json get_table_of_contents();

    bool process_document_from_file(std::string& _filename,
                                    std::optional<std::string>& _password);

    bool process_document_from_bytesio(std::shared_ptr<std::string> _buffer,
                                       std::optional<std::string>& _password,
                                       std::string description = "processing buffer");

    void decode_document(const decode_config& config);

    void decode_document(std::vector<int>& page_numbers,
                         const decode_config& config);

    // Decode a single page and return the page decoder directly
    page_decoder_ptr decode_page(int page_number,
                                 const decode_config& config);
    
    // New: Direct access to page decoders (typed API)
    bool has_page_decoder(int page_number);
    page_decoder_ptr get_page_decoder(int page_number);

    bool unload_pages();

    bool unload_page(int page_number);

    pdf_timings& get_timings() { return timings; }

    std::shared_ptr<std::string> get_buffer() { return buffer; }
    std::optional<std::string> get_password() { return password; }

  private:

    void update_qpdf_logger();

    void ensure_annots_loaded();

    void update_timings(pdf_timings& timings_, bool set_timer);

    bool process_document_components();

  private:

    std::string filename;
    std::shared_ptr<std::string> buffer; // keep a shared copy, in order to not let it expire
    std::optional<std::string> password; // stored for thread-safe page decoding

    pdf_timings timings;

    QPDF qpdf_document;

    QPDFObjectHandle qpdf_root;
    QPDFObjectHandle qpdf_pages;

    int number_of_pages;

    nlohmann::json json_annots;
    bool annots_loaded;

    // New: Persistent page decoders for typed API
    std::map<int, page_decoder_ptr> page_decoders;
  };

  pdf_decoder<DOCUMENT>::pdf_decoder():
    filename(""),
    buffer(nullptr),
    password(std::nullopt),

    timings({}),
    qpdf_document(),

    // have compatibulity between QPDF v10 and v11
    qpdf_root(),
    qpdf_pages(),

    number_of_pages(-1),

    json_annots(nlohmann::json::value_t::null),
    annots_loaded(false),
    page_decoders({})
  {
    update_qpdf_logger();
  }

  pdf_decoder<DOCUMENT>::pdf_decoder(pdf_timings& timings_):
    filename(""),
    buffer(nullptr),
    password(std::nullopt),

    timings(timings_),
    qpdf_document(),

    // have compatibulity between QPDF v10 and v11
    qpdf_root(),
    qpdf_pages(),

    number_of_pages(-1),

    json_annots(nlohmann::json::value_t::null),
    annots_loaded(false),
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

  void pdf_decoder<DOCUMENT>::ensure_annots_loaded()
  {
    if(annots_loaded)
      {
        return;
      }

    try
      {
        utils::timer annots_timer;
        json_annots = extract_document_annotations_in_json(qpdf_document, qpdf_root);

        double annots_elapsed = annots_timer.get_time();
        timings.add_timing(pdf_timings::KEY_EXTRACT_DOC_ANNOTATIONS, annots_elapsed);
      }
    catch(const std::exception& exc)
      {
        LOG_S(WARNING) << "filename: " << filename << " can not find any `annotations` " << exc.what();
      }

    annots_loaded = true;
  }

  bool pdf_decoder<DOCUMENT>::process_document_components()
  {
    utils::timer timer;

    if(qpdf_root.hasKey("/Pages"))
      {
        qpdf_pages = qpdf_root.getKey("/Pages");

        if(qpdf_pages.hasKey("/Count"))
          {
            number_of_pages = qpdf_pages.getKey("/Count").getIntValue();
          }
        else
          {
            LOG_S(WARNING) << "filename: " << filename << " has no `/Count`";
            number_of_pages = 0;
            for(QPDFObjectHandle page : qpdf_document.getAllPages())
              {
                number_of_pages += 1;
              }
          }

        LOG_S(INFO) << "#-pages: " << number_of_pages;
      }
    else
      {
        LOG_S(ERROR) << "filename: " << filename << " has no pages";
        return false;
      }

    timings.add_timing(pdf_timings::KEY_PROCESS_DOCUMENT_COMPONENTS, timer.get_time());

    return true;
  }

  nlohmann::json pdf_decoder<DOCUMENT>::get_annotations()
  {
    ensure_annots_loaded();
    return json_annots;
  }

  nlohmann::json pdf_decoder<DOCUMENT>::get_meta_xml()
  {
    ensure_annots_loaded();
    return json_annots["meta_xml"];
  }

  nlohmann::json pdf_decoder<DOCUMENT>::get_table_of_contents()
  {
    ensure_annots_loaded();
    return json_annots["table_of_contents"];
  }

  /*
    bool pdf_decoder<DOCUMENT>::process_document_from_file(std::string& _filename,
    std::optional<std::string>& password)
    {
    try
    {
    if (password.has_value())
    {
    qpdf_document.processFile(filename.c_str(), password.value().c_str());
    }
    else
    {
    qpdf_document.processFile(filename.c_str());
    }
    LOG_S(INFO) << "filename: " << filename << " processed by qpdf!";

    qpdf_root  = qpdf_document.getRoot();
    }
    catch(const std::exception& exc)
    {
    LOG_S(ERROR) << "filename: " << filename << " can not be processed by qpdf: " << exc.what();
    return false;
    }

    timings.add_timing(pdf_timings::KEY_PROCESS_DOCUMENT_FROM_FILE, timer.get_time());

    if(not process_document_components())
    {
    return false;
    }

    ensure_annots_loaded();

    return true;
    }
  */

  bool pdf_decoder<DOCUMENT>::process_document_from_file(std::string& _filename,
                                                         std::optional<std::string>& _password)
  {
    filename = _filename; // save it
    LOG_S(INFO) << "start processing '" << filename << "' by reading into memory ...";

    utils::timer timer;

    // Read the file into a buffer
    std::ifstream ifs(filename, std::ios::binary | std::ios::ate);
    if(!ifs.is_open())
      {
        LOG_S(ERROR) << "could not open file: " << filename;
        return false;
      }

    auto file_size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    auto file_buffer = std::make_shared<std::string>(file_size, '\0');
    if(!ifs.read(file_buffer->data(), file_size))
      {
        LOG_S(ERROR) << "could not read file: " << filename;
        return false;
      }
    ifs.close();

    std::string description = "processing file " + filename;
    bool result = process_document_from_bytesio(file_buffer, _password, description);

    double total_elapsed = timer.get_time();
    timings.add_timing(pdf_timings::KEY_PROCESS_DOCUMENT_FROM_FILE, total_elapsed);

    return result;
  }

  bool pdf_decoder<DOCUMENT>::process_document_from_bytesio(std::shared_ptr<std::string> _buffer,
                                                            std::optional<std::string>& _password,
                                                            std::string description)
  {
    buffer = _buffer;
    password = _password;
    LOG_S(INFO) << "start processing buffer of size " << buffer->size() << " by qpdf ...";

    utils::timer timer;

    try
      {
        if (password.has_value())
          {
            qpdf_document.processMemoryFile(description.c_str(),
                                            buffer->c_str(),
                                            buffer->size(),
                                            password.value().c_str());
          }
        else
          {
            qpdf_document.processMemoryFile(description.c_str(),
                                            buffer->c_str(),
                                            buffer->size());
          }
        LOG_S(INFO) << "buffer processed by qpdf!";

        qpdf_root = qpdf_document.getRoot();
      }
    catch(const std::exception & exc)
      {
        LOG_S(ERROR) << "could not process buffer by qpdf: " << exc.what();
        return false;
      }

    timings.add_timing(pdf_timings::KEY_PROCESS_DOCUMENT_FROM_BYTESIO, timer.get_time());

    if(not process_document_components())
      {
        return false;
      }

    ensure_annots_loaded();

    return true;
  }

  void pdf_decoder<DOCUMENT>::decode_document(const decode_config& config)
  {
    LOG_S(INFO) << "start decoding all pages ...";
    utils::timer timer;

    for(int page_number = 0; page_number < number_of_pages; page_number++)
      {
        decode_page(page_number, config);
      }

    timings.add_timing(pdf_timings::KEY_DECODE_DOCUMENT, timer.get_time());
  }

  void pdf_decoder<DOCUMENT>::decode_document(std::vector<int>& page_numbers,
                                              const decode_config& config)
  {
    LOG_S(INFO) << "start decoding selected pages:\n" << config.to_string();
    utils::timer timer;

    for(auto page_number : page_numbers)
      {
        decode_page(page_number, config);
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

  pdf_decoder<DOCUMENT>::page_decoder_ptr pdf_decoder<DOCUMENT>::decode_page(int page_number,
                                                                             const decode_config& config)
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

    // Get the QPDF page and decode it
    std::shared_ptr<pdf_decoder<PAGE> > page_decoder = nullptr;
    {
      bool set_timer = (timings.empty());

      if(config.do_thread_safe)
        {
          // creates its own QPDF document
          page_decoder = std::make_shared<pdf_decoder<PAGE>>(buffer, password, page_number);	  
        }
      else
        {
          std::vector<QPDFObjectHandle> pages = qpdf_document.getAllPages();
          QPDFObjectHandle qpdf_page = pages.at(page_number);

          page_decoder = std::make_shared<pdf_decoder<PAGE>>(qpdf_page, page_number);
        }

      page_decoder->decode_page(config);

      update_timings(page_decoder->get_timings(), set_timer);
    }

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
    if(page_decoders.count(page_number) > 0)
      {
        page_decoders.erase(page_number);
        LOG_S(INFO) << "unloaded page decoder for page: " << page_number;
      }

    return true;
  }

  bool pdf_decoder<DOCUMENT>::unload_pages()
  {
    page_decoders.clear();
    LOG_S(INFO) << "unloaded all page decoders";

    return true;
  }

}

#endif
