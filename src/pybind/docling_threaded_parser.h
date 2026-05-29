//-*-C++-*-

#ifndef PYBIND_THREADED_PDF_PARSER_H
#define PYBIND_THREADED_PDF_PARSER_H

#include <chrono>

#include <pybind/docling_threaded_base.h>

namespace docling
{
  class docling_threaded_parser :
    public docling_threaded_base<docling_threaded_parser, page_decode_result>
  {
  public:

    docling_threaded_parser(std::string loglevel,
                            int num_threads,
                            int max_concurrent_results,
                            pdflib::decode_config config):
      docling_threaded_base<docling_threaded_parser, page_decode_result>(loglevel,
                                                                         num_threads,
                                                                         max_concurrent_results,
                                                                         config)
    {}

    void worker_loop();
  };

  inline void docling_threaded_parser::worker_loop()
  {
    using clock_type = std::chrono::steady_clock;

    while(true)
      {
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

                auto total_start = clock_type::now();

                auto stage_start = clock_type::now();
                auto page_decoder = doc_decoder->make_thread_safe_page_decoder(page_number);
                result.timings.make_page_decoder_s
                  = std::chrono::duration<double>(clock_type::now() - stage_start).count();

                stage_start = clock_type::now();
                page_decoder->decode_page(config);
                result.timings.decode_page_s
                  = std::chrono::duration<double>(clock_type::now() - stage_start).count();

                if(config.create_word_cells)
                  {
                    stage_start = clock_type::now();
                    page_decoder->create_word_cells(config);
                    result.timings.create_word_cells_s
                      = std::chrono::duration<double>(clock_type::now() - stage_start).count();
                  }

                if(config.create_line_cells)
                  {
                    stage_start = clock_type::now();
                    page_decoder->create_line_cells(config);
                    result.timings.create_line_cells_s
                      = std::chrono::duration<double>(clock_type::now() - stage_start).count();
                  }

                result.timings.total_s
                  = std::chrono::duration<double>(clock_type::now() - total_start).count();
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

        {
          std::unique_lock<std::mutex> lock(results_mutex);

          cv_results_consumed.wait(lock, [this]() {
            return static_cast<int>(results_queue.size()) < max_concurrent_results;
          });

          results_queue.push(std::move(result));
          cv_results_available.notify_one();
        }

        maybe_release_native_memory();
      }

    active_workers.fetch_sub(1);

    cv_results_available.notify_all();
  }

}

#endif
