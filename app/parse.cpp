//-*-C++-*-

#include "parse.h"

void set_loglevel(std::string level)
{
  if(level=="info")
    {
      loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
      //set_verbosity(loguru::Verbosity_INFO);
    }
  else if(level=="warning")
    {
      loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
      //loguru::set_verbosity(loguru::Verbosity_WARNING);
    }
  else if(level=="error")
    {
      loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
      //loguru::set_verbosity(loguru::Verbosity_ERROR);
    }
  else if(level=="fatal")
    {
      loguru::g_stderr_verbosity = loguru::Verbosity_FATAL;
      //loguru::set_verbosity(loguru::Verbosity_ERROR);
    }
  else
    {
      loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;      
    }
}

nlohmann::json create_config(std::filesystem::path ifile,
                             std::filesystem::path ofile,
                             int page=-1,
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

    if(page!=-1)
      {
        std::vector<int> pages = {page};
        task["page-numbers"] = pages;
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

  bool do_sanitization = true;
  bool keep_shapes = true;
  bool keep_bitmaps = true;

  try {
    cxxopts::Options options("PDFProcessor", "A program to process PDF files or configuration files");

    // Define the options
    options.add_options()
      ("i,input", "Input PDF file", cxxopts::value<std::string>())
      ("c,config", "Config file", cxxopts::value<std::string>())
      ("create-config", "Create config file", cxxopts::value<std::string>())
      ("p,page", "Pages to process (default: -1 for all)", cxxopts::value<int>()->default_value("-1"))
      ("password", "Password for accessing encrypted, password-protected files", cxxopts::value<std::string>())
      ("o,output", "Output file", cxxopts::value<std::string>())
      ("export-images", "Export images to directory", cxxopts::value<std::string>())
      ("keep-text", "Keep text cells in output (default: true)", cxxopts::value<bool>()->default_value("true"))
      ("keep-shapes", "Keep shapes in output (default: true)", cxxopts::value<bool>()->default_value("true"))
      ("keep-bitmaps", "Keep bitmaps in output (default: true)", cxxopts::value<bool>()->default_value("true"))
      ("do-sanitation", "Do text sanitation (default: true)", cxxopts::value<bool>()->default_value("true"))
      ("l,loglevel", "loglevel [error;warning;success;info]", cxxopts::value<std::string>())
      ("h,help", "Print usage");

    // Parse command line arguments
    auto result = options.parse(argc, argv);

    // Check if either input or config file is provided (mandatory)
    if (orig_argc == 1) {
      LOG_S(INFO) << argc;
      LOG_F(ERROR, "Either input (-i) or config (-c) must be specified.");
      LOG_F(INFO, "%s", options.help().c_str());
      return 1;
    }

    std::string level = "warning";
    if (result.count("loglevel")){
      level = result["loglevel"].as<std::string>();

      // Convert the string to lowercase
      std::transform(level.begin(), level.end(), level.begin(), [](unsigned char c) {
        return std::tolower(c);
      });
      
      set_loglevel(level);
    }

    do_sanitization = result["do-sanitation"].as<bool>();
    bool keep_text = result["keep-text"].as<bool>();
    keep_shapes = result["keep-shapes"].as<bool>();
    keep_bitmaps = result["keep-bitmaps"].as<bool>();

    if (result.count("config")) {
      std::string config_file = result["config"].as<std::string>();
      LOG_F(INFO, "Config file: %s", config_file.c_str());

      pdflib::decode_page_config page_config;

      page_config.do_sanitization = do_sanitization;
      page_config.keep_char_cells = keep_text;
      page_config.keep_shapes = keep_shapes;
      page_config.keep_bitmaps = keep_bitmaps;

      std::cout << "decode_page_config:\n" << page_config.to_string() << std::endl;

      utils::timer timer;

      plib::parser parser;

      parser.parse(config_file, page_config);

      double total_time = timer.get_time();
      std::cout << "\ntimings:\n";
      for(const auto& [key, val] : parser.get_timings())
        {
          if(pdflib::pdf_timings::is_static_key(key))
            {
              std::cout << "  " << std::setw(48) << std::left << key << val << " [sec]\n";
            }
        }
      std::cout << std::setw(48) << std::left << "  total-time" << total_time << " [sec]" << std::endl;

      return 0;
    }

    if (result.count("create-config")) {

      std::string ifile = result["input"].as<std::string>();
      std::string ofile = "";

      int page = result["page"].as<int>();
      LOG_F(INFO, "Page to process: %d", page);

      if (result.count("output")) {
        ofile = result["output"].as<std::string>();
        LOG_F(INFO, "Output file: %s", ofile.c_str());
      }

      auto config = create_config(ifile, ofile, page);
      LOG_S(INFO) << "config: \n" << config.dump(2);
    }

    // Retrieve and process the options
    if (result.count("input")) {

      std::string ifile = result["input"].as<std::string>();
      std::string ofile = ifile+".json";

      int page = result["page"].as<int>();
      LOG_F(INFO, "Page to process: %d", page);

      if (result.count("output")) {
        ofile = result["output"].as<std::string>();
        LOG_F(INFO, "Output file: %s", ofile.c_str());
      }
      else {
        LOG_F(INFO, "No output file found, defaulting to %s", ofile.c_str());
      }

      auto config = create_config(ifile, ofile, page);
      LOG_S(INFO) << "config: \n" << config.dump(2);
      if (result.count("password")) {
        config["password"] = result["password"].as<std::string>();
      }

      pdflib::decode_page_config page_config;
      page_config.do_sanitization = do_sanitization;
      page_config.keep_char_cells = keep_text;
      page_config.keep_shapes = keep_shapes;
      page_config.keep_bitmaps = keep_bitmaps;

      std::cout << "decode_page_config:\n" << page_config.to_string() << std::endl;

      utils::timer timer;

      plib::parser parser(level);
      parser.parse(config, page_config);

      double total_time = timer.get_time();
      std::cout << "\ntimings:\n";
      for(const auto& [key, val] : parser.get_timings())
        {
          if(pdflib::pdf_timings::is_static_key(key))
            {
              std::cout << "  " << std::setw(48) << std::left << key << val << " [sec]\n";
            }
        }
      std::cout << std::setw(48) << std::left << "  total-time" << total_time << " [sec]" << std::endl;

      if (result.count("export-images")) {
        std::string images_dir = result["export-images"].as<std::string>();
        parser.export_images(images_dir, page);
      }

      return 0;
    }

    // Help option or no arguments provided
    if (result.count("help")) {
      LOG_F(INFO, "%s", options.help().c_str());
      return 0;
    }

    //} catch (const cxxopts::OptionException& e) {
  } catch (const cxxopts::exceptions::exception& e) {
    LOG_F(ERROR, "Error parsing options: %s", e.what());
    return 1;
  }

  return 0;
}
