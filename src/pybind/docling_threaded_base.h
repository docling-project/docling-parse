//-*-C++-*-

#ifndef PYBIND_THREADED_PDF_BASE_H
#define PYBIND_THREADED_PDF_BASE_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <locale>
#include <codecvt>
#endif

#include <pybind/native_memory.h>
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
                       std::optional<std::string> password,
                       std::optional<std::vector<int>> page_numbers = std::nullopt);

    bool load_document_from_bytesio(std::string key,
                                    pybind11::object bytes_io,
                                    std::optional<std::string> password,
                                    std::optional<std::vector<int>> page_numbers = std::nullopt);

    int number_of_pages(std::string key) const;
    int scheduled_number_of_pages(std::string key) const;

    bool unload_document(std::string key);
    void unload_all_documents();

    bool has_tasks();

    ResultType get_task();

  private:

    void set_loglevel_with_label(std::string level);
    std::vector<int> normalise_page_numbers(const std::string& key,
                                            int num_pages,
                                            std::optional<std::vector<int>> page_numbers) const;
    void validate_unload_state() const;
    void reset_after_completion();

    void build_task_queue();

    void start_workers();

  protected:

    void maybe_release_native_memory();

    pdflib::decode_config config;
    int num_threads;
    int max_concurrent_results;

    std::unordered_map<std::string, doc_decoder_ptr_type> key2doc;
    std::unordered_map<std::string, std::vector<int>> key2scheduled_pages;

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
    std::atomic<int> total_processed_pages{0};

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
    key2doc({}),
    key2scheduled_pages({})
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
      std::optional<std::string> password,
      std::optional<std::vector<int>> page_numbers)
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
        try
          {
            key2doc[key] = std::make_shared<doc_decoder_type>();
            key2doc.at(key)->process_document_from_file(filename, password);
          }
        catch(const std::exception& exc)
          {
            key2doc.erase(key);
            LOG_S(ERROR) << "could not decode file object for key=" << key;
            return false;
          }

        try
          {
            key2scheduled_pages[key] = normalise_page_numbers(key,
                                                              key2doc.at(key)->get_number_of_pages(),
                                                              page_numbers);
          }
        catch(const std::exception& exc)
          {
            key2doc.erase(key);
            key2scheduled_pages.erase(key);
            throw;
          }
        return true;
      }

    LOG_S(ERROR) << "File not found: " << filename;
    return false;
  }

  template<typename Derived, typename ResultType>
  bool docling_threaded_base<Derived, ResultType>::load_document_from_bytesio(
      std::string key,
      pybind11::object bytes_io,
      std::optional<std::string> password,
      std::optional<std::vector<int>> page_numbers)
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
      }
    catch(const std::exception& exc)
      {
        key2doc.erase(key);
        key2scheduled_pages.erase(key);
        LOG_S(ERROR) << "could not decode bytesio object for key=" << key;
        return false;
      }

    try
      {
        key2scheduled_pages[key] = normalise_page_numbers(key,
                                                          key2doc.at(key)->get_number_of_pages(),
                                                          page_numbers);
      }
    catch(const std::exception& exc)
      {
        key2doc.erase(key);
        key2scheduled_pages.erase(key);
        throw;
      }
    return true;
  }

  template<typename Derived, typename ResultType>
  int docling_threaded_base<Derived, ResultType>::number_of_pages(std::string key) const
  {
    auto itr = key2doc.find(key);
    if(itr == key2doc.end())
      {
        throw std::runtime_error("Document key not found: " + key);
      }

    return itr->second->get_number_of_pages();
  }

  template<typename Derived, typename ResultType>
  int docling_threaded_base<Derived, ResultType>::scheduled_number_of_pages(std::string key) const
  {
    auto itr = key2scheduled_pages.find(key);
    if(itr == key2scheduled_pages.end())
      {
        throw std::runtime_error("Document key not found: " + key);
      }

    return static_cast<int>(itr->second.size());
  }

  template<typename Derived, typename ResultType>
  bool docling_threaded_base<Derived, ResultType>::unload_document(std::string key)
  {
    validate_unload_state();

    bool removed_doc = key2doc.erase(key) > 0;
    bool removed_schedule = key2scheduled_pages.erase(key) > 0;

    if(key2doc.empty())
      {
        reset_after_completion();
      }

    return removed_doc || removed_schedule;
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::unload_all_documents()
  {
    validate_unload_state();
    key2doc.clear();
    key2scheduled_pages.clear();
    reset_after_completion();
  }

  template<typename Derived, typename ResultType>
  std::vector<int> docling_threaded_base<Derived, ResultType>::normalise_page_numbers(
      const std::string& key,
      int num_pages,
      std::optional<std::vector<int>> page_numbers) const
  {
    std::vector<int> scheduled_pages;

    if(not page_numbers.has_value())
      {
        scheduled_pages.reserve(num_pages);
        for(int page = 0; page < num_pages; ++page)
          {
            scheduled_pages.push_back(page);
          }
        return scheduled_pages;
      }

    scheduled_pages.reserve(page_numbers->size());
    for(int page_number : *page_numbers)
      {
        if(page_number < 1 or page_number > num_pages)
          {
            throw std::runtime_error("Invalid page number " + std::to_string(page_number)
                                     + " for document key " + key
                                     + " with " + std::to_string(num_pages) + " pages");
          }
        scheduled_pages.push_back(page_number - 1);
      }

    std::sort(scheduled_pages.begin(), scheduled_pages.end());
    scheduled_pages.erase(std::unique(scheduled_pages.begin(), scheduled_pages.end()),
                          scheduled_pages.end());
    return scheduled_pages;
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::validate_unload_state() const
  {
    if(tasks_remaining.load() > 0)
      {
        throw std::runtime_error("Cannot unload documents while threaded iteration is active");
      }
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::reset_after_completion()
  {
    while(not task_queue.empty())
      {
        task_queue.pop();
      }

    while(not results_queue.empty())
      {
        results_queue.pop();
      }

    for(auto& worker : workers)
      {
        if(worker.joinable())
          {
            worker.join();
          }
      }
    workers.clear();

    tasks_remaining.store(0);
    active_workers.store(0);
    total_processed_pages.store(0);
    started.store(false);
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::build_task_queue()
  {
    for(const auto& pair : key2scheduled_pages)
      {
        for(int page : pair.second)
          {
            task_queue.push(std::make_pair(pair.first, page));
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
  void docling_threaded_base<Derived, ResultType>::maybe_release_native_memory()
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
