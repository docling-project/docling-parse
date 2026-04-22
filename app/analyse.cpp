//-*-C++-*-

#include "parse.h"
#include "render.h"
#include "parse/utils/bitmap/bitmap_exporter.h"

namespace
{
std::filesystem::path page_pdf_output_path(std::filesystem::path const& image_path)
{
  std::filesystem::path pdf_path = image_path;
  pdf_path.replace_extension(".pdf");
  return pdf_path;
}
}

struct ImageIssue
{
  std::string pdf_path;
  int         page_number;         // 0-based internally, printed as 1-based
  std::size_t image_index;         // index within page_images
  std::string xobject_key;
  bool        raw_null;
  bool        decoded_null;
  bool        yellow_box;          // renderer would draw a yellow placeholder
  std::string rendered_page_file;  // path to rendered page image, if render_dir was given
};

// -----------------------------------------------------------------
// Lightweight inspector: mirrors the exact renderer condition for a
// yellow box and collects the xobject keys that would trigger it.
//
//   yellow box iff: not has_data()  OR  sh<=0  OR  sw<=0  OR  sc<1
// -----------------------------------------------------------------
struct yellow_box_inspector
{
  std::unordered_set<std::string> yellow_keys;

  void set_size(pdflib::size_instruction&)              {}
  void render_text(pdflib::text_instruction&)           {}
  void render_widget(pdflib::text_widget_instruction&)  {}
  void render_shape(pdflib::shape_instruction&)         {}

  void render_bitmap(pdflib::bitmap_instruction& instr)
  {
    auto const& shape = instr.get_shape();
    int sh = shape[0], sw = shape[1], sc = shape[2];
    if ((not instr.has_data()) or sh <= 0 or sw <= 0 or sc < 1)
      {
        yellow_keys.insert(instr.get_key());
      }
  }
};

// -----------------------------------------------------------------
// Analyse a single PDF and append findings to `entries`.
// Returns the number of pages that contain at least one image issue.
// `total_pages` is incremented by the page count of this document.
// -----------------------------------------------------------------
static int analyse_pdf(const std::string&      pdf_path,
                       std::vector<ImageIssue>& entries,
                       int&                     total_pages,
                       const std::string&       render_dir,
                       bool                     export_bitmaps,
                       bool                     export_page_pdf,
                       const std::string&       bitmap_dir,
                       int                      target_page)
{
  pdflib::pdf_decoder<pdflib::DOCUMENT> doc;
  std::optional<std::string> password = std::nullopt;
  std::string mutable_path = pdf_path;

  if (not doc.process_document_from_file(mutable_path, password))
    {
      LOG_S(ERROR) << "could not open: " << pdf_path;
      return 0;
    }

  int num_pages = doc.get_number_of_pages();
  total_pages += num_pages;

  // When rendering is requested we need the full instruction set.
  // When only analysing streams, skip cells/shapes to go faster.
  pdflib::decode_config config;
  config.keep_bitmaps      = true;
  config.keep_char_cells   = not render_dir.empty();
  config.keep_shapes       = not render_dir.empty();
  config.do_sanitization   = false;
  config.create_word_cells = false;
  config.create_line_cells = false;

  pdflib::render_config render_cfg; // default render settings
  std::filesystem::path good_render_dir;
  std::filesystem::path yellow_render_dir;

  if (not render_dir.empty())
    {
      good_render_dir   = std::filesystem::path(render_dir) / "good";
      yellow_render_dir = std::filesystem::path(render_dir) / "yellow";
      std::filesystem::create_directories(good_render_dir);
      std::filesystem::create_directories(yellow_render_dir);
    }

  std::unordered_set<int> flagged_pages;

  for (int page_num = 0; page_num < num_pages; page_num++)
    {
      if(target_page >= 0 and page_num != target_page)
        {
          continue;
        }

      std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE>> page_dec;

      try
        {
          page_dec = doc.decode_page(page_num, config);
        }
      catch (std::exception const& exc)
        {
          LOG_S(WARNING) << pdf_path << " page " << (page_num + 1)
                         << " decode failed: " << exc.what();
          continue;
        }

      if (not page_dec)
        {
          continue;
        }

      // Run the yellow-box inspector over all bitmap instructions on
      // this page — same condition the renderer uses.
      yellow_box_inspector inspector;
      page_dec->get_instructions().iterate_over_instructions(inspector);

      if(export_bitmaps)
        {
          pdflib::bitmap_export::bitmap_exporter_visitor exporter(
            std::filesystem::path(bitmap_dir),
            pdf_path,
            page_num);
          page_dec->get_instructions().iterate_over_instructions(exporter);
        }

      // Check every image on this page for stream / render issues.
      bool page_has_issue = false;
      auto& images = page_dec->get_page_images();
      for (std::size_t i = 0; i < images.size(); i++)
        {
          auto& img = images[i];

          bool raw_null     = (not img.raw_stream_data
                               or img.raw_stream_data->getSize() == 0);
          bool decoded_null = (not img.decoded_stream_data
                               or img.decoded_stream_data->getSize() == 0);

          bool yellow_box = (inspector.yellow_keys.count(img.xobject_key) > 0);

          if ((raw_null and decoded_null) or yellow_box)
            {
              // Compute the rendered page path now; flagged pages are written
              // below into the yellow/ subdirectory.
              std::string rendered_page_file;
              if (not render_dir.empty())
                {
                  std::string stem = std::filesystem::path(pdf_path).stem().string()
                    + "_p" + std::to_string(page_num) + ".png";
                  rendered_page_file = (yellow_render_dir / stem).string();
                }

              entries.push_back({pdf_path,
                                 page_num,
                                 i,
                                 img.xobject_key,
                                 raw_null,
                                 decoded_null,
                                 yellow_box,
                                 rendered_page_file});
              flagged_pages.insert(page_num);
              page_has_issue = true;
            }
        }

      // Render and save every page when rendering is requested.
      // Clean pages go to good/, flagged pages go to yellow/.
      if (not render_dir.empty())
        {
          std::string stem = std::filesystem::path(pdf_path).stem().string()
            + "_p" + std::to_string(page_num) + ".png";
          std::filesystem::path out_dir = page_has_issue ? yellow_render_dir : good_render_dir;
          std::string out_path = (out_dir / stem).string();

          try
            {
              pdflib::renderer<pdflib::BLEND2D> rnd(render_cfg);
              page_dec->get_instructions().iterate_over_instructions(rnd);
              rnd.save(out_path);
              if(export_page_pdf)
                {
                  page_dec->save_pdf_page(page_pdf_output_path(out_path));
                }
              LOG_S(INFO) << "saved rendered page: " << out_path;
            }
          catch (std::exception const& exc)
            {
              LOG_S(WARNING) << "could not render page " << (page_num + 1)
                             << " of " << pdf_path << ": " << exc.what();
            }
        }
    }

  return static_cast<int>(flagged_pages.size());
}

