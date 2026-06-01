//-*-C++-*-

#include "parse.h"
#include "render.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
  using clock_type = std::chrono::steady_clock;
  using doc_decoder_type = pdflib::pdf_decoder<pdflib::DOCUMENT>;
  using page_decoder_type = pdflib::pdf_decoder<pdflib::PAGE>;
  using doc_decoder_ptr = std::shared_ptr<doc_decoder_type>;

  enum class run_mode
  {
    parse,
    render,
    both,
  };

  struct scheduled_doc
  {
    std::filesystem::path path;
    int total_pages = 0;
    std::vector<int> pages;
  };

  struct page_task
  {
    std::size_t doc_index = 0;
    int page_number = 0;
  };

  struct page_timings
  {
    double total_s = 0.0;
    double make_page_decoder_s = 0.0;
    double decode_page_s = 0.0;
    double create_word_cells_s = 0.0;
    double create_line_cells_s = 0.0;
    double render_page_s = 0.0;
  };

  struct page_result
  {
    std::string doc_key;
    int page_number = 0;
    bool success = false;
    std::string error_message;
    page_timings timings;
    std::shared_ptr<page_decoder_type> page_decoder;
    std::shared_ptr<std::vector<uint8_t>> image_data;
  };

  struct benchmark_result
  {
    int threads = 0;
    double wall_time_s = 0.0;
    int errors = 0;
  };

  struct cli_options
  {
    std::filesystem::path input;
    run_mode mode = run_mode::render;
    bool recursive = false;
    std::optional<int> max_pages = std::nullopt;
    int max_concurrent_results = 64;
    std::vector<int> threads{1, 2, 4, 8, 12, 16};
    float scale = 1.0f;
    bool enable_timing = false;
    std::filesystem::path timing_csv = "timing-cpp.csv";
    std::string loglevel = "fatal";
  };

  void set_loglevel(std::string level)
  {
    std::transform(level.begin(), level.end(), level.begin(),
                   [](unsigned char c) { return std::tolower(c); });

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

  std::string mode_to_string(run_mode mode)
  {
    switch(mode)
      {
      case run_mode::parse: return "parse";
      case run_mode::render: return "render";
      case run_mode::both: return "both";
      }
    return "render";
  }

  std::vector<int> parse_thread_counts(const std::string& raw)
  {
    std::vector<int> values;
    std::stringstream ss(raw);
    std::string token;

    while(std::getline(ss, token, ','))
      {
        if(token.empty())
          {
            continue;
          }

        int value = std::stoi(token);
        if(value <= 0)
          {
            throw std::runtime_error("--threads must contain positive integers");
          }
        values.push_back(value);
      }

    if(values.empty())
      {
        throw std::runtime_error("--threads must contain at least one value");
      }

    return values;
  }

  bool parse_bool(const std::string& raw)
  {
    std::string value = raw;
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if(value == "1" or value == "true" or value == "yes" or value == "on")
      {
        return true;
      }
    if(value == "0" or value == "false" or value == "no" or value == "off")
      {
        return false;
      }

    throw std::runtime_error("expected boolean value, got: " + raw);
  }

  std::string format_duration(double seconds)
  {
    const int total_seconds = static_cast<int>(seconds);
    const int hours = total_seconds / 3600;
    const int minutes = (total_seconds % 3600) / 60;
    const int secs = total_seconds % 60;

    std::ostringstream ss;
    if(hours > 0)
      {
        ss << hours << ":"
           << std::setw(2) << std::setfill('0') << minutes << ":"
           << std::setw(2) << std::setfill('0') << secs;
      }
    else
      {
        ss << minutes << ":"
           << std::setw(2) << std::setfill('0') << secs;
      }
    return ss.str();
  }

  class progress_bar
  {
  public:
    progress_bar(std::string label, int total):
      label_(std::move(label)),
      total_(std::max(0, total)),
      start_(clock_type::now()),
      last_draw_(start_)
    {
      draw(0, true);
    }

    void update(int current)
    {
      current_ = std::min(std::max(0, current), total_);
      const auto now = clock_type::now();
      const double since_last_draw =
        std::chrono::duration<double>(now - last_draw_).count();
      if(current_ == total_ or since_last_draw >= 0.1)
        {
          draw(current_, false);
          last_draw_ = now;
        }
    }

    void finish()
    {
      draw(current_, true);
      std::cerr << "\n";
    }

  private:
    void draw(int current, bool force)
    {
      if(total_ <= 0 and not force)
        {
          return;
        }

      const double fraction = total_ > 0
        ? static_cast<double>(current) / static_cast<double>(total_) : 1.0;
      const double elapsed =
        std::chrono::duration<double>(clock_type::now() - start_).count();
      const double rate = elapsed > 0.0
        ? static_cast<double>(current) / elapsed : 0.0;
      const double total = (current > 0 and total_ > current)
        ? elapsed * (static_cast<double>(total_) / static_cast<double>(current))
        : 0.0;

      std::ostringstream suffix;
      suffix << "] "
             << current << "/" << total_ << " "
             << std::fixed << std::setprecision(1) << (fraction * 100.0)
             << "% "
             << std::fixed << std::setprecision(1) << rate << "/s "
             << "elapsed: " << format_duration(elapsed) << " [sec]"
             << " total: " << format_duration(total) << " [sec]";
      const std::string suffix_text = suffix.str();

      const std::string prefix = label_ + ": [";
      const int available = 100
        - static_cast<int>(prefix.size())
        - static_cast<int>(suffix_text.size());
      const int width = std::max(0, std::min(40, available));
      const int filled = std::min(width, static_cast<int>(fraction * width));

      std::ostringstream line;
      line << prefix;
      for(int i = 0; i < width; ++i)
        {
          line << (i < filled ? '#' : '-');
        }
      line << suffix_text;

      std::cerr << "\r\033[K" << line.str() << std::flush;
    }

  private:
    std::string label_;
    int total_ = 0;
    int current_ = 0;
    clock_type::time_point start_;
    clock_type::time_point last_draw_;
  };

  pdflib::decode_config default_decode_config()
  {
    pdflib::decode_config config;
    config.page_boundary = "crop_box";
    config.do_sanitization = true;
    config.keep_char_cells = true;
    config.keep_shapes = false;
    config.keep_bitmaps = false;
    config.max_num_lines = -1;
    config.max_num_bitmaps = -1;
    config.create_word_cells = false;
    config.create_line_cells = false;
    config.enforce_same_font = true;
    config.horizontal_cell_tolerance = 1.0;
    config.word_space_width_factor_for_merge = 0.33;
    config.line_space_width_factor_for_merge = 1.0;
    config.line_space_width_factor_for_merge_with_space = 0.33;
    config.do_thread_safe = true;
    config.release_native_memory_every_n_pages = 0;
    config.keep_glyphs = false;
    config.keep_qpdf_warnings = false;
    return config;
  }

  pdflib::render_config default_render_config(float scale)
  {
    pdflib::render_config config;
    config.render_text = true;
    config.draw_text_bbox = false;
    config.draw_text_basepoint = false;
    config.fit_glyph_bbox_to_target = false;
    config.resolve_fonts = true;
    config.font_similarity_cutoff = 0.75f;
    config.scale = scale;
    config.canvas_width = -1;
    config.canvas_height = -1;
    return config;
  }

  std::vector<std::filesystem::path> find_pdfs(const std::filesystem::path& input,
                                               bool recursive)
  {
    std::vector<std::filesystem::path> result;

    if(std::filesystem::is_regular_file(input))
      {
        if(input.extension() == ".pdf")
          {
            result.push_back(input);
          }
        return result;
      }

    if(not std::filesystem::is_directory(input))
      {
        return result;
      }

    if(recursive)
      {
        for(const auto& entry : std::filesystem::recursive_directory_iterator(input))
          {
            if(entry.is_regular_file() and entry.path().extension() == ".pdf")
              {
                result.push_back(entry.path());
              }
          }
      }
    else
      {
        for(const auto& entry : std::filesystem::directory_iterator(input))
          {
            if(entry.is_regular_file() and entry.path().extension() == ".pdf")
              {
                result.push_back(entry.path());
              }
          }
      }

    std::sort(result.begin(), result.end());
    return result;
  }

  int count_pages(const std::filesystem::path& pdf_path)
  {
    pdflib::pdf_timings timings;
    doc_decoder_type doc(timings);
    std::string filename = pdf_path.string();
    std::optional<std::string> password = std::nullopt;

    if(not doc.process_document_from_file(filename, password))
      {
        return 0;
      }

    return doc.get_number_of_pages();
  }

  std::vector<scheduled_doc> make_schedule(const std::vector<std::filesystem::path>& pdfs,
                                           std::optional<int> max_pages,
                                           int& total_pages)
  {
    std::vector<scheduled_doc> schedule;
    int remaining = max_pages.value_or(std::numeric_limits<int>::max());
    total_pages = 0;
    int counted = 0;
    progress_bar progress("counting pages", static_cast<int>(pdfs.size()));

    for(const auto& pdf_path : pdfs)
      {
        if(remaining <= 0)
          {
            break;
          }

        const int page_count = count_pages(pdf_path);
        ++counted;
        progress.update(counted);
        if(page_count <= 0)
          {
            continue;
          }

        const int selected = std::min(page_count, remaining);

        scheduled_doc doc;
        doc.path = pdf_path;
        doc.total_pages = page_count;
        doc.pages.reserve(static_cast<std::size_t>(selected));
        for(int page = 0; page < selected; ++page)
          {
            doc.pages.push_back(page);
          }

        schedule.push_back(std::move(doc));
        total_pages += selected;
        remaining -= selected;
      }

    progress.finish();
    return schedule;
  }

  std::vector<doc_decoder_ptr> load_documents(const std::vector<scheduled_doc>& schedule)
  {
    std::vector<doc_decoder_ptr> docs;
    docs.reserve(schedule.size());

    for(const auto& entry : schedule)
      {
        auto doc = std::make_shared<doc_decoder_type>();
        std::string filename = entry.path.string();
        std::optional<std::string> password = std::nullopt;

        if(not doc->process_document_from_file(filename, password))
          {
            throw std::runtime_error("failed to load PDF: " + filename);
          }

        docs.push_back(doc);
      }

    return docs;
  }

  std::vector<page_task> build_tasks(const std::vector<scheduled_doc>& schedule)
  {
    std::vector<page_task> tasks;
    for(std::size_t doc_index = 0; doc_index < schedule.size(); ++doc_index)
      {
        for(int page : schedule[doc_index].pages)
          {
            tasks.push_back(page_task{doc_index, page});
          }
      }
    return tasks;
  }

  std::string csv_escape(const std::string& value)
  {
    if(value.find_first_of(",\"\n") == std::string::npos)
      {
        return value;
      }

    std::string escaped = "\"";
    for(char c : value)
      {
        if(c == '"')
          {
            escaped += "\"\"";
          }
        else
          {
            escaped += c;
          }
      }
    escaped += "\"";
    return escaped;
  }

  class timing_csv_writer
  {
  public:
    timing_csv_writer(bool enabled, const std::filesystem::path& path):
      enabled_(enabled)
    {
      if(not enabled_)
        {
          return;
        }

      const bool write_header = (not std::filesystem::exists(path))
        or std::filesystem::file_size(path) == 0;
      out_.open(path, std::ios::app);
      if(not out_)
        {
          throw std::runtime_error("could not open timing csv: " + path.string());
        }
      if(write_header)
        {
          out_ << "mode,threads,render,doc_key,page_number,success,"
               << "timing_total_s,timing_make_page_decoder_s,timing_decode_page_s,"
               << "timing_create_word_cells_s,timing_create_line_cells_s,"
               << "timing_render_page_s,error_message\n";
        }
    }

    void write(const std::string& mode,
               int threads,
               bool render,
               const page_result& result)
    {
      if(not enabled_)
        {
          return;
        }

      out_ << mode << ','
           << threads << ','
           << (render ? "true" : "false") << ','
           << csv_escape(result.doc_key) << ','
           << (result.page_number + 1) << ','
           << (result.success ? "true" : "false") << ','
           << result.timings.total_s << ','
           << result.timings.make_page_decoder_s << ','
           << result.timings.decode_page_s << ','
           << result.timings.create_word_cells_s << ','
           << result.timings.create_line_cells_s << ','
           << result.timings.render_page_s << ','
           << csv_escape(result.error_message)
           << '\n';
    }

  private:
    bool enabled_;
    std::ofstream out_;
  };

  class threaded_benchmark
  {
  public:
    threaded_benchmark(const std::vector<scheduled_doc>& schedule,
                       const std::vector<doc_decoder_ptr>& docs,
                       int num_threads,
                       int max_concurrent_results,
                       pdflib::decode_config decode_config,
                       std::optional<pdflib::render_config> render_config):
      schedule_(schedule),
      docs_(docs),
      num_threads_(num_threads),
      max_concurrent_results_(max_concurrent_results),
      decode_config_(decode_config),
      render_config_(render_config)
    {
      if(render_config_.has_value())
        {
          font_resolver_ = std::make_shared<pdflib::blend2d_font_resolver>();
          font_resolver_->warm();
        }
    }

    benchmark_result run(const std::string& mode,
                         bool enable_timing,
                         const std::filesystem::path& timing_csv)
    {
      tasks_ = build_tasks(schedule_);
      next_task_.store(0);
      tasks_remaining_.store(static_cast<int>(tasks_.size()));
      active_workers_.store(std::min(num_threads_, static_cast<int>(tasks_.size())));

      timing_csv_writer csv_writer(enable_timing, timing_csv);

      auto start = clock_type::now();

      for(int i = 0; i < active_workers_.load(); ++i)
        {
          workers_.emplace_back(&threaded_benchmark::worker_loop, this);
        }

      int errors = 0;
      int completed = 0;
      progress_bar progress(render_config_.has_value() ? "  rendering" : "  parsing",
                            static_cast<int>(tasks_.size()));
      while(tasks_remaining_.load() > 0)
        {
          page_result result = get_result();
          ++completed;
          if(not result.success)
            {
              ++errors;
            }

          csv_writer.write(mode, num_threads_, render_config_.has_value(), result);
          progress.update(completed);
        }
      progress.finish();

      for(auto& worker : workers_)
        {
          if(worker.joinable())
            {
              worker.join();
            }
        }

      const double elapsed = std::chrono::duration<double>(clock_type::now() - start).count();
      return benchmark_result{num_threads_, elapsed, errors};
    }

  private:
    void worker_loop()
    {
      while(true)
        {
          const std::size_t task_index = next_task_.fetch_add(1);
          if(task_index >= tasks_.size())
            {
              break;
            }

          const page_task task = tasks_[task_index];
          page_result result;
          result.doc_key = schedule_[task.doc_index].path.string();
          result.page_number = task.page_number;

          try
            {
              auto total_start = clock_type::now();

              auto stage_start = clock_type::now();
              auto page_decoder = docs_[task.doc_index]->make_thread_safe_page_decoder(task.page_number);
              result.timings.make_page_decoder_s =
                std::chrono::duration<double>(clock_type::now() - stage_start).count();

              stage_start = clock_type::now();
              page_decoder->decode_page(decode_config_);
              result.timings.decode_page_s =
                std::chrono::duration<double>(clock_type::now() - stage_start).count();

              if(decode_config_.create_word_cells)
                {
                  stage_start = clock_type::now();
                  page_decoder->create_word_cells(decode_config_);
                  result.timings.create_word_cells_s =
                    std::chrono::duration<double>(clock_type::now() - stage_start).count();
                }

              if(decode_config_.create_line_cells)
                {
                  stage_start = clock_type::now();
                  page_decoder->create_line_cells(decode_config_);
                  result.timings.create_line_cells_s =
                    std::chrono::duration<double>(clock_type::now() - stage_start).count();
                }

              if(render_config_.has_value())
                {
                  stage_start = clock_type::now();
                  pdflib::renderer<pdflib::BLEND2D> rnd(*render_config_, font_resolver_);
                  page_decoder->get_instructions().iterate_over_instructions(rnd);
                  result.timings.render_page_s =
                    std::chrono::duration<double>(clock_type::now() - stage_start).count();
                  result.image_data = rnd.get_canvas();
                }

              result.timings.total_s =
                std::chrono::duration<double>(clock_type::now() - total_start).count();
              result.success = true;
              result.page_decoder = page_decoder;
            }
          catch(const std::exception& exc)
            {
              result.success = false;
              result.error_message = exc.what();
            }

          push_result(std::move(result));
        }

      active_workers_.fetch_sub(1);
      results_available_.notify_all();
    }

    void push_result(page_result result)
    {
      std::unique_lock<std::mutex> lock(results_mutex_);
      results_consumed_.wait(lock, [this]() {
        return static_cast<int>(results_.size()) < max_concurrent_results_;
      });

      results_.push(std::move(result));
      lock.unlock();
      results_available_.notify_one();
    }

    page_result get_result()
    {
      std::unique_lock<std::mutex> lock(results_mutex_);
      results_available_.wait(lock, [this]() {
        return not results_.empty() or active_workers_.load() == 0;
      });

      if(results_.empty())
        {
          page_result result;
          result.success = false;
          result.error_message = "no result available";
          return result;
        }

      page_result result = std::move(results_.front());
      results_.pop();
      tasks_remaining_.fetch_sub(1);
      lock.unlock();
      results_consumed_.notify_one();
      return result;
    }

  private:
    const std::vector<scheduled_doc>& schedule_;
    const std::vector<doc_decoder_ptr>& docs_;
    int num_threads_;
    int max_concurrent_results_;
    pdflib::decode_config decode_config_;
    std::optional<pdflib::render_config> render_config_;
    std::shared_ptr<pdflib::blend2d_font_resolver> font_resolver_;

    std::vector<page_task> tasks_;
    std::atomic<std::size_t> next_task_{0};
    std::atomic<int> tasks_remaining_{0};
    std::atomic<int> active_workers_{0};

    std::queue<page_result> results_;
    std::mutex results_mutex_;
    std::condition_variable results_available_;
    std::condition_variable results_consumed_;

    std::vector<std::thread> workers_;
  };

  void print_decode_config(const pdflib::decode_config& config)
  {
    std::cout << "Decode config:\n" << config.to_string() << "\n";
  }

  void print_render_config(const pdflib::render_config& config)
  {
    std::cout << "Render config:\n"
              << std::left
              << std::setw(32) << "parameter" << "value\n"
              << std::string(44, '-') << "\n"
              << std::setw(32) << "render_text" << config.render_text << "\n"
              << std::setw(32) << "draw_text_bbox" << config.draw_text_bbox << "\n"
              << std::setw(32) << "draw_text_basepoint" << config.draw_text_basepoint << "\n"
              << std::setw(32) << "fit_glyph_bbox_to_target" << config.fit_glyph_bbox_to_target << "\n"
              << std::setw(32) << "resolve_fonts" << config.resolve_fonts << "\n"
              << std::setw(32) << "font_similarity_cutoff" << config.font_similarity_cutoff << "\n"
              << std::setw(32) << "scale" << config.scale << "\n"
              << std::setw(32) << "canvas_width" << config.canvas_width << "\n"
              << std::setw(32) << "canvas_height" << config.canvas_height << "\n";
  }

  void print_table(const std::string& title,
                   const std::vector<benchmark_result>& results,
                   int total_pages)
  {
    const double t1 = results.empty() ? 0.0 : results.front().wall_time_s;

    std::cout << "\n=== " << title << " ===\n";
    std::cout << std::left
              << std::setw(18) << "backend"
              << std::right
              << std::setw(10) << "threads"
              << std::setw(18) << "wall_time (s)"
              << std::setw(18) << "vs threaded(1)"
              << std::setw(14) << "pages/sec"
              << std::setw(12) << "ms/page"
              << std::setw(10) << "errors"
              << "\n";
    std::cout << std::string(100, '-') << "\n";

    for(const auto& result : results)
      {
        const double speedup = result.wall_time_s > 0.0 ? t1 / result.wall_time_s : 0.0;
        const double pages_per_sec = result.wall_time_s > 0.0
          ? static_cast<double>(total_pages) / result.wall_time_s : 0.0;
        const double ms_per_page = total_pages > 0
          ? 1000.0 * result.wall_time_s / static_cast<double>(total_pages) : 0.0;

        std::ostringstream speedup_ss;
        speedup_ss << std::fixed << std::setprecision(2) << speedup << "x";

        std::cout << std::left
                  << std::setw(18) << "docling threaded"
                  << std::right
                  << std::setw(10) << result.threads
                  << std::setw(18) << std::fixed << std::setprecision(3) << result.wall_time_s
                  << std::setw(18) << speedup_ss.str()
                  << std::setw(14) << std::fixed << std::setprecision(1) << pages_per_sec
                  << std::setw(12) << std::fixed << std::setprecision(2) << ms_per_page
                  << std::setw(10) << result.errors
                  << "\n";
      }
  }

  void initialise_fonts()
  {
    std::string resource_dir = resource_utils::get_resources_dir(false).string();
    nlohmann::json data = nlohmann::json::object({});
    data[pdflib::pdf_resource<pdflib::PAGE_FONT>::RESOURCE_DIR_KEY] = resource_dir;
    std::unordered_map<std::string, double> font_timings;
    pdflib::pdf_resource<pdflib::PAGE_FONT>::initialise(data, font_timings);
  }

  cli_options parse_cli(int argc, char* argv[], pdflib::decode_config& decode_config)
  {
    cxxopts::Options options("run_scaling", "Thread-scaling benchmark for docling-parse C++");
    options.add_options()
      ("input", "Local PDF file or directory", cxxopts::value<std::string>())
      ("mode", "Benchmark stage: parse, render, or both", cxxopts::value<std::string>()->default_value("render"))
      ("recursive,r", "Recurse into subdirectories", cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
      ("max-pages,l", "Maximum number of pages to process across all input PDFs", cxxopts::value<int>())
      ("max-concurrent-results", "Max buffered results for threaded processing", cxxopts::value<int>()->default_value("64"))
      ("threads", "Comma-separated thread counts", cxxopts::value<std::string>()->default_value("1,2,4,8,12,16"))
      ("scale", "Render scale for render mode", cxxopts::value<float>()->default_value("1.0"))
      ("enable-timing", "Write one CSV timing row per page result", cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
      ("timing-csv", "CSV path used when --enable-timing is set", cxxopts::value<std::string>()->default_value("timing-cpp.csv"))
      ("loglevel", "Log level [fatal, error, warning, info]", cxxopts::value<std::string>()->default_value("fatal"))
      ("page-boundary", "Page boundary [crop_box, media_box]", cxxopts::value<std::string>())
      ("do-sanitization", "Run post-parse sanitization", cxxopts::value<std::string>())
      ("keep-char-cells", "Keep individual character cells", cxxopts::value<std::string>())
      ("keep-shapes", "Keep shape items", cxxopts::value<std::string>())
      ("keep-bitmaps", "Keep bitmap items", cxxopts::value<std::string>())
      ("max-num-lines", "Cap on number of lines per page", cxxopts::value<int>())
      ("max-num-bitmaps", "Cap on number of bitmaps per page", cxxopts::value<int>())
      ("create-word-cells", "Build word-level cells", cxxopts::value<std::string>())
      ("create-line-cells", "Build line-level cells", cxxopts::value<std::string>())
      ("enforce-same-font", "Require same font within a word/line cell", cxxopts::value<std::string>())
      ("horizontal-cell-tolerance", "Horizontal merge tolerance", cxxopts::value<double>())
      ("word-space-factor", "Space-width factor for word merging", cxxopts::value<double>())
      ("line-space-factor", "Space-width factor for line merging", cxxopts::value<double>())
      ("line-space-factor-with-space", "Space-width factor for line merging with space", cxxopts::value<double>())
      ("keep-glyphs", "Keep unmapped GLYPH<...> tokens", cxxopts::value<std::string>())
      ("keep-qpdf-warnings", "Emit QPDF warnings", cxxopts::value<std::string>())
      ("h,help", "Print usage");

    options.parse_positional({"input"});
    options.positional_help("input");

    auto result = options.parse(argc, argv);
    if(result.count("help") or not result.count("input"))
      {
        std::cout << options.help() << "\n";
        std::exit(result.count("help") ? 0 : 1);
      }

    cli_options cli;
    cli.input = result["input"].as<std::string>();
    cli.recursive = result["recursive"].as<bool>();
    cli.max_concurrent_results = result["max-concurrent-results"].as<int>();
    if(cli.max_concurrent_results <= 0)
      {
        throw std::runtime_error("--max-concurrent-results must be positive");
      }
    cli.threads = parse_thread_counts(result["threads"].as<std::string>());
    cli.scale = result["scale"].as<float>();
    cli.enable_timing = result["enable-timing"].as<bool>();
    cli.timing_csv = result["timing-csv"].as<std::string>();
    cli.loglevel = result["loglevel"].as<std::string>();

    if(result.count("max-pages"))
      {
        cli.max_pages = result["max-pages"].as<int>();
      }

    std::string raw_mode = result["mode"].as<std::string>();
    std::transform(raw_mode.begin(), raw_mode.end(), raw_mode.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if(raw_mode == "parse")
      {
        cli.mode = run_mode::parse;
      }
    else if(raw_mode == "render")
      {
        cli.mode = run_mode::render;
      }
    else if(raw_mode == "both")
      {
        cli.mode = run_mode::both;
      }
    else
      {
        throw std::runtime_error("--mode must be one of parse, render, both");
      }

    if(result.count("page-boundary")) { decode_config.page_boundary = result["page-boundary"].as<std::string>(); }
    if(result.count("do-sanitization")) { decode_config.do_sanitization = parse_bool(result["do-sanitization"].as<std::string>()); }
    if(result.count("keep-char-cells")) { decode_config.keep_char_cells = parse_bool(result["keep-char-cells"].as<std::string>()); }
    if(result.count("keep-shapes")) { decode_config.keep_shapes = parse_bool(result["keep-shapes"].as<std::string>()); }
    if(result.count("keep-bitmaps")) { decode_config.keep_bitmaps = parse_bool(result["keep-bitmaps"].as<std::string>()); }
    if(result.count("max-num-lines")) { decode_config.max_num_lines = result["max-num-lines"].as<int>(); }
    if(result.count("max-num-bitmaps")) { decode_config.max_num_bitmaps = result["max-num-bitmaps"].as<int>(); }
    if(result.count("create-word-cells")) { decode_config.create_word_cells = parse_bool(result["create-word-cells"].as<std::string>()); }
    if(result.count("create-line-cells")) { decode_config.create_line_cells = parse_bool(result["create-line-cells"].as<std::string>()); }
    if(result.count("enforce-same-font")) { decode_config.enforce_same_font = parse_bool(result["enforce-same-font"].as<std::string>()); }
    if(result.count("horizontal-cell-tolerance")) { decode_config.horizontal_cell_tolerance = result["horizontal-cell-tolerance"].as<double>(); }
    if(result.count("word-space-factor")) { decode_config.word_space_width_factor_for_merge = result["word-space-factor"].as<double>(); }
    if(result.count("line-space-factor")) { decode_config.line_space_width_factor_for_merge = result["line-space-factor"].as<double>(); }
    if(result.count("line-space-factor-with-space")) { decode_config.line_space_width_factor_for_merge_with_space = result["line-space-factor-with-space"].as<double>(); }
    if(result.count("keep-glyphs")) { decode_config.keep_glyphs = parse_bool(result["keep-glyphs"].as<std::string>()); }
    if(result.count("keep-qpdf-warnings")) { decode_config.keep_qpdf_warnings = parse_bool(result["keep-qpdf-warnings"].as<std::string>()); }

    decode_config.do_thread_safe = true;
    return cli;
  }
}

int main(int argc, char* argv[])
{
  loguru::init(argc, argv);

  try
    {
      pdflib::decode_config decode_config = default_decode_config();
      cli_options cli = parse_cli(argc, argv, decode_config);
      set_loglevel(cli.loglevel);
      initialise_fonts();

      auto pdfs = find_pdfs(cli.input, cli.recursive);
      if(pdfs.empty())
        {
          std::cerr << "No PDFs found for input: " << cli.input << "\n";
          return 2;
        }

      int total_pages = 0;
      auto schedule = make_schedule(pdfs, cli.max_pages, total_pages);
      if(schedule.empty() or total_pages <= 0)
        {
          std::cerr << "No pages selected for benchmarking\n";
          return 2;
        }

      std::cout << "Benchmark: " << schedule.size()
                << " documents, " << total_pages << " total pages\n";
      std::cout << "Mode: " << mode_to_string(cli.mode) << "\n";
      std::cout << "Thread counts to test: ";
      for(std::size_t i = 0; i < cli.threads.size(); ++i)
        {
          if(i > 0) { std::cout << ","; }
          std::cout << cli.threads[i];
        }
      std::cout << "\n";
      std::cout << "Max concurrent results: " << cli.max_concurrent_results << "\n";
      if(cli.mode == run_mode::render or cli.mode == run_mode::both)
        {
          std::cout << "Render scale: " << cli.scale << "\n";
        }
      std::cout << "\n";

      print_decode_config(decode_config);
      auto render_config = default_render_config(cli.scale);
      if(cli.mode == run_mode::render or cli.mode == run_mode::both)
        {
          print_render_config(render_config);
        }

      std::cout << "\nLoading documents ...\n";
      auto docs = load_documents(schedule);

      std::vector<run_mode> modes;
      if(cli.mode == run_mode::both)
        {
          modes = {run_mode::parse, run_mode::render};
        }
      else
        {
          modes = {cli.mode};
        }

      for(run_mode mode : modes)
        {
          const bool render = (mode == run_mode::render);
          const std::string title = render ? "RENDER (decode + rasterise)" : "PARSE (decode only)";
          std::cout << "\n##### " << title << " #####\n";

          std::vector<benchmark_result> results;
          for(int threads : cli.threads)
            {
              std::cout << "Running threaded "
                        << (render ? "renderer" : "parser")
                        << " with " << threads << " threads ...\n";

              threaded_benchmark benchmark(schedule,
                                           docs,
                                           threads,
                                           cli.max_concurrent_results,
                                           decode_config,
                                           render ? std::optional<pdflib::render_config>(render_config)
                                                  : std::nullopt);
              benchmark_result result = benchmark.run(render ? "render" : "parse",
                                                      cli.enable_timing,
                                                      cli.timing_csv);
              results.push_back(result);
              std::cout << "  threads=" << threads
                        << ": " << std::fixed << std::setprecision(3)
                        << result.wall_time_s << "s";
              if(result.errors > 0)
                {
                  std::cout << " (" << result.errors << " errors)";
                }
              std::cout << "\n";
            }

          print_table(title, results, total_pages);
        }
    }
  catch(const cxxopts::exceptions::exception& exc)
    {
      std::cerr << "Error parsing options: " << exc.what() << "\n";
      return 1;
    }
  catch(const std::exception& exc)
    {
      std::cerr << "Error: " << exc.what() << "\n";
      return 1;
    }

  return 0;
}
