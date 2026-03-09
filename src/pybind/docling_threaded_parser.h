//-*-C++-*-

#ifndef PYBIND_THREADED_PDF_PARSER_H
#define PYBIND_THREADED_PDF_PARSER_H

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

  class docling_threaded_parser: public docling_resources
  {
    typedef pdflib::pdf_decoder<pdflib::PAGE> page_decoder_type;
    typedef pdflib::pdf_decoder<pdflib::DOCUMENT> doc_decoder_type;

    typedef std::shared_ptr<page_decoder_type> page_decoder_ptr_type;
    typedef std::shared_ptr<doc_decoder_type> doc_decoder_ptr_type;

  public:

    docling_threaded_parser(std::string loglevel,
                            int num_threads,
                            int max_concurrent_results,
                            pdflib::decode_config config);

    ~docling_threaded_parser();

    bool load_document(std::string key,
                       std::string filename,
                       std::optional<std::string> password);

    bool load_document_from_bytesio(std::string key,
                                    pybind11::object bytes_io,
                                    std::optional<std::string> password);

    bool has_tasks();

    page_decode_result get_task();

  private:

    void set_loglevel_with_label(std::string level);

    void build_task_queue();

    void start_workers();

    void worker_loop();

  private:

    std::string pdf_resources_dir;

    pdflib::decode_config config;
    int num_threads;
    int max_concurrent_results;

    std::unordered_map<std::string, doc_decoder_ptr_type> key2doc;

    // Task queue: (doc_key, page_number) pairs
    std::queue<std::pair<std::string, int>> task_queue;
    std::mutex task_mutex;

    // Results queue with bounded capacity
    std::queue<page_decode_result> results_queue;
    std::mutex results_mutex;
    std::condition_variable cv_results_available;
    std::condition_variable cv_results_consumed;

    // State tracking
    std::atomic<int> tasks_remaining{0};
    std::atomic<bool> started{false};
    std::atomic<int> active_workers{0};

    std::vector<std::thread> workers;
  };

  docling_threaded_parser::docling_threaded_parser(std::string loglevel,
                                                    int num_threads,
                                                    int max_concurrent_results,
                                                    pdflib::decode_config config):
    docling_resources(),
    pdf_resources_dir(resource_utils::get_resources_dir(true).string()),
    config(config),
    num_threads(num_threads),
    max_concurrent_results(max_concurrent_results),
    key2doc({})
  {
    set_loglevel_with_label(loglevel);

    // Force thread-safe mode since we're doing parallel decoding
    this->config.do_thread_safe = true;

    LOG_S(WARNING) << "pdf_resources_dir: " << pdf_resources_dir;

    auto RESOURCE_DIR_KEY = pdflib::pdf_resource<pdflib::PAGE_FONT>::RESOURCE_DIR_KEY;

    nlohmann::json data = nlohmann::json::object({});
    data[RESOURCE_DIR_KEY] = pdf_resources_dir;

    std::unordered_map<std::string, double> timings = {};
    pdflib::pdf_resource<pdflib::PAGE_FONT>::initialise(data, timings);
  }

  docling_threaded_parser::~docling_threaded_parser()
  {
    // Wait for all worker threads to finish
    for(auto& worker : workers)
      {
        if(worker.joinable())
          {
            worker.join();
          }
      }
  }

  void docling_threaded_parser::set_loglevel_with_label(std::string level)
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

  bool docling_threaded_parser::load_document(std::string key,
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

  bool docling_threaded_parser::load_document_from_bytesio(std::string key,
                                                            pybind11::object bytes_io,
                                                            std::optional<std::string> password)
  {
    if(started.load())
      {
        LOG_S(ERROR) << "Cannot load documents after processing has started";
        return false;
      }

    LOG_S(INFO) << __FILE__ << ":" << __LINE__ << "\t" << __FUNCTION__;

    if(!pybind11::hasattr(bytes_io, "read"))
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

  void docling_threaded_parser::build_task_queue()
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

  void docling_threaded_parser::start_workers()
  {
    int num_workers = std::min(num_threads, static_cast<int>(task_queue.size()));
    active_workers.store(num_workers);

    for(int i = 0; i < num_workers; i++)
      {
        workers.emplace_back(&docling_threaded_parser::worker_loop, this);
      }
  }

  bool docling_threaded_parser::has_tasks()
  {
    if(!started.load())
      {
        build_task_queue();
        start_workers();
        started.store(true);
      }

    return tasks_remaining.load() > 0;
  }

  page_decode_result docling_threaded_parser::get_task()
  {
    std::unique_lock<std::mutex> lock(results_mutex);

    // Wait until a result is available or all workers are done
    cv_results_available.wait(lock, [this]() {
      return !results_queue.empty() || active_workers.load() == 0;
    });

    if(results_queue.empty())
      {
        // All workers done and no more results — should not happen if has_tasks() was checked
        page_decode_result empty_result;
        empty_result.doc_key = "";
        empty_result.page_number = -1;
        empty_result.success = false;
        empty_result.error_message = "No more tasks available";
        return empty_result;
      }

    page_decode_result result = std::move(results_queue.front());
    results_queue.pop();
    tasks_remaining.fetch_sub(1);

    lock.unlock();

    // Notify workers that space is available in the results queue
    cv_results_consumed.notify_one();

    return result;
  }

  void docling_threaded_parser::worker_loop()
  {
    while(true)
      {
        // Pop a task from the task queue
        std::pair<std::string, int> task;
        {
          std::lock_guard<std::mutex> lock(task_mutex);

          if(task_queue.empty())
            {
              break;
            }

          task = task_queue.front();
          task_queue.pop();
        }

        const std::string& doc_key = task.first;
        int page_number = task.second;

        // Decode the page (thread-safe: each page gets its own QPDF instance)
        page_decode_result result;
        result.doc_key = doc_key;
        result.page_number = page_number;

        try
          {
            auto itr = key2doc.find(doc_key);
            if(itr == key2doc.end())
              {
                result.success = false;
                result.error_message = "Document key not found: " + doc_key;
              }
            else
              {
                auto& doc_decoder = itr->second;

                // Create a thread-safe page decoder directly
                auto page_decoder = std::make_shared<page_decoder_type>(
                    doc_decoder->get_buffer(),
                    doc_decoder->get_password(),
                    page_number);

                page_decoder->decode_page(config);

                if(config.create_word_cells)
                  {
                    page_decoder->create_word_cells(config);
                  }

                if(config.create_line_cells)
                  {
                    page_decoder->create_line_cells(config);
                  }

                result.success = true;
                result.page_decoder = page_decoder;
              }
          }
        catch(const std::exception& exc)
          {
            result.success = false;
            result.error_message = "Error decoding page " + std::to_string(page_number)
              + " of " + doc_key + ": " + exc.what();
          }

        // Push result to the bounded results queue
        {
          std::unique_lock<std::mutex> lock(results_mutex);

          // Block if results queue is full (backpressure)
          cv_results_consumed.wait(lock, [this]() {
            return static_cast<int>(results_queue.size()) < max_concurrent_results;
          });

          results_queue.push(std::move(result));
          cv_results_available.notify_one();
        }
      }

    // Worker is done
    active_workers.fetch_sub(1);

    // Wake up get_task() in case it's waiting and all workers are now done
    cv_results_available.notify_all();
  }

}

#endif