// -----------------------------------------------------------------
// Collect PDF paths from either a single file or a directory.
// -----------------------------------------------------------------
static std::vector<std::filesystem::path> collect_pdfs(const std::filesystem::path& input)
{
  std::vector<std::filesystem::path> paths;

  if (std::filesystem::is_regular_file(input))
    {
      paths.push_back(input);
    }
  else if (std::filesystem::is_directory(input))
    {
      for (auto const& entry : std::filesystem::recursive_directory_iterator(input))
        {
          if (entry.is_regular_file())
            {
              std::string ext = entry.path().extension().string();
              // Lowercase comparison
              std::transform(ext.begin(), ext.end(), ext.begin(),
                             [](unsigned char c) { return std::tolower(c); });
              if (ext == ".pdf")
                {
                  paths.push_back(entry.path());
                }
            }
        }
      std::sort(paths.begin(), paths.end());
    }
  else
    {
      LOG_S(ERROR) << "input is neither a file nor a directory: " << input.string();
    }

  return paths;
}

// -----------------------------------------------------------------
// main
// -----------------------------------------------------------------
int main(int argc, char* argv[])
{
  int orig_argc = argc;
  loguru::init(argc, argv);
  loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;

  try
    {
      cxxopts::Options options("analyse",
                               "Find pages with null raw_stream_data and "
                               "decoded_stream_data in PDF image XObjects");

      options.add_options()
        ("i,input",      "Input PDF file or directory",                    cxxopts::value<std::string>())
        ("o,output",     "Output JSON file (optional)",                    cxxopts::value<std::string>())
        ("r,render-dir", "Directory containing rendered page PNG images",  cxxopts::value<std::string>())
        ("p,page",       "Page number to analyse (0-based, default: -1 for all pages)",
                         cxxopts::value<int>()->default_value("-1"))
        ("export-bitmaps", "Export decoded bitmap payloads encountered on each page",
                           cxxopts::value<bool>()->implicit_value("true"))
        ("export-page-pdf", "Export each rendered page as a sibling PDF",
                            cxxopts::value<bool>()->implicit_value("true"))
        ("l,loglevel",   "Log level [error, warning, info]",               cxxopts::value<std::string>())
        ("h,help",       "Print usage");

      auto result = options.parse(argc, argv);

      if (orig_argc == 1 or result.count("help"))
        {
          std::cout << options.help() << "\n";
          return result.count("help") ? 0 : 1;
        }

      if (result.count("loglevel"))
        {
          std::string lvl = result["loglevel"].as<std::string>();
          std::transform(lvl.begin(), lvl.end(), lvl.begin(),
                         [](unsigned char c) { return std::tolower(c); });
          if      (lvl == "info")    { loguru::g_stderr_verbosity = loguru::Verbosity_INFO; }
          else if (lvl == "warning") { loguru::g_stderr_verbosity = loguru::Verbosity_WARNING; }
          else if (lvl == "error")   { loguru::g_stderr_verbosity = loguru::Verbosity_ERROR; }
        }

      if (not result.count("input"))
        {
          LOG_S(ERROR) << "-i/--input is required";
          return 1;
        }

      std::filesystem::path input_path = result["input"].as<std::string>();
      std::vector<std::filesystem::path> pdf_paths = collect_pdfs(input_path);
      int target_page = result["page"].as<int>();

      std::string render_dir;
      if (result.count("render-dir"))
        {
          render_dir = result["render-dir"].as<std::string>();
        }

      bool export_bitmaps = false;
      if(result.count("export-bitmaps"))
        {
          export_bitmaps = result["export-bitmaps"].as<bool>();
        }

      bool export_page_pdf = false;
      if(result.count("export-page-pdf"))
        {
          export_page_pdf = result["export-page-pdf"].as<bool>();
        }

      std::string bitmap_dir;
      if(export_bitmaps)
        {
          bitmap_dir = not render_dir.empty()
                     ? (std::filesystem::path(render_dir) / "bitmaps").string()
                     : "./bitmaps_out";
          LOG_S(INFO) << "exporting decoded bitmaps to: " << bitmap_dir;
        }

      if (pdf_paths.empty())
        {
          LOG_S(ERROR) << "no PDF files found at: " << input_path.string();
          return 1;
        }

      std::cout << "Analysing " << pdf_paths.size() << " PDF file(s)...\n\n";

      std::vector<ImageIssue> all_entries;
      int total_pages         = 0;
      int total_flagged_pages = 0;
      int total_pdfs_with_issues = 0;

      for (auto const& pdf : pdf_paths)
        {
          std::cout << "FILE: " << pdf.string() << "\n";

          std::vector<ImageIssue> file_entries;
          int flagged = 0;
          try
            {
              flagged = analyse_pdf(pdf.string(),
                                    file_entries,
                                    total_pages,
                                    render_dir,
                                    export_bitmaps,
                                    export_page_pdf,
                                    bitmap_dir,
                                    target_page);
            }
          catch (std::exception const& exc)
            {
              LOG_S(ERROR) << pdf.string() << ": " << exc.what();
              continue;
            }

          if (flagged > 0)
            {
              total_pdfs_with_issues++;
              total_flagged_pages += flagged;

              std::cout << "  => " << flagged << " page(s) with null-stream images:\n";

              int last_page = -1;
              for (auto const& e : file_entries)
                {
                  if (e.page_number != last_page)
                    {
                      std::cout << "  page " << (e.page_number + 1) << ":\n";
                      last_page = e.page_number;
                    }
                  std::cout << "    image[" << e.image_index << "]"
                            << "  xobj=" << (e.xobject_key.empty() ? "(none)" : e.xobject_key)
                            << "  raw=" << (e.raw_null ? "null" : "ok")
                            << "  decoded=" << (e.decoded_null ? "null" : "ok")
                            << "  yellow_box=" << (e.yellow_box ? "YES" : "no");
                  if (not e.rendered_page_file.empty())
                    {
                      std::cout << "  page_img=" << e.rendered_page_file;
                    }
                  std::cout << "\n";
                }
              std::cout << "\n";

              for (auto& e : file_entries)
                {
                  all_entries.push_back(std::move(e));
                }
            }
          else
            {
              std::cout << "OK: " << pdf.string() << "\n";
            }
        }

      // Summary
      std::cout << "\n=== Summary ===\n";
      std::cout << "  PDFs scanned       : " << pdf_paths.size() << "\n";
      std::cout << "  Total pages        : " << total_pages << "\n";
      std::cout << "  PDFs with issues   : " << total_pdfs_with_issues << "\n";
      std::cout << "  Pages with issues  : " << total_flagged_pages << "\n";
      std::cout << "  Images with issues : " << all_entries.size() << "\n";

      // Optional JSON output
      if (result.count("output"))
        {
          nlohmann::json report = nlohmann::json::array();

          for (auto const& e : all_entries)
            {
              nlohmann::json entry;
              entry["pdf"]                 = e.pdf_path;
              entry["page"]                = e.page_number + 1; // 1-based
              entry["image_index"]         = e.image_index;
              entry["xobject_key"]         = e.xobject_key;
              entry["raw_null"]            = e.raw_null;
              entry["decoded_null"]        = e.decoded_null;
              entry["yellow_box"]          = e.yellow_box;
              entry["rendered_page_file"]  = e.rendered_page_file;
              report.push_back(entry);
            }

          std::string out_path = result["output"].as<std::string>();
          std::ofstream ofs(out_path);
          if (ofs)
            {
              ofs << report.dump(2) << "\n";
              std::cout << "\nReport written to: " << out_path << "\n";
            }
          else
            {
              LOG_S(ERROR) << "could not write to: " << out_path;
            }
        }

      return (total_pdfs_with_issues > 0) ? 2 : 0;
    }
  catch (cxxopts::exceptions::exception const& e)
    {
      LOG_S(ERROR) << "option parsing: " << e.what();
      return 1;
    }
  catch (std::exception const& e)
    {
      LOG_S(ERROR) << e.what();
      return 1;
    }
}
