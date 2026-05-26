//-*-C++-*-

#ifndef PYBIND_THREADED_PDF_RENDERER_H
#define PYBIND_THREADED_PDF_RENDERER_H

#include <array>
#include <memory>
#include <vector>

#include <pybind/docling_threaded_base.h>
#include <render/blend2d_renderer.h>

namespace docling
{
  struct page_render_result : page_decode_result
  {
    // RGBA pixel data laid out as {height, width, 4} row-major top-to-bottom.
    // Suitable for direct consumption by PIL:
    //   Image.frombuffer("RGBA", (w, h), data, "raw", "RGBA", 0, 1)
    std::shared_ptr<std::vector<unsigned char>> image_data;
    std::array<int, 3> image_shape{0, 0, 4}; // {height, width, channels}
  };

  class docling_threaded_renderer :
    public docling_threaded_base<docling_threaded_renderer, page_render_result>
  {
  public:

    docling_threaded_renderer(std::string loglevel,
                              int num_threads,
                              int max_concurrent_results,
                              pdflib::decode_config decode_config,
                              pdflib::render_config render_config):
      docling_threaded_base<docling_threaded_renderer, page_render_result>(loglevel,
                                                                           num_threads,
                                                                           max_concurrent_results,
                                                                           decode_config),
      render_cfg(render_config)
    {}

    void worker_loop();

  private:

    pdflib::render_config render_cfg;
  };

  inline void docling_threaded_renderer::worker_loop()
  {
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

        page_render_result result;
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

                auto page_decoder = doc_decoder->make_thread_safe_page_decoder(page_number);

                page_decoder->decode_page(config);

                if(config.create_word_cells)
                  {
                    page_decoder->create_word_cells(config);
                  }

                if(config.create_line_cells)
                  {
                    page_decoder->create_line_cells(config);
                  }

                pdflib::renderer<pdflib::BLEND2D> rnd(render_cfg);
                page_decoder->get_instructions().iterate_over_instructions(rnd);

                result.success = true;
                result.page_decoder = page_decoder;
                result.image_data   = rnd.get_canvas();
                result.image_shape  = rnd.get_shape();
              }
          }
        catch(const std::exception& exc)
          {
            result.success = false;
            result.error_message = "Error rendering page " + std::to_string(page_number)
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
