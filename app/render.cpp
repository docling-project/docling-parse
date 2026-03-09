//-*-C++-*-

#include "parse.h"
#include "render.h"

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

int main(int argc, char* argv[])
{
  int orig_argc = argc;

  // Initialize loguru
  loguru::init(argc, argv);

  bool do_sanitization = true;

  try
    {
      cxxopts::Options options("PDFRenderer", "A program to render PDF pages");

      // Define the options
      options.add_options()
	("i,input", "Input PDF file", cxxopts::value<std::string>())
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
	LOG_F(ERROR, "Input (-i) must be specified.");
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

      // Help option
      if (result.count("help")) {
	LOG_F(INFO, "%s", options.help().c_str());
	return 0;
      }

      // Retrieve and process the options
      if (result.count("input")) {

	std::string ifile = result["input"].as<std::string>();
	std::string ofile = ifile+".rendered.json";

	int page = result["page"].as<int>();
	LOG_F(INFO, "Page to process: %d", page);

	if (result.count("output")) {
	  ofile = result["output"].as<std::string>();
	  LOG_F(INFO, "Output file: %s", ofile.c_str());
	}
	else {
	  LOG_F(INFO, "No output file found, defaulting to %s", ofile.c_str());
	}

	// Initialize fonts
	{
	  nlohmann::json data;
	  std::string resource_dir = resource_utils::get_resources_dir(false).string();
	  data[pdflib::pdf_resource<pdflib::PAGE_FONT>::RESOURCE_DIR_KEY] = resource_dir;

	  std::unordered_map<std::string, double> font_timings;
	  pdflib::pdf_resource<pdflib::PAGE_FONT>::initialise(data, font_timings);
	}

	utils::timer timer;

	// Process PDF document
	pdflib::pdf_timings timings;
	pdflib::pdf_decoder<pdflib::DOCUMENT> doc(timings);

	std::optional<std::string> password;
	if (result.count("password")) {
	  password = result["password"].as<std::string>();
	}

	if (!doc.process_document_from_file(ifile, password)) {
	  LOG_S(ERROR) << "Failed to process: " << ifile;
	  return 1;
	}

	// Decode and render
	pdflib::renderer<pdflib::NAIVE> renderer;

	if (page == -1)
	  {
	    // Decode all pages
	    int num_pages = doc.get_number_of_pages();
	    for (int p = 0; p < num_pages; p++) {
	      pdflib::decode_config page_config;
	      page_config.page_boundary = "crop_box";
	      page_config.do_sanitization = do_sanitization;
	      page_config.keep_shapes = false;
	      page_config.keep_bitmaps = false;
	      auto page_decoder = doc.decode_page(p, page_config);
	      if (page_decoder) {
		auto& instructions = page_decoder->get_instructions();
		instructions.iterate_over_instructions(renderer);
	      }
	    }
	  }
	else
	  {
	    // Decode specific page
	    pdflib::decode_config page_config;
	    page_config.page_boundary = "crop_box";
	    page_config.do_sanitization = do_sanitization;
	    page_config.keep_shapes = false;
	    page_config.keep_bitmaps = false;
	    auto page_decoder = doc.decode_page(page, page_config);
	    if (page_decoder)
	      {
		auto& instructions = page_decoder->get_instructions();
		instructions.iterate_over_instructions(renderer);
	      }
	    else
	      {
		LOG_S(ERROR) << "Failed to decode page: " << page;
		return 1;
	      }
	  }
	
	LOG_S(INFO) << "total-time [sec]: " << timer.get_time();
	return 0;
      }
    }
  catch (const cxxopts::exceptions::exception& e)
    {
      LOG_F(ERROR, "Error parsing options: %s", e.what());
      return 1;
    }

  return 0;
}
