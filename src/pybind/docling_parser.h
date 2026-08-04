//-*-C++-*-

#ifndef PYBIND_PDF_PARSER_H
#define PYBIND_PDF_PARSER_H

#include <atomic>
#include <list>
#include <optional>
#ifdef _WIN32
#include <locale>
#include <codecvt>
#endif

#include <pybind/native_memory.h>
#include <pybind/docling_resources.h>

#include <parse.h>

namespace docling
{
  class docling_parser: public docling_resources
  {
    typedef pdflib::pdf_decoder<pdflib::PAGE> page_decoder_type;
    typedef pdflib::pdf_decoder<pdflib::DOCUMENT> doc_decoder_type;

    typedef std::shared_ptr<page_decoder_type> page_decoder_ptr_type;
    typedef std::shared_ptr<doc_decoder_type> doc_decoder_ptr_type;
    
  public:

    docling_parser(std::string level="fatal", int max_concurrent_results=16);

    void set_loglevel_with_label(std::string level="error");

    bool is_loaded(std::string key);
    std::vector<std::string> list_loaded_keys();

    bool load_document(std::string key,
		       std::string filename,
		       std::optional<std::string> password,
                       bool keep_qpdf_warnings = false);
    
    bool load_document_from_bytesio(std::string key,
				    pybind11::object bytes_io,
				    std::optional<std::string> password,
                                    bool keep_qpdf_warnings = false);

    bool unload_document(std::string key);
    bool unload_document_pages(std::string key);
    bool unload_document_page(std::string key, int page_num);

    void unload_documents();

    int number_of_pages(std::string key);

    nlohmann::json get_annotations(std::string key);

    nlohmann::json get_meta_xml(std::string key);
    nlohmann::json get_table_of_contents(std::string key);

    std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE>> get_page_decoder(std::string key,
                                                                        int page,
                                                                        const pdflib::decode_config& config);

  private:

    struct page_decoder_cache_entry
    {
      std::string key;
      int page_number;
      page_decoder_ptr_type page_decoder;
    };

    bool verify_page_boundary(std::string page_boundary);
    void maybe_release_native_memory(const pdflib::decode_config& config);
    page_decoder_ptr_type find_page_decoder(const std::string& key, int page);
    void add_page_decoder(const std::string& key, int page, page_decoder_ptr_type page_decoder);
    void remove_page_decoders(const std::string& key);
    void remove_page_decoder(const std::string& key, int page);
    void trim_page_decoders();

  private:

    std::string pdf_resources_dir;

    std::unordered_map<std::string, doc_decoder_ptr_type> doc_decoders;
    std::list<page_decoder_cache_entry> page_decoders;
    int max_concurrent_results;
    std::atomic<int> total_processed_pages{0};
  };

  docling_parser::docling_parser(std::string level, int max_concurrent_results):
    docling_resources(),
    pdf_resources_dir(resource_utils::get_resources_dir(true).string()),
    doc_decoders({}),
    page_decoders({}),
    max_concurrent_results(max_concurrent_results)
  {
    set_loglevel_with_label(level);

    LOG_S(WARNING) << "pdf_resources_dir: " << pdf_resources_dir;

    auto RESOURCE_DIR_KEY = pdflib::pdf_resource<pdflib::PAGE_FONT>::RESOURCE_DIR_KEY;

    nlohmann::json data = nlohmann::json::object({});
    data[RESOURCE_DIR_KEY] = pdf_resources_dir;

    std::unordered_map<std::string, double> timings = {};
    pdflib::pdf_resource<pdflib::PAGE_FONT>::initialise(data, timings);
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
    for (const auto& pair : doc_decoders)
      {
        keys.push_back(pair.first);
      }

    return keys;
  }

  bool docling_parser::is_loaded(std::string key)
  {
    return (doc_decoders.count(key)==1);
  }

  bool docling_parser::load_document(std::string key,
				     std::string filename,
				     std::optional<std::string> password,
                                     bool keep_qpdf_warnings)
  {
#ifdef _WIN32
    // Convert UTF-8 string to UTF-16 wstring
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
    std::wstring wide_filename = converter.from_bytes(filename);
    std::filesystem::path path_filename(wide_filename);
#else
    std::filesystem::path path_filename(filename);
#endif

    if (std::filesystem::exists(path_filename))
      {
        remove_page_decoders(key);

        doc_decoders[key] = std::make_shared<doc_decoder_type>();
        bool success = doc_decoders.at(key)->process_document_from_file(filename,
                                                                        password,
                                                                        keep_qpdf_warnings);
        if(not success)
          {
            // do not keep a decoder that never parsed the document: it would
            // report is_loaded()=true and a number_of_pages of -1
            doc_decoders.erase(key);
            LOG_S(ERROR) << "could not process document for key=" << key;
            return false;
          }

        return true;
      }

    LOG_S(ERROR) << "File not found: " << filename;
    return false;
  }

  bool docling_parser::load_document_from_bytesio(std::string key,
						  pybind11::object bytes_io,
						  std::optional<std::string> password,
                                                  bool keep_qpdf_warnings)
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

    // Get the data into a shared buffer
    auto data_buffer = std::make_shared<std::string>(data.cast<std::string>());

    try
      {
        remove_page_decoders(key);

        doc_decoders[key] = std::make_shared<doc_decoder_type>();
        std::string description = "parsing of " + key + " from bytesio";
        bool success = doc_decoders.at(key)->process_document_from_bytesio(data_buffer,
                                                                           password,
                                                                           description,
                                                                           keep_qpdf_warnings);
        if(not success)
          {
            // do not keep a decoder that never parsed the document: it would
            // report is_loaded()=true and a number_of_pages of -1
            doc_decoders.erase(key);
            LOG_S(ERROR) << "could not decode bytesio object for key="<<key;
            return false;
          }

        return true;
      }
    catch(const std::exception& exc)
      {
        doc_decoders.erase(key);
        LOG_S(ERROR) << "could not decode bytesio object for key="<<key;
        return false;
      }

