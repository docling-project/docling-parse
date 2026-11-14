//-*-C++-*-

#ifndef PYBIND_PDF_PARSER_H
#define PYBIND_PDF_PARSER_H

#include <atomic>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
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

    // Load from bytes already owned by native code. Needs no Python object,
    // so it may be called with the GIL released.
    bool load_document_from_buffer(std::string key,
                                   std::shared_ptr<std::string> buffer,
                                   std::optional<std::string> password,
                                   bool keep_qpdf_warnings = false);

    bool unload_document(std::string key);
    bool unload_document_pages(std::string key);
    bool unload_document_page(std::string key, int page_num);

    void unload_documents();

    int number_of_pages(std::string key);

    // Return and clear why the last load of `key` failed:
    // {"category": ..., "message": ...}, or an empty map.
    std::map<std::string, std::string> take_load_error(std::string key);

    nlohmann::json get_annotations(std::string key);

    // Snapshot ownership while Python holds the GIL, before a binding releases
    // it for immutable/lazy document reads. Unload can then remove the map entry
    // without destroying an in-flight decoder.
    doc_decoder_ptr_type get_document_decoder(const std::string& key);

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
    void record_load_error(const std::string& key,
                           const std::string& category,
                           const std::string& message);
    void remove_page_decoder(const std::string& key, int page);
    void trim_page_decoders();

  protected:

    // Thread safety: every public method takes parser_mutex around its access
    // to the members below, so one parser may be shared between threads. The
    // mutex is recursive because docling_editor calls these methods while it
    // already holds it. docling_parser itself never holds it across a page
    // decode, and nothing may wait for the Python GIL while holding it.
    mutable std::recursive_mutex parser_mutex;

    std::string pdf_resources_dir;

    std::unordered_map<std::string, doc_decoder_ptr_type> doc_decoders;

    // Failed loads by key, until Python collects the reason.
    std::unordered_map<std::string, std::map<std::string, std::string>> load_errors;

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

  docling_parser::doc_decoder_ptr_type docling_parser::get_document_decoder(const std::string& key)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    auto it = doc_decoders.find(key);
    return it == doc_decoders.end() ? nullptr : it->second;
  }

  std::vector<std::string> docling_parser::list_loaded_keys()
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

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
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return (doc_decoders.count(key)==1);
  }

  std::map<std::string, std::string> docling_parser::take_load_error(std::string key)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    std::map<std::string, std::string> result;

    auto itr = load_errors.find(key);
    if(itr != load_errors.end())
      {
        result = itr->second;
        load_errors.erase(itr);
      }

    return result;
  }

  void docling_parser::record_load_error(const std::string& key,
                                         const std::string& category,
                                         const std::string& message)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    load_errors[key] = {{"category", category}, {"message", message}};
  }

  bool docling_parser::load_document(std::string key,
				     std::string filename,
				     std::optional<std::string> password,
                                     bool keep_qpdf_warnings)
  {
    {
      std::lock_guard<std::recursive_mutex> lock(parser_mutex);
      load_errors.erase(key);
    }

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
        // Read and parse outside parser_mutex: the new decoder is not visible
        // to any other thread until it is registered below.
        auto doc_decoder = std::make_shared<doc_decoder_type>();
        bool success = doc_decoder->process_document_from_file(filename,
                                                               password,
                                                               keep_qpdf_warnings);

        std::lock_guard<std::recursive_mutex> lock(parser_mutex);

        remove_page_decoders(key);

        if(not success)
          {
            // do not keep a decoder that never parsed the document: it would
            // report is_loaded()=true and a number_of_pages of -1
            record_load_error(key,
                              doc_decoder->get_load_error_category(),
                              doc_decoder->get_load_error_message());
            doc_decoders.erase(key);
            LOG_S(ERROR) << "could not process document for key=" << key;
            return false;
          }

        doc_decoders[key] = doc_decoder;
        return true;
      }

    record_load_error(key, pdflib::load_error::IO, "file not found: " + filename);
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

    // The Python calls below need the GIL, so they run before parser_mutex is
    // taken: a thread must never wait for the GIL while holding that mutex.

    // Seek to the beginning of the BytesIO stream
    bytes_io.attr("seek")(0);

    // Read the entire content of the BytesIO stream
    pybind11::bytes data = bytes_io.attr("read")();

    // Get the data into a shared buffer
    auto data_buffer = std::make_shared<std::string>(data.cast<std::string>());

    return load_document_from_buffer(key, data_buffer, password, keep_qpdf_warnings);
  }

  bool docling_parser::load_document_from_buffer(std::string key,
                                                 std::shared_ptr<std::string> buffer,
                                                 std::optional<std::string> password,
                                                 bool keep_qpdf_warnings)
  {
    {
      std::lock_guard<std::recursive_mutex> lock(parser_mutex);
      load_errors.erase(key);
    }

    if(buffer == nullptr)
      {
        record_load_error(key, pdflib::load_error::IO, "no buffer provided");
        return false;
      }

    auto doc_decoder = std::make_shared<doc_decoder_type>();

    try
      {
        // Parse outside parser_mutex: the new decoder is not visible to any
        // other thread until it is registered below.
        std::string description = "parsing of " + key + " from bytesio";
        bool success = doc_decoder->process_document_from_bytesio(buffer,
                                                                  password,
                                                                  description,
                                                                  keep_qpdf_warnings);

        std::lock_guard<std::recursive_mutex> lock(parser_mutex);

        remove_page_decoders(key);

        if(not success)
          {
            // do not keep a decoder that never parsed the document: it would
            // report is_loaded()=true and a number_of_pages of -1
            record_load_error(key,
                              doc_decoder->get_load_error_category(),
                              doc_decoder->get_load_error_message());
            doc_decoders.erase(key);
            LOG_S(ERROR) << "could not decode bytesio object for key="<<key;
            return false;
          }

        doc_decoders[key] = doc_decoder;
        return true;
      }
    catch(const std::exception& exc)
      {
        std::lock_guard<std::recursive_mutex> lock(parser_mutex);

        record_load_error(key, pdflib::load_error::UNKNOWN, exc.what());
        doc_decoders.erase(key);
        LOG_S(ERROR) << "could not decode bytesio object for key="<<key;
        return false;
      }

    return false;
  }

  bool docling_parser::unload_document(std::string key)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

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
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

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
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

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
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

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
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

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

    // The decoder serializes its own lazy reads, so parser_mutex only covers
    // the lookup.
    doc_decoder_ptr_type doc_decoder = get_document_decoder(key);

    if(doc_decoder==nullptr)
      {
        LOG_S(ERROR) << "key not found: " << key;
        return nlohmann::json::value_t::null;
      }

    return doc_decoder->get_annotations();
  }

  nlohmann::json docling_parser::get_meta_xml(std::string key)
  {
    LOG_S(INFO) << __FUNCTION__;

    // The decoder serializes its own lazy reads, so parser_mutex only covers
    // the lookup.
    doc_decoder_ptr_type doc_decoder = get_document_decoder(key);

    if(doc_decoder==nullptr)
      {
        LOG_S(ERROR) << "key not found: " << key;
        return nlohmann::json::value_t::null;
      }

    return doc_decoder->get_meta_xml();
  }

  nlohmann::json docling_parser::get_table_of_contents(std::string key)
  {
    LOG_S(INFO) << __FUNCTION__;

    // The decoder serializes its own lazy reads, so parser_mutex only covers
    // the lookup.
    doc_decoder_ptr_type doc_decoder = get_document_decoder(key);

    if(doc_decoder==nullptr)
      {
        LOG_S(ERROR) << "key not found: " << key;
        return nlohmann::json::value_t::null;
      }

    return doc_decoder->get_table_of_contents();
  }

  std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE>> docling_parser::get_page_decoder(std::string key,
                                                                                      int page,
                                                                                      const pdflib::decode_config& config)
  {
    LOG_S(INFO) << __FUNCTION__ << " for key: " << key << " and page: " << page;

    doc_decoder_ptr_type doc_decoder = nullptr;
    {
      std::lock_guard<std::recursive_mutex> lock(parser_mutex);

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

      doc_decoder = itr->second;
    }

    // The decode runs without parser_mutex: it works on a standalone one-page
    // copy that the document decoder hands out under its own mutex, so threads
    // sharing this parser still decode different pages in parallel.
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

    {
      std::lock_guard<std::recursive_mutex> lock(parser_mutex);

      auto itr = doc_decoders.find(key);
      if(itr != doc_decoders.end() and itr->second == doc_decoder)
        {
          // Another thread may have decoded the same page meanwhile; its
          // result is already shared, so ours is dropped.
          auto cached_page_decoder = find_page_decoder(key, page);
          if(cached_page_decoder != nullptr)
            {
              page_decoder = cached_page_decoder;
            }
          else
            {
              add_page_decoder(key, page, page_decoder);
            }
        }
      // else: the document was unloaded or replaced during the decode; the
      // result is returned to this caller but not cached under the key.
    }

    maybe_release_native_memory(config);
    return page_decoder;
  }

}

#endif
