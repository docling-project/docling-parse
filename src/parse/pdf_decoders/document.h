//-*-C++-*-

#ifndef PDF_DOCUMENT_DECODER_H
#define PDF_DOCUMENT_DECODER_H

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <qpdf/Buffer.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <parse/qpdf/logger.h>

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
                                    std::optional<std::string>& _password,
                                    bool keep_qpdf_warnings = false);

    bool process_document_from_bytesio(std::shared_ptr<std::string> _buffer,
                                       std::optional<std::string>& _password,
                                       std::string description = "processing buffer",
                                       bool keep_qpdf_warnings = false);

    void decode_document(const decode_config& config);

    void decode_document(std::vector<int>& page_numbers,
                         const decode_config& config);

    // Decode a single page and return the page decoder directly
    page_decoder_ptr decode_page(int page_number,
                                 const decode_config& config);
    page_decoder_ptr make_thread_safe_page_decoder(int page_number,
                                                   bool keep_qpdf_warnings);
    
    // New: Direct access to page decoders (typed API)
    bool has_page_decoder(int page_number);
    page_decoder_ptr get_page_decoder(int page_number);

    bool unload_pages();

    bool unload_page(int page_number);

    pdf_timings& get_timings() { return timings; }

  private:

    std::pair<int, std::shared_ptr<std::string> > get_thread_safe_page_buffer(
      int page_ind,
      bool keep_qpdf_warnings);

    QPDFAcroFormDocumentHelper& source_acroform_helper();

    void ensure_annots_loaded();

    void update_timings(pdf_timings& timings_, bool set_timer);

  private:

    std::string filename;
    std::shared_ptr<std::string> buffer; // keep a shared copy, in order to not let it expire
    std::optional<std::string> password; // stored for thread-safe page decoding

    std::mutex thread_safe_buffer_mutex;

    // Built at most once per document, and only once a page is found that
    // actually carries annotations. Its constructor calls analyze(), which
    // walks every field of /AcroForm and then every page of the document, so
    // building it per page makes page extraction quadratic in the page count.
    // Only ever touched under thread_safe_buffer_mutex.
    std::unique_ptr<QPDFAcroFormDocumentHelper> src_afdh;

    pdf_timings timings;

    QPDF qpdf_document;
    std::vector<QPDFObjectHandle> qpdf_pages;

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
    qpdf_pages({}),

    number_of_pages(-1),

    json_annots(nlohmann::json::value_t::null),
    annots_loaded(false),
    page_decoders({})
  {
    configure_qpdf_warnings(qpdf_document);
  }

  pdf_decoder<DOCUMENT>::pdf_decoder(pdf_timings& timings_):
    filename(""),
    buffer(nullptr),

    password(std::nullopt),

    timings(timings_),
    qpdf_document(),
    qpdf_pages({}),

    number_of_pages(-1),

    json_annots(nlohmann::json::value_t::null),
    annots_loaded(false),
    page_decoders({})
  {
    configure_qpdf_warnings(qpdf_document);
  }

  pdf_decoder<DOCUMENT>::~pdf_decoder()
  {}

  void pdf_decoder<DOCUMENT>::ensure_annots_loaded()
  {
    if(annots_loaded)
      {
        return;
      }

    try
      {
        QPDFObjectHandle qpdf_root = qpdf_document.getRoot();
	
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

  bool pdf_decoder<DOCUMENT>::process_document_from_file(std::string& _filename,
                                                         std::optional<std::string>& _password,
                                                         bool keep_qpdf_warnings)
  {
    filename = _filename; // save it
    LOG_S(INFO) << "start processing '" << filename << "' by reading into memory ...";

    utils::timer timer;

    // Read the file into a buffer.
    //
    // The filename arrives UTF-8 encoded from the Python layer. On Windows a
    // narrow ifstream hands it to the ANSI codepage (GBK, cp1252, ...), so any
    // non-ASCII path fails to open (docling-parse#324; regression from #274,
    // which replaced qpdf's UTF-8-aware processFile with this read). Build a
    // std::filesystem::path from the bytes as UTF-8 so MSVC opens via the
    // wide-char API; POSIX keeps byte-passthrough semantics either way.
#ifdef _WIN32
    std::filesystem::path fs_path(
      std::u8string(filename.begin(), filename.end()));
#else
    std::filesystem::path fs_path(filename);
#endif
    std::ifstream ifs(fs_path, std::ios::binary | std::ios::ate);
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
    bool result = process_document_from_bytesio(file_buffer,
                                                _password,
                                                description,
                                                keep_qpdf_warnings);

    double total_elapsed = timer.get_time();
    timings.add_timing(pdf_timings::KEY_PROCESS_DOCUMENT_FROM_FILE, total_elapsed);

    return result;
  }

  bool pdf_decoder<DOCUMENT>::process_document_from_bytesio(std::shared_ptr<std::string> _buffer,
                                                            std::optional<std::string>& _password,
                                                            std::string description,
                                                            bool keep_qpdf_warnings)
  {
    buffer = _buffer;
    password = _password;

    LOG_S(INFO) << "start processing buffer of size " << buffer->size() << " by qpdf ...";
    
    utils::timer timer;
    
    try
      {
        qpdf_document.setSuppressWarnings(!keep_qpdf_warnings);

	utils::timer process_timer;

        if(password.has_value())
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
        LOG_S(INFO) << "buffer processed by qpdf which took  " << process_timer.get_time() << " sec";

	timings.add_timing(pdf_timings::KEY_QPDF_PROCESS, process_timer.get_time());

	if(keep_qpdf_warnings and qpdf_document.anyWarnings())
	  {
	    LOG_S(WARNING) << "qpdf detected inconsistencies!";
	  }

	qpdf_pages = qpdf_document.getAllPages();
	number_of_pages = qpdf_pages.size();
	
        LOG_S(INFO) << "#-pages: " << number_of_pages;	
      }
    catch(const std::exception & exc)
      {
        LOG_S(ERROR) << "could not process buffer by qpdf: " << exc.what();
        return false;
      }
    
    ensure_annots_loaded();

    timings.add_timing(pdf_timings::KEY_PROCESS_DOCUMENT_FROM_BYTESIO, timer.get_time());
    
    return true;
  }

  std::pair<int, std::shared_ptr<std::string> > pdf_decoder<DOCUMENT>::get_thread_safe_page_buffer(
    int page_ind,
    bool keep_qpdf_warnings)
  {
    std::pair<int, std::shared_ptr<std::string> > result(-1, nullptr);

    /*
    if(not qpdf_document.anyWarnings())
      {
	result.first = page_ind;
	result.second = buffer;

	return result;
      }
    */

    if(number_of_pages == 1)
      {
        // Preserve the existing bounds behavior of qpdf_pages.at(page_ind),
        // but avoid serializing an already standalone one-page PDF.
        (void)qpdf_pages.at(page_ind);

        result.first = page_ind;
        result.second = buffer;

        return result;
      }
    
    // Thread-safe decoding uses standalone one-page PDF buffers.
    // Page extraction and serialization are intentionally serialized, and
    // the mutex is expected to remain held across QPDFWriter::write().
    utils::timer page_timer;

    std::shared_ptr<Buffer> out = nullptr;
    {
      std::lock_guard<std::mutex> lock(thread_safe_buffer_mutex);

      QPDFObjectHandle qpdf_page = qpdf_pages.at(page_ind);

      QPDF out_pdf;
      out_pdf.setSuppressWarnings(!keep_qpdf_warnings);
      out_pdf.emptyPDF();

      QPDFPageDocumentHelper out_pages(out_pdf);
      QPDFPageObjectHelper page_helper(qpdf_page);
      out_pages.addPage(page_helper, false);
      auto out_page = out_pages.getAllPages().at(0);

      // fixCopiedAnnotations() begins by reading /Annots off the source page
      // and returns immediately when it is not a non-empty array, so on such a
      // page the call cannot change the output. Testing that here as well lets
      // us skip building both helpers -- and the source helper is the
      // expensive one. 91% of the pages of the reference corpus have no
      // annotations at all.
      QPDFObjectHandle qpdf_annots = qpdf_page.getKey("/Annots");
      if(qpdf_annots.isArray() and qpdf_annots.getArrayNItems()>0)
        {
          QPDFAcroFormDocumentHelper out_afdh(out_pdf);
          out_afdh.fixCopiedAnnotations(out_page.getObjectHandle(),
                                        page_helper.getObjectHandle(),
                                        source_acroform_helper());
        }

      QPDFWriter writer(out_pdf);
      writer.setOutputMemory();
      writer.setObjectStreamMode(qpdf_o_preserve);
      writer.setStreamDataMode(qpdf_s_preserve);
      writer.setPreserveEncryption(true);
      writer.write();

      out = writer.getBufferSharedPointer();
    }

    // Copying qpdf's buffer into a std::string does not touch the shared
    // document, so it happens after the lock is released.
    result.first = 0;
    result.second = std::make_shared<std::string>(reinterpret_cast<char const*>(out->getBuffer()),
                                                  out->getSize());

    LOG_S(INFO) << "writing a pdf-page buffer in " << page_timer.get_time() << " [sec]";

    return result;
  }

  QPDFAcroFormDocumentHelper& pdf_decoder<DOCUMENT>::source_acroform_helper()
  {
    // Caller holds thread_safe_buffer_mutex.
    if(src_afdh==nullptr)
      {
        src_afdh = std::make_unique<QPDFAcroFormDocumentHelper>(qpdf_document);
      }

    return *src_afdh;
  }

  pdf_decoder<DOCUMENT>::page_decoder_ptr
  pdf_decoder<DOCUMENT>::make_thread_safe_page_decoder(int page_number,
                                                       bool keep_qpdf_warnings)
  {
    std::pair<int, std::shared_ptr<std::string> > result =
      get_thread_safe_page_buffer(page_number, keep_qpdf_warnings);

    int orig_page_number = page_number;
    int curr_page_number = result.first;

    std::shared_ptr<std::string> page_buffer = result.second;
    
    return std::make_shared<pdf_decoder<PAGE>>(page_buffer,
                                               password,
                                               orig_page_number,
                                               curr_page_number,
                                               keep_qpdf_warnings);
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
    if((page_number < 0) or (page_number >= number_of_pages))
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
	  LOG_S(INFO) << "decoding page thread-safe";
          page_decoder = make_thread_safe_page_decoder(page_number,
                                                       config.keep_qpdf_warnings);
        }
      else
        {
	  LOG_S(INFO) << "decoding page thread-unsafe";
          QPDFObjectHandle qpdf_page = qpdf_pages.at(page_number);

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
