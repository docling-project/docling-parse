//-*-C++-*-

#include "parse.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct NullStreamEntry
{
  std::string pdf_path;
  int         page_number;    // 0-based internally, printed as 1-based
  std::size_t image_index;    // index within page_images
  std::string xobject_key;
  bool        raw_null;
  bool        decoded_null;
};

// -----------------------------------------------------------------
// Analyse a single PDF and append findings to `entries`.
// Returns the number of pages that contain at least one image with
// both streams null.
// -----------------------------------------------------------------
static int analyse_pdf(const std::string&           pdf_path,
                       std::vector<NullStreamEntry>& entries)
{
  pdflib::pdf_decoder<pdflib::DOCUMENT> doc;
  std::optional<std::string> password = std::nullopt;
  std::string mutable_path = pdf_path;

  if (not doc.process_document_from_file(mutable_path, password))
    {
      std::cerr << "[error] could not open: " << pdf_path << "\n";
      return 0;
    }

  int num_pages = doc.get_number_of_pages();

  pdflib::decode_config config;
  config.keep_bitmaps       = true;
  config.keep_char_cells    = false;
  config.keep_shapes        = false;
  config.do_sanitization    = false;
  config.create_word_cells  = false;
  config.create_line_cells  = false;

  std::unordered_set<int> flagged_pages;

  for (int page_num = 0; page_num < num_pages; page_num++)
    {
      auto page_dec = doc.decode_page(page_num, config);
      if (not page_dec)
        {
          continue;
        }

      auto& images = page_dec->get_page_images();
      for (std::size_t i = 0; i < images.size(); i++)
        {
          auto& img = images[i];

          bool raw_null     = (not img.raw_stream_data
                               or img.raw_stream_data->getSize() == 0);
          bool decoded_null = (not img.decoded_stream_data
                               or img.decoded_stream_data->getSize() == 0);

          if (raw_null and decoded_null)
            {
              entries.push_back({pdf_path,
                                 page_num,
                                 i,
                                 img.xobject_key,
                                 raw_null,
                                 decoded_null});
              flagged_pages.insert(page_num);
            }
        }
    }

  return static_cast<int>(flagged_pages.size());
}

// -----------------------------------------------------------------
// Collect PDF paths from either a single file or a directory.
// -----------------------------------------------------------------
static std::vector<fs::path> collect_pdfs(const fs::path& input)
{
  std::vector<fs::path> paths;

  if (fs::is_regular_file(input))
    {
      paths.push_back(input);
    }
  else if (fs::is_directory(input))
    {
      for (auto const& entry : fs::recursive_directory_iterator(input))
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
      std::cerr << "[error] input is neither a file nor a directory: "
                << input << "\n";
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
        ("i,input",    "Input PDF file or directory",    cxxopts::value<std::string>())
        ("o,output",   "Output JSON file (optional)",    cxxopts::value<std::string>())
        ("l,loglevel", "Log level [error, warning, info]", cxxopts::value<std::string>())
        ("h,help",     "Print usage");

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
          std::cerr << "[error] -i/--input is required\n";
          return 1;
        }

      fs::path input_path = result["input"].as<std::string>();
      std::vector<fs::path> pdf_paths = collect_pdfs(input_path);

      if (pdf_paths.empty())
        {
          std::cerr << "[error] no PDF files found at: " << input_path << "\n";
          return 1;
        }

      std::cout << "Analysing " << pdf_paths.size() << " PDF file(s)...\n\n";

      std::vector<NullStreamEntry> all_entries;
      int total_flagged_pages = 0;
      int total_pdfs_with_issues = 0;

      for (auto const& pdf : pdf_paths)
        {
          std::vector<NullStreamEntry> file_entries;
          int flagged = analyse_pdf(pdf.string(), file_entries);

          if (flagged > 0)
            {
              total_pdfs_with_issues++;
              total_flagged_pages += flagged;

              std::cout << "FILE: " << pdf.string() << "\n";
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
                            << "\n";
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
      std::cout << "  PDFs with issues   : " << total_pdfs_with_issues << "\n";
      std::cout << "  Pages with issues  : " << total_flagged_pages << "\n";
      std::cout << "  Null-stream images : " << all_entries.size() << "\n";

      // Optional JSON output
      if (result.count("output"))
        {
          nlohmann::json report = nlohmann::json::array();

          for (auto const& e : all_entries)
            {
              nlohmann::json entry;
              entry["pdf"]            = e.pdf_path;
              entry["page"]           = e.page_number + 1; // 1-based
              entry["image_index"]    = e.image_index;
              entry["xobject_key"]    = e.xobject_key;
              entry["raw_null"]       = e.raw_null;
              entry["decoded_null"]   = e.decoded_null;
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
              std::cerr << "[error] could not write to: " << out_path << "\n";
            }
        }

      return (total_pdfs_with_issues > 0) ? 2 : 0;
    }
  catch (cxxopts::exceptions::exception const& e)
    {
      std::cerr << "[error] option parsing: " << e.what() << "\n";
      return 1;
    }
  catch (std::exception const& e)
    {
      std::cerr << "[error] " << e.what() << "\n";
      return 1;
    }
}
