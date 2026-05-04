//-*-C++-*-

#ifndef PYBIND_THREADED_PDF_BASE_H
#define PYBIND_THREADED_PDF_BASE_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <locale>
#include <codecvt>
#endif

#include <pybind/docling_resources.h>

#include <parse.h>

namespace docling
{
  struct page_decode_result
  {
    std::string doc_key;
    int page_number;
    bool success;
    std::string error_message;
    std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE>> page_decoder;
  };

  // ---------------------------------------------------------------------------
  // docling_threaded_base<Derived, ResultType>
  //
  // CRTP base that owns all thread-pool machinery and document storage.
  //
  // Two explicit template parameters avoid the classic CRTP ordering problem
  // where typename Derived::result_type cannot be resolved while the derived
  // class body is still incomplete (i.e. during base-class instantiation).
  //
  // Derived must provide:
  //   void worker_loop();
  // ---------------------------------------------------------------------------

  template<typename Derived, typename ResultType>
  class docling_threaded_base : public docling_resources
  {
    using page_decoder_type    = pdflib::pdf_decoder<pdflib::PAGE>;
    using doc_decoder_type     = pdflib::pdf_decoder<pdflib::DOCUMENT>;
    using doc_decoder_ptr_type = std::shared_ptr<doc_decoder_type>;

  public:

    docling_threaded_base(std::string loglevel,
                          int num_threads,
                          int max_concurrent_results,
                          pdflib::decode_config config);

    ~docling_threaded_base();

    bool load_document(std::string key,
                       std::string filename,
                       std::optional<std::string> password);

    bool load_document_from_bytesio(std::string key,
                                    pybind11::object bytes_io,
                                    std::optional<std::string> password);

    bool has_tasks();

    ResultType get_task();

  private:

    void set_loglevel_with_label(std::string level);

    void build_task_queue();

    void start_workers();

  protected:

    pdflib::decode_config config;
    int num_threads;
    int max_concurrent_results;

    std::unordered_map<std::string, doc_decoder_ptr_type> key2doc;

    // Task queue: (doc_key, page_number) pairs
    std::queue<std::pair<std::string, int>> task_queue;
    std::mutex task_mutex;

    // Results queue with bounded capacity
    std::queue<ResultType> results_queue;
    std::mutex results_mutex;
    std::condition_variable cv_results_available;
    std::condition_variable cv_results_consumed;

    // State tracking
    std::atomic<int> tasks_remaining{0};
    std::atomic<bool> started{false};
    std::atomic<int> active_workers{0};

    std::vector<std::thread> workers;
  };

  // ---------------------------------------------------------------------------
  // Implementation
  // ---------------------------------------------------------------------------

  template<typename Derived, typename ResultType>
  docling_threaded_base<Derived, ResultType>::docling_threaded_base(
      std::string loglevel,
      int num_threads,
      int max_concurrent_results,
      pdflib::decode_config config):
    docling_resources(),
    config(config),
    num_threads(num_threads),
    max_concurrent_results(max_concurrent_results),
    key2doc({})
  {
    set_loglevel_with_label(loglevel);

    // Force thread-safe mode since we're doing parallel decoding
    this->config.do_thread_safe = true;

    std::string pdf_resources_dir = resource_utils::get_resources_dir(true).string();
    LOG_S(WARNING) << "pdf_resources_dir: " << pdf_resources_dir;

    auto RESOURCE_DIR_KEY = pdflib::pdf_resource<pdflib::PAGE_FONT>::RESOURCE_DIR_KEY;

    nlohmann::json data = nlohmann::json::object({});
    data[RESOURCE_DIR_KEY] = pdf_resources_dir;

    std::unordered_map<std::string, double> timings = {};
    pdflib::pdf_resource<pdflib::PAGE_FONT>::initialise(data, timings);
  }

  template<typename Derived, typename ResultType>
  docling_threaded_base<Derived, ResultType>::~docling_threaded_base()
  {
    for(auto& worker : workers)
      {
        if(worker.joinable())
          {
            worker.join();
          }
      }
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::set_loglevel_with_label(std::string level)
  {
    if(level == "info")
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
      }
    else if(level == "warning" or level == "warn")
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
      }
    else if(level == "error")
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
      }
    else if(level == "fatal")
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_FATAL;
      }
    else
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
      }
  }

  template<typename Derived, typename ResultType>
  bool docling_threaded_base<Derived, ResultType>::load_document(
      std::string key,
      std::string filename,
      std::optional<std::string> password)
  {
    if(started.load())
      {
        LOG_S(ERROR) << "Cannot load documents after processing has started";
        return false;
      }

#ifdef _WIN32
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    std::wstring wide_filename = converter.from_bytes(filename);
    std::filesystem::path path_filename(wide_filename);
#else
    std::filesystem::path path_filename(filename);
#endif

    if(std::filesystem::exists(path_filename))
      {
        key2doc[key] = std::make_shared<doc_decoder_type>();
        key2doc.at(key)->process_document_from_file(filename, password);
        return true;
      }

    LOG_S(ERROR) << "File not found: " << filename;
    return false;
  }

  template<typename Derived, typename ResultType>
  bool docling_threaded_base<Derived, ResultType>::load_document_from_bytesio(
      std::string key,
      pybind11::object bytes_io,
      std::optional<std::string> password)
  {
    if(started.load())
      {
        LOG_S(ERROR) << "Cannot load documents after processing has started";
        return false;
      }

    LOG_S(INFO) << __FILE__ << ":" << __LINE__ << "\t" << __FUNCTION__;

    if(not pybind11::hasattr(bytes_io, "read"))
      {
        throw std::runtime_error("Expected a BytesIO object");
      }

    bytes_io.attr("seek")(0);

    pybind11::bytes data = bytes_io.attr("read")();

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
        LOG_S(ERROR) << "could not decode bytesio object for key=" << key;
        return false;
      }

    return false;
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::build_task_queue()
  {
    for(const auto& pair : key2doc)
      {
        const std::string& doc_key = pair.first;
        int num_pages = pair.second->get_number_of_pages();

        for(int page = 0; page < num_pages; page++)
          {
            task_queue.push(std::make_pair(doc_key, page));
          }
      }

    tasks_remaining.store(static_cast<int>(task_queue.size()));
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::start_workers()
  {
    int num_workers = std::min(num_threads, static_cast<int>(task_queue.size()));
    active_workers.store(num_workers);

    for(int i = 0; i < num_workers; i++)
      {
        workers.emplace_back(&Derived::worker_loop, static_cast<Derived*>(this));
      }
  }

  template<typename Derived, typename ResultType>
  bool docling_threaded_base<Derived, ResultType>::has_tasks()
  {
    if(not started.load())
      {
        build_task_queue();
        start_workers();
        started.store(true);
      }

    return tasks_remaining.load() > 0;
  }

  template<typename Derived, typename ResultType>
  ResultType docling_threaded_base<Derived, ResultType>::get_task()
  {
    std::unique_lock<std::mutex> lock(results_mutex);

    cv_results_available.wait(lock, [this]() {
      return not results_queue.empty() or active_workers.load() == 0;
    });

    if(results_queue.empty())
      {
        ResultType empty_result;
        empty_result.doc_key = "";
        empty_result.page_number = -1;
        empty_result.success = false;
        empty_result.error_message = "No more tasks available";
        return empty_result;
      }

    ResultType result = std::move(results_queue.front());
    results_queue.pop();
    tasks_remaining.fetch_sub(1);

    lock.unlock();

    cv_results_consumed.notify_one();

    return result;
  }

}

#endif
