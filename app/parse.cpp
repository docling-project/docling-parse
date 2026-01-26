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
    
    if (result.count("config")) {
      std::string config_file = result["config"].as<std::string>();
      LOG_F(INFO, "Config file: %s", config_file.c_str());

      utils::timer timer;

      plib::parser parser;
      parser.parse(config_file, do_sanitization);

      LOG_S(INFO) << "total-time [sec]: " << timer.get_time();
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

      utils::timer timer;

      plib::parser parser(level);
      parser.parse(config, do_sanitization);

      LOG_S(INFO) << "total-time [sec]: " << timer.get_time();
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
