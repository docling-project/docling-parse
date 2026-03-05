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
    typedef pdflib::pdf_decoder<pdflib::PAGE> page_decoder_type;
    typedef pdflib::pdf_decoder<pdflib::DOCUMENT> doc_decoder_type;

    typedef std::shared_ptr<page_decoder_type> page_decoder_ptr_type;
    typedef std::shared_ptr<doc_decoder_type> doc_decoder_ptr_type;
    
  public:

    docling_parser();

    docling_parser(std::string level);

    void set_loglevel_with_label(std::string level="error");

    bool is_loaded(std::string key);
    std::vector<std::string> list_loaded_keys();

    bool load_document(std::string key,
		       std::string filename,
		       std::optional<std::string> password);
    
    bool load_document_from_bytesio(std::string key,
				    pybind11::object bytes_io,
				    std::optional<std::string> password);

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

    //std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE>> get_page_decoders_in_parallel(const pdflib::decode_config& config);
    
  private:

    bool verify_page_boundary(std::string page_boundary);

  private:

    std::string pdf_resources_dir;

    std::unordered_map<std::string, doc_decoder_ptr_type> key2doc;
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

    std::unordered_map<std::string, double> timings = {};
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

  bool docling_parser::load_document(std::string key,
				     std::string filename,
				     std::optional<std::string> password)
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
        key2doc[key] = std::make_shared<doc_decoder_type>();
        key2doc.at(key)->process_document_from_file(filename, password);

        return true;
      }

    LOG_S(ERROR) << "File not found: " << filename;
    return false;
  }

  bool docling_parser::load_document_from_bytesio(std::string key,
						  pybind11::object bytes_io,
						  std::optional<std::string> password)
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
        key2doc[key] = std::make_shared<doc_decoder_type>();
        std::string description = "parsing of " + key + " from bytesio";
        key2doc.at(key)->process_document_from_bytesio(data_buffer, password, description);

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
        doc_decoder_ptr_type decoder_ptr = itr->second;
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
        doc_decoder_ptr_type decoder_ptr = itr->second;
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

  std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE>> docling_parser::get_page_decoder(std::string key,
                                                                                      int page,
                                                                                      const pdflib::decode_config& config)
  {
    LOG_S(INFO) << __FUNCTION__ << " for key: " << key << " and page: " << page;

    auto itr = key2doc.find(key);
    if(itr == key2doc.end())
      {
        LOG_S(ERROR) << "key not found: " << key;
        return nullptr;
      }

    auto& doc_decoder = itr->second;
    return doc_decoder->decode_page(page, config);
  }

}

#endif
