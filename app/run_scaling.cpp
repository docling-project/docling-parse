//-*-C++-*-

#include "parse.h"
#include "render.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
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
    parse_without_bitmaps,
    parse_with_bitmaps,
    render,
    default_mode,
    all,
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
    double get_canvas_s = 0.0;
    double save_png_s = 0.0;
    double collect_detail_s = 0.0;
    double worker_queue_wait_s = 0.0;
    double consumer_wait_s = 0.0;
  };

  struct page_result
  {
    std::string doc_key;
    int page_number = 0;
    bool success = false;
    std::string error_message;
    page_timings timings;
    std::unordered_map<std::string, double> detail_timings;
    std::optional<nlohmann::json> glyph_cache_diagnostics;
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
    run_mode mode = run_mode::default_mode;
    bool recursive = false;
    std::optional<int> max_pages = std::nullopt;
    int max_concurrent_results = 64;
    std::vector<int> threads{1, 2, 4, 8, 12, 16};
    float scale = 2.0f;
    std::size_t glyph_bbox_cache_capacity = 65536;
    bool enable_timing = false;
    bool suppress_config = false;
    bool suppress_timings = false;
    std::filesystem::path timing_csv = "timing-cpp.csv";
    std::optional<std::filesystem::path> glyph_cache_diagnostics = std::nullopt;
    std::optional<std::filesystem::path> output_dir = std::nullopt;
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
      case run_mode::parse_without_bitmaps: return "parse-without-bitmaps";
      case run_mode::parse_with_bitmaps: return "parse-with-bitmaps";
      case run_mode::render: return "render";
      case run_mode::default_mode: return "default";
      case run_mode::all: return "all";
      }
    return "default";
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
    return std::to_string(static_cast<int>(seconds));
  }

  std::string zero_padded(std::size_t value, int width = 6)
  {
    std::ostringstream ss;
    ss << std::setw(width) << std::setfill('0') << value;
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
    config.min_stroke_width = 1.0f;
    config.draw_text_bbox = false;
    config.draw_text_basepoint = false;
    config.display_widgets = false;
    config.fit_glyph_bbox_to_target = false;
    config.resolve_fonts = true;
    config.font_similarity_cutoff = 0.75f;
    config.scale = scale;
    config.canvas_width = -1;
    config.canvas_height = -1;
    return config;
  }

  // The config a mode's workers decode with: the CLI values plus what the mode
  // requires. Both the printed table and the benchmark use this result.
  pdflib::decode_config effective_decode_config(run_mode mode,
                                                pdflib::decode_config config,
                                                const pdflib::render_config& render_config)
  {
    const bool render = (mode == run_mode::render);
    const bool parse_with_bitmaps = (mode == run_mode::parse_with_bitmaps);

    config.keep_shapes = render or config.keep_shapes;
    // without keep_bitmaps the renderer has no bitmaps to iterate over
    config.keep_bitmaps = render or parse_with_bitmaps;
    config.extract_bitmap_pixels = render or parse_with_bitmaps;

    if(render)
      {
        // Rendering needs the embedded font programs, whatever the CLI asked
        // for. Without them this benchmark silently rasterised with substituted
        // system fonts, which is both cheaper and not what the library does.
        config.extract_font_programs = true;

        // Same resolution hint the library sets, so this benchmark measures
        // the decode work the library actually does; see
        // docling_threaded_renderer for why only the scale form qualifies.
        if(render_config.scale > 0.0f)
          {
            config.bitmap_target_pixels_per_unit = render_config.scale;
          }
      }

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

  struct timing_stage
  {
    const char* name;
    double page_timings::*value;
  };

  const std::vector<timing_stage>& timing_stages()
  {
    static const std::vector<timing_stage> stages = {
      {"total_s", &page_timings::total_s},
      {"make_page_decoder_s", &page_timings::make_page_decoder_s},
      {"decode_page_s", &page_timings::decode_page_s},
      {"create_word_cells_s", &page_timings::create_word_cells_s},
      {"create_line_cells_s", &page_timings::create_line_cells_s},
      {"render_page_s", &page_timings::render_page_s},
      {"get_canvas_s", &page_timings::get_canvas_s},
      {"save_png_s", &page_timings::save_png_s},
      {"collect_detail_s", &page_timings::collect_detail_s},
      {"worker_queue_wait_s", &page_timings::worker_queue_wait_s},
      {"consumer_wait_s", &page_timings::consumer_wait_s},
    };
    return stages;
  }

  void collect_detail_timings(page_result& result,
                              const pdflib::pdf_timings& timings)
  {
    const auto start = clock_type::now();
    for(const auto& item : timings.to_sum_map())
      {
        // Keep a bounded vocabulary; omit dynamic per-font/resource names.
        if(pdflib::pdf_timings::is_static_key(item.first))
          {
            result.detail_timings[item.first] += item.second;
          }
      }
    result.timings.collect_detail_s +=
      std::chrono::duration<double>(clock_type::now() - start).count();
  }

  class timing_summary
  {
  public:
    void add(const page_result& result)
    {
      for(const auto& stage : timing_stages())
        {
          totals_.*(stage.value) += result.timings.*(stage.value);
        }
      for(const auto& item : result.detail_timings)
        {
          details_[item.first] += item.second;
        }
      auto& doc = documents_[result.doc_key];
      doc.worker_s += result.timings.total_s;
      ++doc.pages;
      if(not result.success) { ++doc.errors; }

      slowest_.push_back({result.doc_key, result.page_number,
                          result.timings.total_s, result.success});
      std::sort(slowest_.begin(), slowest_.end(), [](const auto& a, const auto& b) {
        return a.worker_s > b.worker_s;
      });
      if(slowest_.size() > 10) { slowest_.pop_back(); }
    }

    void add_csv_write(double seconds) { csv_write_s_ += seconds; }

    void print(int threads, double wall_s, std::ostream& out = std::cout) const
    {
      out << "\nTiming diagnostics (threads=" << threads << ", CSV schema 2)\n"
                << std::fixed << std::setprecision(6)
                << "  wall_s: " << wall_s << " (includes collection/CSV; excludes summary printing)\n"
                << "  Worker elapsed seconds summed across pages; not CPU seconds.\n"
                << "  total_s includes detail collection, excludes queue wait.\n";
      for(const auto& stage : timing_stages())
        {
          if(stage.value != &page_timings::consumer_wait_s)
            {
              out << "  " << std::left << std::setw(30) << stage.name
                        << std::right << totals_.*(stage.value) << "\n";
            }
        }
      out << "  consumer_wait_s: " << totals_.consumer_wait_s << "\n"
                << "  consumer_csv_write_s: " << csv_write_s_ << "\n"
                << "  Queue waits include mutex acquisition and condition waiting;\n"
                << "  they overlap across threads and are not added to wall time.\n"
                << "\nNative detail worker-seconds (nested; do not sum parent/child rows):\n"
                << pdflib::pdf_timings::format_tree_table(details_);

      std::vector<std::pair<std::string, document_total>> docs(documents_.begin(),
                                                               documents_.end());
      std::sort(docs.begin(), docs.end(), [](const auto& a, const auto& b) {
        return a.second.worker_s > b.second.worker_s;
      });
      out << "\nTop documents by summed page worker elapsed (not document wall time):\n";
      for(std::size_t i = 0; i < std::min<std::size_t>(10, docs.size()); ++i)
        {
          out << "  " << docs[i].second.worker_s << " s  "
                    << docs[i].second.pages << " pages  "
                    << docs[i].second.errors << " errors  " << docs[i].first << "\n";
        }
      out << "Top pages by worker elapsed (1-based page numbers):\n";
      for(const auto& page : slowest_)
        {
          out << "  " << page.worker_s << " s  " << page.doc_key
                    << " page=" << page.page_number + 1
                    << " success=" << (page.success ? "true" : "false") << "\n";
        }
    }

  private:
    struct document_total
    {
      double worker_s = 0.0;
      int pages = 0;
      int errors = 0;
    };
    struct slow_page
    {
      std::string doc_key;
      int page_number;
      double worker_s;
      bool success;
    };
    page_timings totals_;
    std::unordered_map<std::string, double> details_;
    std::unordered_map<std::string, document_total> documents_;
    std::vector<slow_page> slowest_;
    double csv_write_s_ = 0.0;
  };

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
      const auto expected_header = header();
      if(not write_header)
        {
          std::ifstream existing(path);
          std::string existing_header;
          std::getline(existing, existing_header);
          if(not existing_header.empty() and existing_header.back() == '\r')
            {
              existing_header.pop_back();
            }
          if(existing_header != expected_header)
            {
              throw std::runtime_error("incompatible timing CSV header: " + path.string()
                                       + "; choose a new --timing-csv path (schema 2)");
            }
        }
      out_.open(path, std::ios::app);
      if(not out_)
        {
          throw std::runtime_error("could not open timing csv: " + path.string());
        }
      if(write_header)
        {
          out_ << expected_header << '\n';
        }
      out_ << std::setprecision(9);
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

      out_ << "2," << mode << ','
           << threads << ','
           << (render ? "true" : "false") << ','
           << csv_escape(result.doc_key) << ','
           << (result.page_number + 1) << ','
           << (result.success ? "true" : "false");
      for(const auto& stage : timing_stages())
        {
          out_ << ',' << result.timings.*(stage.value);
        }
      out_ << ',' << csv_escape(nlohmann::json(result.detail_timings).dump())
           << ',' << csv_escape(result.error_message) << '\n';
    }

    void finish()
    {
      if(enabled_)
        {
          out_.flush();
          if(not out_) { throw std::runtime_error("could not finish writing timing CSV"); }
        }
    }

  private:
    static std::string header()
    {
      std::string result = "schema_version,mode,threads,render,doc_key,page_number,success";
      for(const auto& stage : timing_stages())
        {
          result += ",timing_";
          result += stage.name;
        }
      return result + ",detail_timings_json,error_message";
    }
    bool enabled_;
    std::ofstream out_;
  };

  nlohmann::json glyph_cache_delta(
      const pdflib::glyph_bbox_cache::diagnostics& before,
      const pdflib::glyph_bbox_cache::diagnostics& after)
  {
    return {
      {"lookups", after.lookups - before.lookups},
      {"hits", after.hits - before.hits},
      {"misses", after.misses - before.misses},
      {"insertions", after.insertions - before.insertions},
      {"updates", after.updates - before.updates},
      {"evictions", after.evictions - before.evictions},
      {"outline_attempts", after.outline_attempts - before.outline_attempts},
      {"outline_successes", after.outline_successes - before.outline_successes},
      {"lookup_s", after.lookup_s - before.lookup_s},
      {"hit_touch_s", after.hit_touch_s - before.hit_touch_s},
      {"insert_s", after.insert_s - before.insert_s},
      {"outline_s", after.outline_s - before.outline_s},
    };
  }

  class threaded_benchmark
  {
  public:
    threaded_benchmark(const std::vector<scheduled_doc>& schedule,
                       const std::vector<doc_decoder_ptr>& docs,
                       int num_threads,
                       int max_concurrent_results,
                       pdflib::decode_config decode_config,
                       std::optional<pdflib::render_config> render_config,
                       std::optional<std::filesystem::path> output_dir):
      schedule_(schedule),
      docs_(docs),
      num_threads_(num_threads),
      max_concurrent_results_(max_concurrent_results),
      decode_config_(decode_config),
      render_config_(render_config),
      output_dir_(std::move(output_dir))
    {
      if(render_config_.has_value())
        {
          font_resolver_ = std::make_shared<pdflib::blend2d_font_resolver>();
          font_resolver_->warm();

          // shared across pages, exactly as docling_threaded_renderer does;
          // a per-page cache would reload every embedded font program
          embedded_font_cache_ = std::make_shared<pdflib::blend2d_embedded_font_cache>();
        }
    }

    benchmark_result run(const std::string& mode,
                         bool enable_timing,
                         const std::filesystem::path& timing_csv,
                         std::ostream* glyph_diagnostics = nullptr,
                         std::ostream* timing_output = nullptr)
    {
      if(glyph_diagnostics and not render_config_.has_value())
        {
          throw std::runtime_error("glyph-cache diagnostics require render mode");
        }
      enable_timing_ = enable_timing;
      glyph_diagnostics_enabled_ = glyph_diagnostics != nullptr;
      tasks_ = build_tasks(schedule_);
      next_task_.store(0);
      tasks_remaining_.store(static_cast<int>(tasks_.size()));
      active_workers_.store(std::min(num_threads_, static_cast<int>(tasks_.size())));

      timing_csv_writer csv_writer(enable_timing, timing_csv);
      timing_summary summary;

      auto start = clock_type::now();

      for(int i = 0; i < active_workers_.load(); ++i)
        {
          workers_.emplace_back(&threaded_benchmark::worker_loop, this, i);
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

          if(enable_timing_)
            {
              summary.add(result);
              const auto csv_start = clock_type::now();
              csv_writer.write(mode, num_threads_, render_config_.has_value(), result);
              summary.add_csv_write(
                std::chrono::duration<double>(clock_type::now() - csv_start).count());
            }
          if(glyph_diagnostics and result.glyph_cache_diagnostics.has_value())
            {
              *glyph_diagnostics << result.glyph_cache_diagnostics->dump() << '\n';
            }
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

      if(enable_timing_)
        {
          const auto flush_start = clock_type::now();
          csv_writer.finish();
          summary.add_csv_write(
            std::chrono::duration<double>(clock_type::now() - flush_start).count());
        }
      if(glyph_diagnostics)
        {
          glyph_diagnostics->flush();
          if(not *glyph_diagnostics)
            {
              throw std::runtime_error("could not write glyph-cache diagnostics");
            }
        }
      const double elapsed = std::chrono::duration<double>(clock_type::now() - start).count();
      if(enable_timing_)
        {
          summary.print(num_threads_, elapsed,
                        timing_output ? *timing_output : std::cout);
          if(timing_output)
            {
              timing_output->flush();
              if(not *timing_output)
                {
                  throw std::runtime_error("could not write timing summary");
                }
            }
        }
      return benchmark_result{num_threads_, elapsed, errors};
    }

  private:
    void worker_loop(int worker_id)
    {
      std::shared_ptr<pdflib::freetype_font_cache> freetype_font_cache;
      std::shared_ptr<pdflib::glyph_bbox_cache> glyph_bbox_cache;
      if(render_config_.has_value())
        {
          // FT_Face objects mutate their selected size and charmap while
          // building an outline. This cache is reused across the worker's
          // pages, but never shared with another render worker.
          freetype_font_cache = std::make_shared<pdflib::freetype_font_cache>();
          glyph_bbox_cache = std::make_shared<pdflib::glyph_bbox_cache>(
            render_config_->glyph_bbox_cache_capacity, glyph_diagnostics_enabled_);
        }

      std::size_t worker_page_index = 0;
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
          ++worker_page_index;

          pdflib::glyph_bbox_cache::diagnostics glyph_before;
          std::size_t entries_before = 0;
          if(glyph_diagnostics_enabled_)
            {
              glyph_before = glyph_bbox_cache->snapshot_diagnostics();
              entries_before = glyph_bbox_cache->size();
            }

          const auto total_start = clock_type::now();
          try
            {
              auto stage_start = clock_type::now();
              auto page_decoder = docs_[task.doc_index]->make_thread_safe_page_decoder(
                task.page_number,
                decode_config_.keep_qpdf_warnings);
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
                  pdflib::renderer<pdflib::BLEND2D> rnd(*render_config_,
                                                        font_resolver_,
                                                        embedded_font_cache_,
                                                        freetype_font_cache,
                                                        glyph_bbox_cache);
                  page_decoder->get_instructions().iterate_over_instructions(rnd);
                  result.timings.render_page_s =
                    std::chrono::duration<double>(clock_type::now() - stage_start).count();
                  auto output_start = enable_timing_ ? clock_type::now()
                                                    : clock_type::time_point{};
                  if(output_dir_.has_value())
                    {
                      const auto page_path = *output_dir_ /
                        ("doc_" + zero_padded(task.doc_index + 1) +
                         "_page_" + zero_padded(task.page_number + 1) + ".png");
                      rnd.save(page_path.string());
                      if(enable_timing_)
                        {
                          result.timings.save_png_s =
                            std::chrono::duration<double>(clock_type::now() - output_start).count();
                        }
                    }
                  else
                    {
                      result.image_data = rnd.get_canvas();
                      if(enable_timing_)
                        {
                          result.timings.get_canvas_s =
                            std::chrono::duration<double>(clock_type::now() - output_start).count();
                        }
                    }
                  if(enable_timing_)
                    {
                      // Render parent includes output extraction, but excludes
                      // collecting the already-recorded renderer detail timings.
                      const double render_total_s =
                        std::chrono::duration<double>(clock_type::now() - stage_start).count();
                      collect_detail_timings(result, rnd.get_timings());
                      result.detail_timings[pdflib::pdf_timings::KEY_RENDER_PAGE] = render_total_s;
                    }
                }

              if(enable_timing_)
                {
                  collect_detail_timings(result, page_decoder->get_timings());
                }
              result.success = true;
              result.page_decoder = page_decoder;
            }
          catch(const std::exception& exc)
            {
              result.success = false;
              result.error_message = exc.what();
            }
          if(glyph_diagnostics_enabled_)
            {
              result.glyph_cache_diagnostics = {
                {"schema_version", 1},
                {"threads", num_threads_},
                {"worker_id", worker_id},
                {"worker_page_index", worker_page_index},
                {"doc_key", result.doc_key},
                {"page_number", result.page_number + 1},
                {"success", result.success},
                {"capacity", render_config_->glyph_bbox_cache_capacity},
                {"entries_before", entries_before},
                {"entries_after", glyph_bbox_cache->size()},
                {"delta", glyph_cache_delta(glyph_before,
                                           glyph_bbox_cache->snapshot_diagnostics())},
              };
            }
          result.timings.total_s =
            std::chrono::duration<double>(clock_type::now() - total_start).count();

          push_result(std::move(result));
        }

      active_workers_.fetch_sub(1);
      results_available_.notify_all();
    }

    void push_result(page_result result)
    {
      const auto wait_start = enable_timing_ ? clock_type::now() : clock_type::time_point{};
      std::unique_lock<std::mutex> lock(results_mutex_);
      results_consumed_.wait(lock, [this]() {
        return static_cast<int>(results_.size()) < max_concurrent_results_;
      });

      if(enable_timing_)
        {
          result.timings.worker_queue_wait_s =
            std::chrono::duration<double>(clock_type::now() - wait_start).count();
        }
      results_.push(std::move(result));
      lock.unlock();
      results_available_.notify_one();
    }

    page_result get_result()
    {
      const auto wait_start = enable_timing_ ? clock_type::now() : clock_type::time_point{};
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
      if(enable_timing_)
        {
          result.timings.consumer_wait_s =
            std::chrono::duration<double>(clock_type::now() - wait_start).count();
        }
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
    bool enable_timing_ = false;
    bool glyph_diagnostics_enabled_ = false;
    pdflib::decode_config decode_config_;
    std::optional<pdflib::render_config> render_config_;
    std::optional<std::filesystem::path> output_dir_;
    std::shared_ptr<pdflib::blend2d_font_resolver> font_resolver_;
    std::shared_ptr<pdflib::blend2d_embedded_font_cache> embedded_font_cache_;

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

  void print_table(const std::string& title,
                   const std::vector<benchmark_result>& results,
                   int total_pages)
  {
    const auto baseline = std::find_if(
      results.begin(), results.end(),
      [](const benchmark_result& result) { return result.threads == 1; });
    const bool has_baseline = baseline != results.end()
      && baseline->wall_time_s > 0.0;
    const double t1 = has_baseline ? baseline->wall_time_s : 0.0;

    std::cout << "\n=== " << title << " ===\n";
    std::cout << std::left
              << std::setw(18) << "backend"
              << std::right
              << std::setw(10) << "threads"
              << std::setw(18) << "wall_time (s)"
              << std::setw(18) << "vs threaded(1)"
              << std::setw(14) << "efficiency"
              << std::setw(14) << "pages/sec"
              << std::setw(12) << "ms/page"
              << std::setw(10) << "errors"
              << "\n";
    std::cout << std::string(114, '-') << "\n";

    for(const auto& result : results)
      {
        const double speedup = has_baseline && result.wall_time_s > 0.0
          ? t1 / result.wall_time_s : 0.0;
        const double pages_per_sec = result.wall_time_s > 0.0
          ? static_cast<double>(total_pages) / result.wall_time_s : 0.0;
        const double ms_per_page = total_pages > 0
          ? 1000.0 * result.wall_time_s / static_cast<double>(total_pages) : 0.0;

        std::ostringstream speedup_ss;
        speedup_ss << std::fixed << std::setprecision(2) << speedup << "x";
        std::ostringstream efficiency_ss;
        if(has_baseline && result.threads > 0 && result.wall_time_s > 0.0)
          {
            efficiency_ss << std::fixed << std::setprecision(0)
                          << (100.0 * speedup / result.threads) << "%";
          }
        else
          {
            efficiency_ss << "n/a";
          }

        std::cout << std::left
                  << std::setw(18) << "docling threaded"
                  << std::right
                  << std::setw(10) << result.threads
                  << std::setw(18) << std::fixed << std::setprecision(3) << result.wall_time_s
                  << std::setw(18) << (has_baseline ? speedup_ss.str() : "n/a")
                  << std::setw(14) << efficiency_ss.str()
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
      ("mode", "Benchmark stage: parse-without-bitmaps, parse-with-bitmaps, render, default, or all", cxxopts::value<std::string>()->default_value("default"))
      ("recursive,r", "Recurse into subdirectories", cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
      ("max-pages,l", "Maximum number of pages to process across all input PDFs", cxxopts::value<int>())
      ("max-concurrent-results", "Max buffered results for threaded processing", cxxopts::value<int>()->default_value("64"))
      ("threads", "Comma-separated thread counts", cxxopts::value<std::string>()->default_value("1,2,4,8,12,16"))
      ("scale", "Render scale for render mode", cxxopts::value<float>()->default_value("2.0"))
      ("glyph-bbox-cache-capacity", "Maximum cached glyph bounding boxes per render worker; 0 disables caching", cxxopts::value<std::size_t>()->default_value("65536"))
      ("glyph-cache-diagnostics", "Write per-page worker-local glyph-cache counts/timings to a new JSONL file (render mode only)", cxxopts::value<std::string>())
      ("output-dir", "Write rendered PNGs under this directory", cxxopts::value<std::string>())
      ("enable-timing", "Write schema-2 per-page CSV timings and print native stage/wait summaries", cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
      ("timing-csv", "CSV path used when --enable-timing is set", cxxopts::value<std::string>()->default_value("timing-cpp.csv"))
      ("suppress-config", "Hide configuration tables; retain progress and results", cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
      ("suppress-timings", "Write timing summaries to <timing-csv>.summary.log instead of the console", cxxopts::value<bool>()->default_value("false")->implicit_value("true"))
      ("loglevel", "Log level [fatal, error, warning, info]", cxxopts::value<std::string>()->default_value("fatal"))
      ("page-boundary", "Page boundary [crop_box, media_box]", cxxopts::value<std::string>())
      ("do-sanitization", "Run post-parse sanitization", cxxopts::value<std::string>())
      ("do-remove-duplicate-cells", "Remove overprinted text cells (default: true)", cxxopts::value<std::string>())
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
    cli.glyph_bbox_cache_capacity =
      result["glyph-bbox-cache-capacity"].as<std::size_t>();
    cli.enable_timing = result["enable-timing"].as<bool>();
    cli.suppress_config = result["suppress-config"].as<bool>();
    cli.suppress_timings = result["suppress-timings"].as<bool>();
    cli.timing_csv = result["timing-csv"].as<std::string>();
    if(result.count("glyph-cache-diagnostics"))
      {
        cli.glyph_cache_diagnostics = result["glyph-cache-diagnostics"].as<std::string>();
      }
    cli.loglevel = result["loglevel"].as<std::string>();
    if(result.count("output-dir"))
      {
        cli.output_dir = result["output-dir"].as<std::string>();
      }

    if(result.count("max-pages"))
      {
        cli.max_pages = result["max-pages"].as<int>();
      }

    std::string raw_mode = result["mode"].as<std::string>();
    std::transform(raw_mode.begin(), raw_mode.end(), raw_mode.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if(raw_mode == "parse" or raw_mode == "parse-without-bitmaps")
      {
        cli.mode = run_mode::parse_without_bitmaps;
      }
    else if(raw_mode == "parse-with-bitmaps")
      {
        cli.mode = run_mode::parse_with_bitmaps;
      }
    else if(raw_mode == "render")
      {
        cli.mode = run_mode::render;
      }
    else if(raw_mode == "default")
      {
        cli.mode = run_mode::default_mode;
      }
    else if(raw_mode == "all")
      {
        cli.mode = run_mode::all;
      }
    else
      {
        throw std::runtime_error(
          "--mode must be parse-without-bitmaps, parse-with-bitmaps, render, "
          "default, or all");
      }

    if(cli.glyph_cache_diagnostics.has_value() and cli.mode != run_mode::render)
      {
        throw std::runtime_error("--glyph-cache-diagnostics requires --mode render");
      }

    if(result.count("page-boundary")) { decode_config.page_boundary = result["page-boundary"].as<std::string>(); }
    if(result.count("do-sanitization")) { decode_config.do_sanitization = parse_bool(result["do-sanitization"].as<std::string>()); }
    if(result.count("do-remove-duplicate-cells")) { decode_config.do_remove_duplicate_cells = parse_bool(result["do-remove-duplicate-cells"].as<std::string>()); }
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

      // Summary output is outside the measured interval, just like console
      // printing. Keep one stream for all modes/thread counts in this run.
      std::ofstream timing_output;
      if(cli.enable_timing and cli.suppress_timings)
        {
          const std::filesystem::path path = cli.timing_csv.string() + ".summary.log";
          if(cli.glyph_cache_diagnostics.has_value() and
             std::filesystem::weakly_canonical(*cli.glyph_cache_diagnostics) ==
               std::filesystem::weakly_canonical(path))
            {
              throw std::runtime_error("glyph-cache diagnostics and timing summary need different paths");
            }
          if(std::filesystem::exists(path))
            {
              throw std::runtime_error("timing summary already exists; choose a new --timing-csv path: "
                                       + path.string());
            }
          timing_output.open(path);
          if(not timing_output)
            {
              throw std::runtime_error("could not open timing summary: " + path.string());
            }
        }

      // One stream serves all requested thread counts in this invocation.
      // Refuse old output rather than appending another run to it.
      std::ofstream glyph_diagnostics;
      if(cli.glyph_cache_diagnostics.has_value())
        {
          if(cli.enable_timing and
             std::filesystem::weakly_canonical(*cli.glyph_cache_diagnostics) ==
               std::filesystem::weakly_canonical(cli.timing_csv))
            {
              throw std::runtime_error("glyph-cache diagnostics and timing CSV need different paths");
            }
          if(std::filesystem::exists(*cli.glyph_cache_diagnostics))
            {
              throw std::runtime_error("glyph-cache diagnostics already exist; choose a new path: "
                                       + cli.glyph_cache_diagnostics->string());
            }
          glyph_diagnostics.open(*cli.glyph_cache_diagnostics);
          if(not glyph_diagnostics)
            {
              throw std::runtime_error("could not open glyph-cache diagnostics: "
                                       + cli.glyph_cache_diagnostics->string());
            }
        }
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

      if(cli.output_dir.has_value())
        {
          std::filesystem::create_directories(*cli.output_dir);
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
      if(cli.mode == run_mode::render
         or cli.mode == run_mode::default_mode
         or cli.mode == run_mode::all)
        {
          std::cout << "Render scale: " << cli.scale << "\n";
        }
      std::cout << "\n";

      auto render_config = default_render_config(cli.scale);
      render_config.glyph_bbox_cache_capacity = cli.glyph_bbox_cache_capacity;

      std::vector<run_mode> modes;
      if(cli.mode == run_mode::all)
        {
          modes = {run_mode::parse_without_bitmaps,
                   run_mode::parse_with_bitmaps,
                   run_mode::render};
        }
      else if(cli.mode == run_mode::default_mode)
        {
          modes = {run_mode::parse_without_bitmaps, run_mode::render};
        }
      else
        {
          modes = {cli.mode};
        }

      if(not cli.suppress_config)
        {
          for(run_mode mode : modes)
            {
              std::cout << "Decode config (" << mode_to_string(mode) << "):\n"
                        << effective_decode_config(mode, decode_config, render_config).to_string()
                        << "\n";
            }
          if(cli.mode == run_mode::render
             or cli.mode == run_mode::default_mode
             or cli.mode == run_mode::all)
            {
              std::cout << "Render config:\n" << render_config.to_string();
            }
        }

      std::cout << "\nLoading documents ...\n";
      auto docs = load_documents(schedule);

      for(run_mode mode : modes)
        {
          const bool render = (mode == run_mode::render);
          const bool parse_with_bitmaps = mode == run_mode::parse_with_bitmaps;
          const std::string title = render ? "RENDER (decode + rasterise)"
            : parse_with_bitmaps ? "PARSE WITH BITMAPS (decode only)"
            : "PARSE WITHOUT BITMAPS (decode only)";
          const auto mode_decode_config =
            effective_decode_config(mode, decode_config, render_config);
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
                                           mode_decode_config,
                                           render ? std::optional<pdflib::render_config>(render_config)
                                                  : std::nullopt,
                                           render ? cli.output_dir : std::nullopt);
              benchmark_result result = benchmark.run(render ? "render" : "parse",
                                                      cli.enable_timing,
                                                      cli.timing_csv,
                                                      cli.glyph_cache_diagnostics.has_value()
                                                        ? &glyph_diagnostics : nullptr,
                                                      cli.enable_timing and cli.suppress_timings
                                                        ? &timing_output : nullptr);
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
