//-*-C++-*-

#include "parse.h"
#include "render.h"
#include "parse/utils/bitmap/bitmap_exporter.h"

namespace
{
std::filesystem::path page_pdf_output_path(std::filesystem::path const& export_dir,
                                           std::filesystem::path const& pdf_path,
                                           int page)
{
  return export_dir / (pdf_path.stem().string()
                       + "_p" + std::to_string(page)
                       + ".pdf");
}
}

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
                       Renderer& rnd,
                       bool export_bitmaps,
                       std::filesystem::path const& bitmap_dir,
                       bool export_page_pdf_files,
                       std::filesystem::path const& page_pdf_dir,
                       std::string const& pdf_path)
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
              if(export_bitmaps)
                {
                  pdflib::bitmap_export::bitmap_exporter_visitor exporter(
                    bitmap_dir, pdf_path, p);
                  instructions.iterate_over_instructions(exporter);
                }
              if(export_page_pdf_files)
                {
                  page_decoder->save_pdf_page(page_pdf_output_path(page_pdf_dir,
                                                                   pdf_path,
                                                                   p));
                }
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
          if(export_bitmaps)
            {
              pdflib::bitmap_export::bitmap_exporter_visitor exporter(
                bitmap_dir, pdf_path, page);
              instructions.iterate_over_instructions(exporter);
            }
          if(export_page_pdf_files)
            {
              page_decoder->save_pdf_page(page_pdf_output_path(page_pdf_dir,
                                                               pdf_path,
                                                               page));
            }
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

