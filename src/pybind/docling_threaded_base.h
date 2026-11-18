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
      auto it = key2doc.find(key);
      if(it == key2doc.end()) { throw std::runtime_error("Document key not found: " + key); }
      return it->second;
    }

    bool unload_document(std::string key);
    void unload_all_documents();
    int cancel_document(const std::string& key);

    int get_number_of_active_workers() const;
    int get_number_of_scheduled_tasks() const;
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
    void validate_unload_state();
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

    std::unordered_map<std::string, doc_decoder_ptr_type> key2doc;
    std::unordered_map<std::string, std::vector<int>> key2scheduled_pages;

    // Failed loads by key, until Python collects the reason.
    std::unordered_map<std::string, std::map<std::string, std::string>> load_errors;

  private:

    // The GIL serializes Python load, unload, cancel and startup calls.
    bool started = false;

    // Queue changes and condition-variable predicates share this mutex.
    mutable std::mutex queue_mutex;
    bool shutdown_requested = false;
    std::deque<std::pair<std::string, int>> task_queue;
    std::deque<ResultType> results_queue;
    std::unordered_set<std::string> cancelled_keys;
    std::condition_variable cv_results_available;
    std::condition_variable cv_results_consumed;

    // Pending, in-flight and buffered pages count until consumed or discarded.
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
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      if(shutdown_requested)
        {
          return;
        }
      shutdown_requested = true;
      tasks_remaining -= static_cast<int>(task_queue.size() + results_queue.size());
      task_queue.clear();
      results_queue.clear();
    }

    cv_results_consumed.notify_all();
    cv_results_available.notify_all();
    join_workers();
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::join_workers()
  {
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
    if(started)
      {
        LOG_S(ERROR) << "Cannot load documents after processing has started";
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
    if(started)
      {
        LOG_S(ERROR) << "Cannot load documents after processing has started";
        return false;
      }

    load_errors.erase(key);

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
    auto itr = key2doc.find(key);
    if(itr == key2doc.end())
      {
        throw std::runtime_error("Document key not found: " + key);
      }

    return itr->second->get_number_of_pages();
  }

  template<typename Derived, typename ResultType>
  typename docling_threaded_base<Derived, ResultType>::doc_decoder_ptr_type
  docling_threaded_base<Derived, ResultType>::find_document(const std::string& key) const
  {
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
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      cancelled_keys.erase(key);
    }

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
  int docling_threaded_base<Derived, ResultType>::cancel_document(const std::string& key)
  {
    const auto schedule = key2scheduled_pages.find(key);
    if(schedule == key2scheduled_pages.end() or not key2doc.contains(key))
      {
        return 0;
      }

    // Keep evicted payloads alive until after the queue lock is released.
    std::vector<ResultType> evicted;
    std::unique_lock<std::mutex> lock(queue_mutex);
    if(cancelled_keys.contains(key))
      {
        return 0;
      }
    if(not started)
      {
        cancelled_keys.insert(key);
        return static_cast<int>(schedule->second.size());
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

    const int removed = static_cast<int>(removed_tasks + evicted.size());
    tasks_remaining -= removed;
    lock.unlock();
    cv_results_consumed.notify_all();
    cv_results_available.notify_all();
    return removed;
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
  void docling_threaded_base<Derived, ResultType>::validate_unload_state()
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    if(tasks_remaining > 0)
      {
        throw std::runtime_error("Cannot unload documents while threaded iteration is active");
      }
  }

  template<typename Derived, typename ResultType>
  void docling_threaded_base<Derived, ResultType>::reset_after_completion()
  {
    join_workers();

    std::lock_guard<std::mutex> lock(queue_mutex);
    task_queue.clear();
    results_queue.clear();
    cancelled_keys.clear();
    tasks_remaining = 0;
    active_workers = 0;
    total_processed_pages.store(0);
    started = false;
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
  int docling_threaded_base<Derived, ResultType>::get_number_of_active_workers() const
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return active_workers;
  }

  template<typename Derived, typename ResultType>
  int docling_threaded_base<Derived, ResultType>::get_number_of_scheduled_tasks() const
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return tasks_remaining;
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
    if(shutdown_requested)
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

    if(shutdown_requested or cancelled_keys.contains(result.doc_key))
      {
        --tasks_remaining;
        lock.unlock();
        cv_results_available.notify_all();
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

    cv_results_available.wait(lock, [this]() {
      return shutdown_requested or not results_queue.empty()
        or tasks_remaining == 0 or active_workers == 0;
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
