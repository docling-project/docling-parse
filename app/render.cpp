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

template<typename Renderer>
bool decode_and_render(pdflib::pdf_decoder<pdflib::DOCUMENT>& doc,
                       int page,
                       const pdflib::decode_config& page_config,
                       Renderer& rnd)
{
  if (page == -1)
    {
      int num_pages = doc.get_number_of_pages();
      for (int p = 0; p < num_pages; p++)
        {
          auto page_decoder = doc.decode_page(p, page_config);
          if (page_decoder)
            {
              auto& instructions = page_decoder->get_instructions();
              instructions.iterate_over_instructions(rnd);
            }
        }
    }
  else
    {
      auto page_decoder = doc.decode_page(page, page_config);
      if (page_decoder)
        {
          auto& instructions = page_decoder->get_instructions();
          instructions.iterate_over_instructions(rnd);
        }
      else
        {
          LOG_S(ERROR) << "Failed to decode page: " << page;
          return false;
        }
    }
  return true;
}

int main(int argc, char* argv[])
{
  int orig_argc = argc;

  // Initialize loguru
  loguru::init(argc, argv);

  try
    {
      cxxopts::Options options("PDFRenderer", "A program to render PDF pages");

      // Define the options
      options.add_options()
	("i,input",    "Input PDF file",                                                    cxxopts::value<std::string>())
	("p,page",     "Pages to process (default: -1 for all)",                            cxxopts::value<int>()->default_value("-1"))
	("password",   "Password for encrypted files",                                      cxxopts::value<std::string>())
	("o,output",   "Output file",                                                       cxxopts::value<std::string>())
	("r,renderer", "Renderer type [NAIVE, BLEND2D] (default: NAIVE)",                   cxxopts::value<std::string>()->default_value("NAIVE"))
	("l,loglevel", "Log level [error, warning, info]",                                  cxxopts::value<std::string>())
	("h,help",     "Print usage")

        // ---- blend2d_render_config ----
        ("draw-text-bbox", "Draw bounding quad around each text cell",                      cxxopts::value<bool>()->implicit_value("true"))
        ("resolve-fonts",  "Resolve PDF font names to system fonts (default: true)",        cxxopts::value<bool>()->implicit_value("true"))
        ("canvas-width",   "Canvas width in pixels (-1 = use page size)",                   cxxopts::value<int>())
        ("canvas-height",  "Canvas height in pixels (-1 = use page size)",                  cxxopts::value<int>())

        // ---- decode_config ----
        ("page-boundary",   "Page boundary [crop_box, media_box, ...] (default: crop_box)", cxxopts::value<std::string>())
        ("do-sanitization", "Run post-parse sanitization (default: true)",                  cxxopts::value<bool>()->implicit_value("true"))
        ("keep-char-cells", "Keep individual character cells (default: true)",              cxxopts::value<bool>()->implicit_value("true"))
        ("keep-shapes",     "Keep shape items (default: true)",                             cxxopts::value<bool>()->implicit_value("true"))
        ("keep-bitmaps",    "Keep bitmap items (default: true)",                            cxxopts::value<bool>()->implicit_value("true"))
        ("max-num-lines",   "Cap on number of lines per page (-1 = no cap)",                cxxopts::value<int>())
        ("max-num-bitmaps", "Cap on number of bitmaps per page (-1 = no cap)",              cxxopts::value<int>())
        ("create-word-cells",  "Build word-level cells (default: true)",                    cxxopts::value<bool>()->implicit_value("true"))
        ("create-line-cells",  "Build line-level cells (default: true)",                    cxxopts::value<bool>()->implicit_value("true"))
        ("enforce-same-font",  "Require same font within a word/line cell (default: true)", cxxopts::value<bool>()->implicit_value("true"))
        ("horizontal-cell-tolerance", "Horizontal merge tolerance (default: 1.0)",          cxxopts::value<double>())
        ("word-space-factor",  "Space-width factor for word merging (default: 0.33)",       cxxopts::value<double>())
        ("line-space-factor",  "Space-width factor for line merging (default: 1.0)",        cxxopts::value<double>())
        ("line-space-factor-with-space", "Space-width factor for line merging with space (default: 0.33)", cxxopts::value<double>())
        ("keep-glyphs",        "Keep unmapped GLYPH<...> tokens (default: false)",          cxxopts::value<bool>()->implicit_value("true"))
        ("keep-qpdf-warnings", "Emit QPDF warnings (default: false)",                       cxxopts::value<bool>()->implicit_value("true"))
        ("populate-json",      "Populate JSON objects during decode (default: false)",       cxxopts::value<bool>()->implicit_value("true"));

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

	std::string renderer_type = result["renderer"].as<std::string>();
	std::transform(renderer_type.begin(), renderer_type.end(), renderer_type.begin(),
		       [](unsigned char c) { return std::toupper(c); });

	// --- decode_config ---
	pdflib::decode_config page_config; // start from struct defaults
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

	if (renderer_type == "BLEND2D")
	  {
	    // --- blend2d_render_config ---
	    pdflib::blend2d_render_config cfg; // start from struct defaults
	    if (result.count("draw-text-bbox")) { cfg.draw_text_bbox = result["draw-text-bbox"].as<bool>(); }
	    if (result.count("resolve-fonts"))  { cfg.resolve_fonts  = result["resolve-fonts"].as<bool>(); }
	    if (result.count("canvas-width"))   { cfg.canvas_width   = result["canvas-width"].as<int>(); }
	    if (result.count("canvas-height"))  { cfg.canvas_height  = result["canvas-height"].as<int>(); }

	    pdflib::renderer<pdflib::BLEND2D> rnd(cfg);
	    if (!decode_and_render(doc, page, page_config, rnd)) { return 1; }
	    rnd.show();
	  }
	else
	  {
	    pdflib::renderer<pdflib::NAIVE> rnd;
	    if (!decode_and_render(doc, page, page_config, rnd)) { return 1; }
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
