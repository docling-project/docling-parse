//-*-C++-*-

#ifndef PYBIND_THREADED_PDF_RENDERER_H
#define PYBIND_THREADED_PDF_RENDERER_H

#include <chrono>
#include <memory>
#include <vector>

#include <pybind/docling_threaded_base.h>
#include <render/blend2d_renderer.h>

namespace docling
{
  class docling_threaded_renderer :
    public docling_threaded_base<docling_threaded_renderer, page_render_result>
  {
  public:

    docling_threaded_renderer(std::string loglevel,
                              int num_threads,
                              int max_concurrent_results,
                              pdflib::decode_config decode_config,
                              pdflib::render_config render_config);

    void worker_loop();

  private:

    pdflib::render_config render_cfg;

    // Shared across workers; pages keep only their tiny local alias cache.
    std::shared_ptr<pdflib::blend2d_font_resolver> font_resolver_;

    // Shared across workers so embedded font programs are loaded once per
    // process instead of once per page. Safe across documents: entries are
    // keyed by a content hash of the font bytes, not by PDF object ids.
    std::shared_ptr<pdflib::blend2d_embedded_font_cache> embedded_font_cache_;

    // Same sharing/keying for the Type 1 / bare CFF programs that Blend2D
    // cannot load; FreeType serializes internally on its own mutex.
    std::shared_ptr<pdflib::freetype_embedded_font_cache> freetype_font_cache_;
  };

  inline docling_threaded_renderer::docling_threaded_renderer(std::string loglevel,
                                                              int num_threads,
                                                              int max_concurrent_results,
                                                              pdflib::decode_config decode_config,
                                                              pdflib::render_config render_config):
    docling_threaded_base<docling_threaded_renderer, page_render_result>(loglevel,
                                                                         num_threads,
                                                                         max_concurrent_results,
                                                                         decode_config),
    render_cfg(render_config),
    font_resolver_(std::make_shared<pdflib::blend2d_font_resolver>()),
    embedded_font_cache_(std::make_shared<pdflib::blend2d_embedded_font_cache>()),
    freetype_font_cache_(std::make_shared<pdflib::freetype_embedded_font_cache>())
  {
    font_resolver_->warm();

    // This pipeline always renders, so embedded font programs must be
    // extracted during page decoding; parse-only pipelines leave this off.
    config.extract_font_programs = true;
  }

  inline void docling_threaded_renderer::worker_loop()
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

                auto total_start = clock_type::now();

                auto stage_start = clock_type::now();
                auto page_decoder = doc_decoder->make_thread_safe_page_decoder(
                  page_number,
                  config.keep_qpdf_warnings);
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

                stage_start = clock_type::now();
                pdflib::renderer<pdflib::BLEND2D> rnd(render_cfg,
                                                      font_resolver_,
                                                      embedded_font_cache_,
                                                      freetype_font_cache_);
                page_decoder->get_instructions().iterate_over_instructions(rnd);
                result.timings.render_page_s
                  = std::chrono::duration<double>(clock_type::now() - stage_start).count();

                result.timings.total_s
                  = std::chrono::duration<double>(clock_type::now() - total_start).count();
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