// Render every page of a single PDF file, saving each page as a PNG.
// When output_dir is empty, pages are saved next to the source file as
// "<stem>_p<N>.png".  Returns the number of pages successfully rendered.
template<typename RenderCfg>
int render_pdf_file(const std::string& pdf_path,
                    const std::string& output_dir,
                    const pdflib::decode_config& page_config,
                    const RenderCfg& render_cfg,
                    bool save_output,
                    bool export_bitmaps,
                    const std::string& bitmap_dir,
                    bool export_page_pdf_files,
                    const std::string& page_pdf_dir)
{
  pdflib::pdf_timings timings;
  pdflib::pdf_decoder<pdflib::DOCUMENT> doc(timings);

  std::string pdf_path_copy = pdf_path;
  std::optional<std::string> no_password = std::nullopt;
  if (not doc.process_document_from_file(pdf_path_copy, no_password))
    {
      LOG_S(ERROR) << "Failed to open: " << pdf_path;
      return 0;
    }

  const int num_pages = doc.get_number_of_pages();
  int ok_count = 0;

  const std::filesystem::path src(pdf_path);
  const std::filesystem::path out_dir = output_dir.empty()
    ? src.parent_path()
    : std::filesystem::path(output_dir);

  for (int p = 0; p < num_pages; ++p)
    {
      auto page_decoder = doc.decode_page(p, page_config);
      if (not page_decoder)
        {
          LOG_S(ERROR) << "Failed to decode page " << p << " of " << pdf_path;
          continue;
        }

      pdflib::renderer<pdflib::BLEND2D> rnd(render_cfg);
      auto& instructions = page_decoder->get_instructions();
      if(export_bitmaps)
        {
          pdflib::bitmap_export::bitmap_exporter_visitor exporter(
            std::filesystem::path(bitmap_dir),
            pdf_path,
            p);
          instructions.iterate_over_instructions(exporter);
        }
      if(export_page_pdf_files)
        {
          page_decoder->save_pdf_page(page_pdf_output_path(std::filesystem::path(page_pdf_dir),
                                                           pdf_path,
                                                           p));
        }
      instructions.iterate_over_instructions(rnd);

      if (save_output)
        {
          const std::string stem = src.stem().string()
            + "_p" + std::to_string(p) + ".png";
          const std::string out_path = (out_dir / stem).string();
          try
            {
              rnd.save(out_path);
              LOG_S(INFO) << "saved: " << out_path;
            }
          catch (const std::exception& e)
            {
              LOG_S(ERROR) << "save failed for " << out_path << ": " << e.what();
            }
        }

      ++ok_count;
    }

  return ok_count;
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
	("d,directory","Input directory: render all PDFs found inside it",                  cxxopts::value<std::string>())
	("p,page",     "Pages to process (default: -1 for all)",                            cxxopts::value<int>()->default_value("-1"))
	("password",   "Password for encrypted files",                                      cxxopts::value<std::string>())
	("o,output",   "Output file or output directory (for -d mode)",                     cxxopts::value<std::string>())
	("r,renderer", "Renderer type [NAIVE, BLEND2D] (default: NAIVE)",                   cxxopts::value<std::string>()->default_value("BLEND2D"))
	("l,loglevel", "Log level [error, warning, info]",                                  cxxopts::value<std::string>())
	("h,help",     "Print usage")

        // ---- render_config ----
        ("render-text",    "Render glyph outlines for text cells (default: true)",          cxxopts::value<bool>()->implicit_value("true"))
        ("draw-text-bbox", "Draw bounding quad around each text cell",                      cxxopts::value<bool>()->implicit_value("true"))
        ("draw-text-basepoint", "Draw the text base point as a small red dot",              cxxopts::value<bool>()->implicit_value("true"))
        ("fit-glyph-bbox-to-target",
         "Uniformly rescale measured glyph outlines so the rendered bbox fits inside the target glyph bbox and matches either its width or height",
         cxxopts::value<bool>()->implicit_value("true"))
        ("resolve-fonts",           "Resolve PDF font names to system fonts (default: true)",                cxxopts::value<bool>()->implicit_value("true"))
        ("font-similarity-cutoff",  "Minimum Jaccard similarity for fuzzy font matching (default: 0.25)",    cxxopts::value<float>())
        ("scale",                   "Canvas scale in multiples of the PDF page size (-1 = disabled)",        cxxopts::value<float>())
        ("canvas-width",            "Canvas width in pixels (-1 = use page size)",                           cxxopts::value<int>())
        ("canvas-height",           "Canvas height in pixels (-1 = use page size)",                          cxxopts::value<int>())

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
        ("extract-font-programs", "Extract embedded font programs for rendering (default: true)", cxxopts::value<bool>()->implicit_value("true"))
        ("populate-json",      "Populate JSON objects during decode (default: false)",       cxxopts::value<bool>()->implicit_value("true"))
        ("export-bitmaps",     "Export decoded bitmap payloads encountered on each page (default: false)",
                               cxxopts::value<bool>()->default_value("false"))
        ("export-page-pdf",    "Export each selected page as a one-page PDF (default: false)",
                               cxxopts::value<bool>()->default_value("false"));

      // Parse command line arguments
      auto result = options.parse(argc, argv);

      // Check if either input or directory is provided (mandatory)
      if (orig_argc == 1 or
          (not result.count("input") and not result.count("directory")))
        {
          LOG_F(ERROR, "Either input (-i) or directory (-d) must be specified.");
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

      // --- Initialize fonts (shared by both -i and -d modes) ---
      {
        nlohmann::json data;
        std::string resource_dir = resource_utils::get_resources_dir(false).string();
        data[pdflib::pdf_resource<pdflib::PAGE_FONT>::RESOURCE_DIR_KEY] = resource_dir;
        std::unordered_map<std::string, double> font_timings;
        pdflib::pdf_resource<pdflib::PAGE_FONT>::initialise(data, font_timings);
      }

      std::string renderer_type = result["renderer"].as<std::string>();
      std::transform(renderer_type.begin(), renderer_type.end(), renderer_type.begin(),
                     [](unsigned char c) { return std::toupper(c); });

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
      // This app always renders, so embedded font extraction defaults to on.
      page_config.extract_font_programs = true;
      if (result.count("extract-font-programs"))    { page_config.extract_font_programs     = result["extract-font-programs"].as<bool>(); }
      if (result.count("populate-json"))            { page_config.populate_json_objects      = result["populate-json"].as<bool>(); }
      bool export_bitmaps = result["export-bitmaps"].as<bool>();
      bool export_page_pdf_files = result["export-page-pdf"].as<bool>();

      // --- render_config ---
      pdflib::render_config cfg;
      if (result.count("render-text"))    { cfg.render_text    = result["render-text"].as<bool>(); }
      if (result.count("draw-text-bbox")) { cfg.draw_text_bbox = result["draw-text-bbox"].as<bool>(); }
      if (result.count("draw-text-basepoint")) { cfg.draw_text_basepoint = result["draw-text-basepoint"].as<bool>(); }
      if (result.count("fit-glyph-bbox-to-target")) { cfg.fit_glyph_bbox_to_target = result["fit-glyph-bbox-to-target"].as<bool>(); }
      if (result.count("resolve-fonts"))          { cfg.resolve_fonts          = result["resolve-fonts"].as<bool>(); }
      if (result.count("font-similarity-cutoff")) { cfg.font_similarity_cutoff = result["font-similarity-cutoff"].as<float>(); }
      if (result.count("scale"))                  { cfg.scale                  = result["scale"].as<float>(); }
      if (result.count("canvas-width"))           { cfg.canvas_width           = result["canvas-width"].as<int>(); }
      if (result.count("canvas-height"))          { cfg.canvas_height          = result["canvas-height"].as<int>(); }

      utils::timer timer;

      // --- single-file mode (-i) ---
      if (result.count("input"))
        {
          std::string ifile = result["input"].as<std::string>();
          std::string ofile = ifile + ".rendered.json";
          std::string bitmap_dir = "./bitmaps_out";
          std::string page_pdf_dir = "./pages_out";
          if(export_bitmaps and result.count("output"))
            {
              std::filesystem::path output_path = result["output"].as<std::string>();
              if(output_path.extension().empty())
                {
                  bitmap_dir = (output_path / "bitmaps").string();
                }
            }
          if(export_page_pdf_files and result.count("output"))
            {
              std::filesystem::path output_path = result["output"].as<std::string>();
              if(output_path.extension().empty())
                {
                  page_pdf_dir = (output_path / "pages").string();
                }
            }

          int page = result["page"].as<int>();
          LOG_F(INFO, "Page to process: %d", page);

          if (result.count("output"))
            {
              ofile = result["output"].as<std::string>();
              LOG_F(INFO, "Output file: %s", ofile.c_str());
            }
          else
            {
              LOG_F(INFO, "No output file found, defaulting to %s", ofile.c_str());
            }

          pdflib::pdf_timings timings;
          pdflib::pdf_decoder<pdflib::DOCUMENT> doc(timings);

          std::optional<std::string> password;
          if (result.count("password"))
            {
              password = result["password"].as<std::string>();
            }

          if (not doc.process_document_from_file(ifile, password))
            {
              LOG_S(ERROR) << "Failed to process: " << ifile;
              return 1;
            }

          if (renderer_type == "BLEND2D")
            {
              pdflib::renderer<pdflib::BLEND2D> rnd(cfg);
              if (not decode_and_render(doc, page, page_config, rnd,
                                        export_bitmaps,
                                        std::filesystem::path(bitmap_dir),
                                        export_page_pdf_files,
                                        std::filesystem::path(page_pdf_dir),
                                        ifile)) { return 1; }
              rnd.show();
            }
          else
            {
              pdflib::renderer<pdflib::NAIVE> rnd(cfg);
              if (not decode_and_render(doc, page, page_config, rnd,
                                        export_bitmaps,
                                        std::filesystem::path(bitmap_dir),
                                        export_page_pdf_files,
                                        std::filesystem::path(page_pdf_dir),
                                        ifile)) { return 1; }
            }

          LOG_S(INFO) << "total-time [sec]: " << timer.get_time();
          return 0;
        }

      // --- directory mode (-d) ---
      if (result.count("directory"))
        {
          const std::string dir_path = result["directory"].as<std::string>();
          const std::string out_dir  = result.count("output")
            ? result["output"].as<std::string>() : "";
          const bool save = not out_dir.empty();
          const std::string bitmap_dir = save
            ? (std::filesystem::path(out_dir) / "bitmaps").string()
            : "./bitmaps_out";
          const std::string page_pdf_dir = save
            ? (std::filesystem::path(out_dir) / "pages").string()
            : "./pages_out";

          if (not std::filesystem::is_directory(dir_path))
            {
              LOG_S(ERROR) << "Not a directory: " << dir_path;
              return 1;
            }

          if (save and not std::filesystem::exists(out_dir))
            {
              std::filesystem::create_directories(out_dir);
            }

          int total_pages = 0;
          int failed_files = 0;

          for (const auto& entry :
               std::filesystem::directory_iterator(dir_path))
            {
              if (entry.path().extension() != ".pdf") { continue; }

              const std::string pdf_path = entry.path().string();
              LOG_S(INFO) << "rendering: " << pdf_path;

              const int pages = render_pdf_file(pdf_path,
                                                out_dir,
                                                page_config,
                                                cfg,
                                                save,
                                                export_bitmaps,
                                                bitmap_dir,
                                                export_page_pdf_files,
                                                page_pdf_dir);
              if (pages == 0)
                {
                  ++failed_files;
                }
              else
                {
                  total_pages += pages;
                }
            }

          LOG_S(WARNING) << "directory mode done:"
                         << " total_pages=" << total_pages
                         << " failed_files=" << failed_files
                         << " time=" << timer.get_time() << "s";
          return (failed_files > 0) ? 1 : 0;
        }
    }
  catch (const cxxopts::exceptions::exception& e)
    {
      LOG_F(ERROR, "Error parsing options: %s", e.what());
      return 1;
    }

  return 0;
}
