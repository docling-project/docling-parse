//-*-C++-*-

#include "parse.h"

#include <sstream>

void set_loglevel(std::string level)
{
  if(level=="info")
    {
      loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
    }
  else if(level=="warning")
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

std::optional<std::vector<int>> parse_page_selection(const cxxopts::ParseResult& result)
{
  const bool has_page = result.count("page") && result["page"].as<int>() != -1;
  const bool has_page_range = result.count("page-range");

  if(has_page && has_page_range)
    {
      throw std::runtime_error("Use either --page or --page-range, not both");
    }

  if(has_page)
    {
      return std::vector<int>{result["page"].as<int>()};
    }

  if(!has_page_range)
    {
      return std::nullopt;
    }

  std::string raw = result["page-range"].as<std::string>();
  size_t dash = raw.find('-');
  if(dash == std::string::npos)
    {
      throw std::runtime_error("Page range must have form start-end");
    }

  int start = std::stoi(raw.substr(0, dash));
  int end = std::stoi(raw.substr(dash + 1));
  if(end < start)
    {
      throw std::runtime_error("Page range end must be >= start");
    }

  std::vector<int> pages;
  pages.reserve(static_cast<size_t>(end - start + 1));
  for(int page = start; page <= end; ++page)
    {
      pages.push_back(page);
    }
  return pages;
}

nlohmann::json create_config(std::filesystem::path ifile,
                             std::filesystem::path ofile,
                             std::optional<std::vector<int>> pages=std::nullopt,
                             std::filesystem::path pdf_resource_dir="../docling_parse/pdf_resources/")
{
  nlohmann::json config = nlohmann::json::object({});

  auto data = nlohmann::json::object({});
  data["pdf-resource-directory"] = pdf_resource_dir;

  auto tasks = nlohmann::json::array({});
  {
    auto task = nlohmann::json::object({});
    task["filename"] = ifile;

    if(ofile.string()!="")
      {
        task["output"] = ofile;
      }

    if(pages.has_value())
      {
        task["page-numbers"] = *pages;
      }

    tasks.push_back(task);
  }

  config["data"] = data;
  config["files"] = tasks;

  return config;
}

int main(int argc, char* argv[]) {
  int orig_argc = argc;

  // Initialize loguru
  loguru::init(argc, argv);

  try {
    cxxopts::Options options("PDFProcessor", "A program to process PDF files or configuration files");

    // Define the options
    options.add_options()
      ("i,input",        "Input PDF file",                                                        cxxopts::value<std::string>())
      ("c,config",       "Config file",                                                           cxxopts::value<std::string>())
      ("create-config",  "Create config file",                                                    cxxopts::value<std::string>())
      ("p,page",         "Pages to process (default: -1 for all)",                               cxxopts::value<int>()->default_value("-1"))
      ("page-range",     "Inclusive page range to process, e.g. 10-20",                          cxxopts::value<std::string>())
      ("password",       "Password for accessing encrypted, password-protected files",            cxxopts::value<std::string>())
      ("o,output",       "Output file",                                                           cxxopts::value<std::string>())
      ("export-images",  "Export images to directory",                                            cxxopts::value<std::string>())
      ("print-cells",    "Print cells to stdout [char, word, line, all] (default: none)",        cxxopts::value<std::string>())
      ("l,loglevel",     "Log level [error, warning, info]",                                     cxxopts::value<std::string>())
      ("h,help",         "Print usage")

      // ---- decode_config ----
      ("page-boundary",   "Page boundary [crop_box, media_box, ...] (default: crop_box)",        cxxopts::value<std::string>())
      ("do-sanitization", "Run post-parse sanitization (default: true)",                         cxxopts::value<bool>()->implicit_value("true"))
      ("keep-char-cells", "Keep individual character cells (default: true)",                     cxxopts::value<bool>()->implicit_value("true"))
      ("keep-shapes",     "Keep shape items (default: true)",                                    cxxopts::value<bool>()->implicit_value("true"))
      ("keep-bitmaps",    "Keep bitmap items (default: true)",                                   cxxopts::value<bool>()->implicit_value("true"))
      ("max-num-lines",   "Cap on number of lines per page (-1 = no cap)",                       cxxopts::value<int>())
      ("max-num-bitmaps", "Cap on number of bitmaps per page (-1 = no cap)",                    cxxopts::value<int>())
      ("create-word-cells",  "Build word-level cells (default: true)",                           cxxopts::value<bool>()->implicit_value("true"))
      ("create-line-cells",  "Build line-level cells (default: true)",                           cxxopts::value<bool>()->implicit_value("true"))
      ("enforce-same-font",  "Require same font within a word/line cell (default: true)",        cxxopts::value<bool>()->implicit_value("true"))
      ("horizontal-cell-tolerance", "Horizontal merge tolerance (default: 1.0)",                 cxxopts::value<double>())
      ("word-space-factor",  "Space-width factor for word merging (default: 0.33)",              cxxopts::value<double>())
      ("line-space-factor",  "Space-width factor for line merging (default: 1.0)",               cxxopts::value<double>())
      ("line-space-factor-with-space", "Space-width factor for line merging with space (default: 0.33)", cxxopts::value<double>())
      ("keep-glyphs",        "Keep unmapped GLYPH<...> tokens (default: false)",                 cxxopts::value<bool>()->implicit_value("true"))
      ("keep-qpdf-warnings", "Emit QPDF warnings (default: false)",                              cxxopts::value<bool>()->implicit_value("true"))
      ("populate-json",      "Populate JSON objects during decode (default: false)",              cxxopts::value<bool>()->implicit_value("true"));

    // Parse command line arguments
    auto result = options.parse(argc, argv);

    // Check if either input or config file is provided (mandatory)
    if (orig_argc == 1) {
      LOG_F(ERROR, "Either input (-i) or config (-c) must be specified.");
      LOG_F(INFO, "%s", options.help().c_str());
      return 1;
    }

    std::string level = "warning";
    if (result.count("loglevel")) {
      level = result["loglevel"].as<std::string>();

      // Convert the string to lowercase
      std::transform(level.begin(), level.end(), level.begin(), [](unsigned char c) {
        return std::tolower(c);
      });

      set_loglevel(level);
    }

    // Help option or no arguments provided
    if (result.count("help")) {
      LOG_F(INFO, "%s", options.help().c_str());
      return 0;
    }

    // --- decode_config ---
    pdflib::decode_config page_config;
    if (result.count("page-boundary"))            { page_config.page_boundary             = result["page-boundary"].as<std::string>(); }
    if (result.count("do-sanitization"))          { page_config.do_sanitization            = result["do-sanitization"].as<bool>(); }
    if (result.count("keep-char-cells"))          { page_config.keep_char_cells            = result["keep-char-cells"].as<bool>(); }
    if (result.count("keep-shapes"))              { page_config.keep_shapes                = result["keep-shapes"].as<bool>(); }
    if (result.count("keep-bitmaps"))             { page_config.keep_bitmaps               = result["keep-bitmaps"].as<bool>(); }
    if (result.count("max-num-lines"))            { page_config.max_num_lines              = result["max-num-lines"].as<int>(); }
    if (result.count("max-num-bitmaps"))          { page_config.max_num_bitmaps            = result["max-num-bitmaps"].as<int>(); }
    if (result.count("create-word-cells"))        { page_config.create_word_cells          = result["create-word-cells"].as<bool>(); }
    if (result.count("create-line-cells"))        { page_config.create_line_cells          = result["create-line-cells"].as<bool>(); }
    if (result.count("enforce-same-font"))        { page_config.enforce_same_font          = result["enforce-same-font"].as<bool>(); }
    if (result.count("horizontal-cell-tolerance")){ page_config.horizontal_cell_tolerance  = result["horizontal-cell-tolerance"].as<double>(); }
    if (result.count("word-space-factor"))        { page_config.word_space_width_factor_for_merge = result["word-space-factor"].as<double>(); }
    if (result.count("line-space-factor"))        { page_config.line_space_width_factor_for_merge = result["line-space-factor"].as<double>(); }
    if (result.count("line-space-factor-with-space")) { page_config.line_space_width_factor_for_merge_with_space = result["line-space-factor-with-space"].as<double>(); }
    if (result.count("keep-glyphs"))              { page_config.keep_glyphs               = result["keep-glyphs"].as<bool>(); }
    if (result.count("keep-qpdf-warnings"))       { page_config.keep_qpdf_warnings        = result["keep-qpdf-warnings"].as<bool>(); }
    if (result.count("populate-json"))            { page_config.populate_json_objects      = result["populate-json"].as<bool>(); }

    if (result.count("config")) {
      std::string config_file = result["config"].as<std::string>();
      LOG_F(INFO, "Config file: %s", config_file.c_str());

      std::cout << "decode_config:\n" << page_config.to_string() << std::endl;

      utils::timer timer;

      plib::parser parser;

      parser.parse(config_file, page_config);

      double total_time = timer.get_time();
      std::cout << "\ntimings:\n"
                << pdflib::pdf_timings::format_tree_table(parser.get_timings(), total_time)
                << std::endl;

      return 0;
    }

    if (result.count("create-config")) {

      std::string ifile = result["input"].as<std::string>();
      std::string ofile = "";

      auto pages = parse_page_selection(result);

      if (result.count("output")) {
        ofile = result["output"].as<std::string>();
        LOG_F(INFO, "Output file: %s", ofile.c_str());
      }

      auto config = create_config(ifile, ofile, pages);
      LOG_S(INFO) << "config: \n" << config.dump(2);
    }

    // Retrieve and process the options
    if (result.count("input")) {

      std::string ifile = result["input"].as<std::string>();
      std::string ofile = ifile+".json";

      auto pages = parse_page_selection(result);

      if (result.count("output")) {
        ofile = result["output"].as<std::string>();
        LOG_F(INFO, "Output file: %s", ofile.c_str());
      }
      else {
        LOG_F(INFO, "No output file found, defaulting to %s", ofile.c_str());
      }

      auto config = create_config(ifile, ofile, pages);
      LOG_S(INFO) << "config: \n" << config.dump(2);
      if (result.count("password")) {
        config["password"] = result["password"].as<std::string>();
      }

      std::cout << "decode_config:\n" << page_config.to_string() << std::endl;

      utils::timer timer;

      plib::parser parser(level);
      parser.parse(config, page_config);

      double total_time = timer.get_time();
      std::cout << "\ntimings:\n"
                << pdflib::pdf_timings::format_tree_table(parser.get_timings(), total_time)
                << std::endl;

      if (result.count("print-cells")) {
        std::string mode = result["print-cells"].as<std::string>();
        parser.print_cells(mode);
      }

      if (result.count("export-images")) {
        std::string images_dir = result["export-images"].as<std::string>();
        parser.export_images(images_dir, result["page"].as<int>());
      }

      return 0;
    }

  } catch (const cxxopts::exceptions::exception& e) {
    LOG_F(ERROR, "Error parsing options: %s", e.what());
    return 1;
  }

  return 0;
}