    return false;
  }

  bool docling_parser::unload_document(std::string key)
  {
    if(doc_decoders.count(key)==1)
      {
        doc_decoders.erase(key);
        remove_page_decoders(key);
        if(doc_decoders.empty())
          {
            total_processed_pages.store(0);
          }
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
    auto itr = doc_decoders.find(key);

    if(itr!=doc_decoders.end())
      {
        doc_decoder_ptr_type decoder_ptr = itr->second;
        decoder_ptr->unload_page(page_num);
        remove_page_decoder(key, page_num);
      }
    else
      {
        LOG_S(ERROR) << "key not found: " << key;
      }

    return false;
  }

  bool docling_parser::unload_document_pages(std::string key)
  {
    auto itr = doc_decoders.find(key);

    if(itr!=doc_decoders.end())
      {
        doc_decoder_ptr_type decoder_ptr = itr->second;
        decoder_ptr->unload_pages();
        remove_page_decoders(key);
      }
    else
      {
        LOG_S(ERROR) << "key not found: " << key;
      }

    return false;
  }

  void docling_parser::unload_documents()
  {
    doc_decoders.clear();
    page_decoders.clear();
    total_processed_pages.store(0);
  }

  void docling_parser::maybe_release_native_memory(const pdflib::decode_config& config)
  {
    const int every_n = config.release_native_memory_every_n_pages;
    if(every_n <= 0)
      {
        return;
      }

    const int processed = total_processed_pages.fetch_add(1, std::memory_order_relaxed) + 1;
    if((processed % every_n) == 0)
      {
        release_native_memory(processed);
      }
  }

  docling_parser::page_decoder_ptr_type docling_parser::find_page_decoder(const std::string& key,
                                                                          int page)
  {
    for(auto itr = page_decoders.begin(); itr != page_decoders.end(); ++itr)
      {
        if(itr->key == key and itr->page_number == page)
          {
            page_decoder_ptr_type page_decoder = itr->page_decoder;
            page_decoders.splice(page_decoders.end(), page_decoders, itr);
            return page_decoder;
          }
      }

    return nullptr;
  }

  void docling_parser::add_page_decoder(const std::string& key,
                                        int page,
                                        page_decoder_ptr_type page_decoder)
  {
    remove_page_decoder(key, page);
    page_decoders.push_back(page_decoder_cache_entry{key, page, page_decoder});
    trim_page_decoders();
  }

  void docling_parser::remove_page_decoders(const std::string& key)
  {
    page_decoders.remove_if([&key](const auto& page_decoder) {
      return page_decoder.key == key;
    });
  }

  void docling_parser::remove_page_decoder(const std::string& key, int page)
  {
    page_decoders.remove_if([&key, page](const auto& page_decoder) {
      return page_decoder.key == key and page_decoder.page_number == page;
    });
  }

  void docling_parser::trim_page_decoders()
  {
    while(static_cast<int>(page_decoders.size()) > max_concurrent_results)
      {
        page_decoders.pop_front();
      }
  }

  int docling_parser::number_of_pages(std::string key)
  {
    auto itr = doc_decoders.find(key);

    if(itr!=doc_decoders.end())
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

    auto itr = doc_decoders.find(key);

    if(itr==doc_decoders.end())
      {
        LOG_S(ERROR) << "key not found: " << key;
        return nlohmann::json::value_t::null;
      }

    return (itr->second)->get_annotations();
  }

  nlohmann::json docling_parser::get_meta_xml(std::string key)
  {
    LOG_S(INFO) << __FUNCTION__;

    auto itr = doc_decoders.find(key);

    if(itr==doc_decoders.end())
      {
        LOG_S(ERROR) << "key not found: " << key;
        return nlohmann::json::value_t::null;
      }

    return (itr->second)->get_meta_xml();
  }

  nlohmann::json docling_parser::get_table_of_contents(std::string key)
  {
    LOG_S(INFO) << __FUNCTION__;

    auto itr = doc_decoders.find(key);

    if(itr==doc_decoders.end())
      {
        LOG_S(ERROR) << "key not found: " << key;
        return nlohmann::json::value_t::null;
      }

    return (itr->second)->get_table_of_contents();
  }

  std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE>> docling_parser::get_page_decoder(std::string key,
                                                                                      int page,
                                                                                      const pdflib::decode_config& config)
  {
    LOG_S(INFO) << __FUNCTION__ << " for key: " << key << " and page: " << page;

    auto itr = doc_decoders.find(key);
    if(itr == doc_decoders.end())
      {
        LOG_S(ERROR) << "key not found: " << key;
        return nullptr;
      }

    auto cached_page_decoder = find_page_decoder(key, page);
    if(cached_page_decoder != nullptr)
      {
        return cached_page_decoder;
      }

    auto& doc_decoder = itr->second;
    auto page_decoder = doc_decoder->make_thread_safe_page_decoder(
      page,
      config.keep_qpdf_warnings);
    page_decoder->decode_page(config);

    if(config.create_word_cells)
      {
        page_decoder->create_word_cells(config);
      }

    if(config.create_line_cells)
      {
        page_decoder->create_line_cells(config);
      }

    add_page_decoder(key, page, page_decoder);
    maybe_release_native_memory(config);
    return page_decoder;
  }

}

#endif
