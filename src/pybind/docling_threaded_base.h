//-*-C++-*-
// SPDX-License-Identifier: MIT

#ifndef PYBIND_THREADED_PDF_BASE_H
#define PYBIND_THREADED_PDF_BASE_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <locale>
#include <codecvt>
#endif

#include <parse.h>
#include <pybind11/gil.h>

#include <pybind/native_memory.h>
#include <pybind/docling_resources.h>
#include <pybind/docling_threaded_results.h>

namespace docling
{
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
  // Its destructor must call shutdown_workers() before destroying worker state.
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
                       std::optional<std::vector<int>> page_numbers = std::nullopt,
                       std::optional<std::pair<std::int64_t, std::int64_t>> page_range = std::nullopt);

    bool load_document_from_bytesio(std::string key,
                                    pybind11::object bytes_io,
                                    std::optional<std::string> password,
                                    std::optional<std::vector<int>> page_numbers = std::nullopt,
                                    std::optional<std::pair<std::int64_t, std::int64_t>> page_range = std::nullopt);

    int number_of_pages(std::string key) const;
    int scheduled_number_of_pages(std::string key) const;

    // Return and clear why the last load of `key` failed:
    // {"category": ..., "message": ...}, or an empty map.
    std::map<std::string, std::string> take_load_error(std::string key);

    // The decoder loaded under `key`; throws when there is none. A binding
    // holds the pointer across a GIL release, so a document unloaded
    // meanwhile stays alive until the binding returns.
    doc_decoder_ptr_type find_document(const std::string& key) const;

    doc_decoder_ptr_type get_document_decoder(const std::string& key) const
    {
      return find_document(key);
    }

    bool unload_document(std::string key);
    void unload_all_documents();
    void cancel_document(const std::string& key);

    bool all_scheduled_work_completed() const;

    bool has_tasks();

    ResultType get_task();

    // The config the workers decode with, after any pipeline overrides.
    const pdflib::decode_config& get_decode_config() const;

  private:

    void set_loglevel_with_label(std::string level);
    std::vector<int> normalise_page_selection(
        const std::string& key,
        int num_pages,
        std::optional<std::vector<int>> page_numbers,
        std::optional<std::pair<std::int64_t, std::int64_t>> page_range) const;
    // The caller holds queue_mutex for lifecycle validation and reset.
    bool validate_load_state(const std::string& key);
    void validate_unload_state() const;
    void join_workers_for_unload(std::unique_lock<std::mutex>& lock);
    void record_load_error(const std::string& key,
                           const std::string& category,
                           const std::string& message);
    void reset_after_completion();
    void join_workers();
    void finish_worker();

    void build_task_queue();

    void start_workers();

  protected:

    void maybe_release_native_memory();
    std::optional<std::pair<std::string, int>> take_task();
    void publish_result(ResultType result);
    void shutdown_workers();

    pdflib::decode_config config;
    int num_threads;
    int max_concurrent_results;

    // Worker loops read key2doc without locking. It stays immutable from
    // startup until the workers have joined. Lifecycle callers use queue_mutex.
    std::unordered_map<std::string, doc_decoder_ptr_type> key2doc;
    std::unordered_map<std::string, std::vector<int>> key2scheduled_pages;

    // Failed loads by key, until Python collects the reason.
    std::unordered_map<std::string, std::map<std::string, std::string>> load_errors;

  private:

    // Lifecycle state, queues, counters and wait predicates share this mutex.
    mutable std::mutex queue_mutex;
    bool started = false;
    bool unloading = false;
    bool shutdown_requested = false;
    std::uint64_t batch_generation = 0;
    std::deque<std::pair<std::string, int>> task_queue;
    std::deque<ResultType> results_queue;
    std::unordered_set<std::string> cancelled_keys;
    std::condition_variable cv_results_available;
    std::condition_variable cv_results_consumed;

    // Claimed pages remain here until published or discarded.
    std::unordered_map<std::string, int> claimed_pages;

    // Queued, claimed and buffered pages count while a result can be delivered.
    int tasks_remaining = 0;
    int active_workers = 0;
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

    std::string pdf_resources_dir = utils::filesystem::to_utf8(resource_utils::get_resources_dir(true));
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
    shutdown_workers();
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::shutdown_workers()
  {
    std::deque<ResultType> evicted;
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      if(shutdown_requested)
        {
          return;
        }
      shutdown_requested = true;
      tasks_remaining = 0;
      task_queue.clear();
      results_queue.swap(evicted);
    }

    cv_results_consumed.notify_all();
    cv_results_available.notify_all();
    evicted.clear();
    join_workers();
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::join_workers()
  {
    if(workers.empty())
      {
        return;
      }

    std::optional<pybind11::gil_scoped_release> release;
    if(Py_IsInitialized() and PyGILState_Check())
      {
#if PY_VERSION_HEX >= 0x030D0000
        const bool finalizing = Py_IsFinalizing();
#else
        const bool finalizing = _Py_IsFinalizing();
#endif
        if(not finalizing)
          {
            release.emplace();
          }
      }

    for(auto& worker : workers)
      {
        if(worker.joinable())
          {
            worker.join();
          }
      }
    workers.clear();
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
      std::optional<std::vector<int>> page_numbers,
      std::optional<std::pair<std::int64_t, std::int64_t>> page_range)
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    if(not validate_load_state(key))
      {
        return false;
      }

    load_errors.erase(key);

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
            bool success = key2doc.at(key)->process_document_from_file(filename,
                                                                       password,
                                                                       config.keep_qpdf_warnings);
            if(not success)
              {
                // do not keep a decoder that never parsed the document: it
                // would schedule zero pages and report a page count of -1
                record_load_error(key,
                                  key2doc.at(key)->get_load_error_category(),
                                  key2doc.at(key)->get_load_error_message());
                key2doc.erase(key);
                LOG_S(ERROR) << "could not decode file object for key=" << key;
                return false;
              }
          }
        catch(const std::exception& exc)
          {
            record_load_error(key, pdflib::load_error::UNKNOWN, exc.what());
            key2doc.erase(key);
            LOG_S(ERROR) << "could not decode file object for key=" << key;
            return false;
          }

        try
          {
            key2scheduled_pages[key] = normalise_page_selection(
                key,
                key2doc.at(key)->get_number_of_pages(),
                page_numbers,
                page_range);
          }
        catch(const std::exception& exc)
          {
            key2doc.erase(key);
            key2scheduled_pages.erase(key);
            throw;
          }
        cancelled_keys.erase(key);
        return true;
      }

    record_load_error(key, pdflib::load_error::IO, "file not found: " + filename);
    LOG_S(ERROR) << "File not found: " << filename;
    return false;
  }

  template<typename Derived, typename ResultType>
  bool docling_threaded_base<Derived, ResultType>::load_document_from_bytesio(
      std::string key,
      pybind11::object bytes_io,
      std::optional<std::string> password,
      std::optional<std::vector<int>> page_numbers,
      std::optional<std::pair<std::int64_t, std::int64_t>> page_range)
  {
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      if(not validate_load_state(key))
        {
          return false;
        }
    }

    LOG_S(INFO) << __FILE__ << ":" << __LINE__ << "\t" << __FUNCTION__;

    if(not pybind11::hasattr(bytes_io, "read"))
      {
        throw std::runtime_error("Expected a BytesIO object");
      }

    bytes_io.attr("seek")(0);

    pybind11::bytes data = bytes_io.attr("read")();

    auto data_buffer = std::make_shared<std::string>(data.cast<std::string>());

    // A Python stream callback can reenter the parser or allow another thread
    // to start the batch. Check again before touching the document maps.
    std::lock_guard<std::mutex> lock(queue_mutex);
    if(not validate_load_state(key))
      {
        return false;
      }
    load_errors.erase(key);

    try
      {
        key2doc[key] = std::make_shared<doc_decoder_type>();
        std::string description = "parsing of " + key + " from bytesio";
        bool success = key2doc.at(key)->process_document_from_bytesio(data_buffer,
                                                                      password,
                                                                      description,
                                                                      config.keep_qpdf_warnings);
        if(not success)
          {
            // do not keep a decoder that never parsed the document: it would
            // schedule zero pages and report a page count of -1
            record_load_error(key,
                              key2doc.at(key)->get_load_error_category(),
                              key2doc.at(key)->get_load_error_message());
            key2doc.erase(key);
            key2scheduled_pages.erase(key);
            LOG_S(ERROR) << "could not decode bytesio object for key=" << key;
            return false;
          }
      }
    catch(const std::exception& exc)
      {
        record_load_error(key, pdflib::load_error::UNKNOWN, exc.what());
        key2doc.erase(key);
        key2scheduled_pages.erase(key);
        LOG_S(ERROR) << "could not decode bytesio object for key=" << key;
        return false;
      }

    try
      {
        key2scheduled_pages[key] = normalise_page_selection(
            key,
            key2doc.at(key)->get_number_of_pages(),
            page_numbers,
            page_range);
      }
    catch(const std::exception& exc)
      {
        key2doc.erase(key);
        key2scheduled_pages.erase(key);
        throw;
      }
    cancelled_keys.erase(key);
    return true;
  }

  template<typename Derived, typename ResultType>
  std::map<std::string, std::string>
  docling_threaded_base<Derived, ResultType>::take_load_error(std::string key)
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    std::map<std::string, std::string> result;

    auto itr = load_errors.find(key);
    if(itr != load_errors.end())
      {
        result = itr->second;
        load_errors.erase(itr);
      }

    return result;
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::record_load_error(const std::string& key,
                                                                     const std::string& category,
                                                                     const std::string& message)
  {
    load_errors[key] = {{"category", category}, {"message", message}};
  }

  template<typename Derived, typename ResultType>
  int docling_threaded_base<Derived, ResultType>::number_of_pages(std::string key) const
  {
    return find_document(key)->get_number_of_pages();
  }

  template<typename Derived, typename ResultType>
  typename docling_threaded_base<Derived, ResultType>::doc_decoder_ptr_type
  docling_threaded_base<Derived, ResultType>::find_document(const std::string& key) const
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    auto itr = key2doc.find(key);
    if(itr == key2doc.end())
      {
        throw std::runtime_error("Document key not found: " + key);
      }

    return itr->second;
  }

  template<typename Derived, typename ResultType>
  int docling_threaded_base<Derived, ResultType>::scheduled_number_of_pages(std::string key) const
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
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
    std::unique_lock<std::mutex> lock(queue_mutex);
    join_workers_for_unload(lock);

    bool removed_doc = key2doc.erase(key) > 0;
    bool removed_schedule = key2scheduled_pages.erase(key) > 0;
    cancelled_keys.erase(key);
    claimed_pages.erase(key);

    if(key2doc.empty())
      {
        reset_after_completion();
      }

    return removed_doc || removed_schedule;
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::unload_all_documents()
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    join_workers_for_unload(lock);
    key2doc.clear();
    key2scheduled_pages.clear();
    reset_after_completion();
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::cancel_document(const std::string& key)
  {
    // Keep evicted payloads alive until after the queue lock is released.
    std::vector<ResultType> evicted;
    std::unique_lock<std::mutex> lock(queue_mutex);
    if(not key2doc.contains(key) or not key2scheduled_pages.contains(key) or cancelled_keys.contains(key))
      {
        return;
      }

    evicted.reserve(results_queue.size());
    cancelled_keys.insert(key);
    const auto removed_tasks = std::erase_if(task_queue, [&key](const auto& task) {
      return task.first == key;
    });

    auto kept = results_queue.begin();
    for(auto it = results_queue.begin(); it != results_queue.end(); ++it)
      {
        if(it->doc_key == key)
          {
            evicted.push_back(std::move(*it));
          }
        else
          {
            if(kept != it)
              {
                *kept = std::move(*it);
              }
            ++kept;
          }
      }
    results_queue.erase(kept, results_queue.end());

    tasks_remaining -= static_cast<int>(removed_tasks + evicted.size());
    if(auto claimed = claimed_pages.find(key); claimed != claimed_pages.end())
      {
        tasks_remaining -= claimed->second;
      }
    lock.unlock();
    cv_results_consumed.notify_all();
    cv_results_available.notify_all();
  }

  template<typename Derived, typename ResultType>
  std::vector<int> docling_threaded_base<Derived, ResultType>::normalise_page_selection(
      const std::string& key,
      int num_pages,
      std::optional<std::vector<int>> page_numbers,
      std::optional<std::pair<std::int64_t, std::int64_t>> page_range) const
  {
    std::vector<int> scheduled_pages;

    if(num_pages < 0)
      {
        // a negative count means the document was never parsed; scheduling
        // from it would silently produce an empty task list
        throw std::runtime_error("Invalid page count " + std::to_string(num_pages)
                                 + " for document key " + key);
      }

    if(page_numbers.has_value() and page_range.has_value())
      {
        throw std::runtime_error("page_numbers and page_range are mutually exclusive");
      }

    if(page_range.has_value())
      {
        auto [start_page, end_page] = page_range.value();
        if(start_page < 1)
          {
            throw std::runtime_error("Invalid page range start "
                                     + std::to_string(start_page)
                                     + " for document key " + key);
          }
        if(end_page < start_page)
          {
            throw std::runtime_error("Invalid page range ["
                                     + std::to_string(start_page) + ", "
                                     + std::to_string(end_page)
                                     + "] for document key " + key);
          }

        std::int64_t clipped_end_page = std::min<std::int64_t>(end_page, num_pages);
        if(start_page > clipped_end_page)
          {
            return scheduled_pages;
          }

        scheduled_pages.reserve(
            static_cast<std::size_t>(clipped_end_page - start_page + 1));
        for(std::int64_t page_number = start_page;
            page_number <= clipped_end_page;
            ++page_number)
          {
            scheduled_pages.push_back(static_cast<int>(page_number - 1));
          }
        return scheduled_pages;
      }

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
  bool docling_threaded_base<Derived, ResultType>::validate_load_state(const std::string& key)
  {
    if(started or unloading or shutdown_requested)
      {
        const char* message = "Cannot load documents while a threaded batch is active or closing";
        record_load_error(key, pdflib::load_error::UNKNOWN, message);
        LOG_S(ERROR) << message;
        return false;
      }
    return true;
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::validate_unload_state() const
  {
    if(unloading)
      {
        throw std::runtime_error("Cannot unload documents while another unload is in progress");
      }
    if(tasks_remaining > 0)
      {
        throw std::runtime_error("Cannot unload documents while threaded iteration is active");
      }
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::join_workers_for_unload(
      std::unique_lock<std::mutex>& lock)
  {
    validate_unload_state();
    if(workers.empty())
      {
        return;
      }

    unloading = true;
    lock.unlock();
    join_workers();
    lock.lock();
    unloading = false;
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::reset_after_completion()
  {
    // Both queues are empty and the last worker has joined.
    cancelled_keys.clear();
    claimed_pages.clear();
    total_processed_pages.store(0);
    started = false;
    ++batch_generation;
    cv_results_available.notify_all();
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::build_task_queue()
  {
    for(const auto& pair : key2scheduled_pages)
      {
        if(cancelled_keys.contains(pair.first))
          {
            continue;
          }
        claimed_pages.try_emplace(pair.first, 0);
        for(int page : pair.second)
          {
            task_queue.emplace_back(pair.first, page);
          }
      }

    tasks_remaining = static_cast<int>(task_queue.size());
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::start_workers()
  {
    int num_workers = std::min(num_threads, static_cast<int>(task_queue.size()));
    for(int i = 0; i < num_workers; i++)
      {
        workers.emplace_back([this]() {
          static_cast<Derived*>(this)->worker_loop();
          finish_worker();
        });
        ++active_workers;
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
  bool docling_threaded_base<Derived, ResultType>::all_scheduled_work_completed() const
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return active_workers == 0 and tasks_remaining == 0;
  }

  template<typename Derived, typename ResultType>
  bool docling_threaded_base<Derived, ResultType>::has_tasks()
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    if(unloading or shutdown_requested)
      {
        return false;
      }
    if(not started)
      {
        build_task_queue();
        started = true;
        start_workers();
      }

    return tasks_remaining > 0;
  }

  template<typename Derived, typename ResultType>
  std::optional<std::pair<std::string, int>>
  docling_threaded_base<Derived, ResultType>::take_task()
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    if(task_queue.empty())
      {
        return std::nullopt;
      }

    ++claimed_pages.at(task_queue.front().first);
    auto task = std::move(task_queue.front());
    task_queue.pop_front();
    return task;
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::publish_result(ResultType result)
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    cv_results_consumed.wait(lock, [this, &result]() {
      return shutdown_requested or cancelled_keys.contains(result.doc_key)
        or static_cast<int>(results_queue.size()) < max_concurrent_results;
    });

    --claimed_pages.at(result.doc_key);
    if(shutdown_requested or cancelled_keys.contains(result.doc_key))
      {
        return;
      }

    results_queue.push_back(std::move(result));
    lock.unlock();
    cv_results_available.notify_one();
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::finish_worker()
  {
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      --active_workers;
    }
    cv_results_available.notify_all();
  }

  template<typename Derived, typename ResultType>
  ResultType docling_threaded_base<Derived, ResultType>::get_task()
  {
    std::unique_lock<std::mutex> lock(queue_mutex);

    const auto generation = batch_generation;
    cv_results_available.wait(lock, [this, generation]() {
      return generation != batch_generation or shutdown_requested or not results_queue.empty()
        or tasks_remaining == 0 or active_workers == 0;
    });

    if(generation != batch_generation or results_queue.empty())
      {
        ResultType empty_result;
        empty_result.doc_key = "";
        empty_result.page_number = -1;
        empty_result.success = false;
        empty_result.error_message = "No more tasks available";
        return empty_result;
      }

    ResultType result = std::move(results_queue.front());
    results_queue.pop_front();
    const bool completed = --tasks_remaining == 0;

    lock.unlock();

    cv_results_consumed.notify_one();
    if(completed)
      {
        cv_results_available.notify_all();
      }

    return result;
  }

  template<typename Derived, typename ResultType>
  const pdflib::decode_config& docling_threaded_base<Derived, ResultType>::get_decode_config() const
  {
    return config;
  }

}

#endif
