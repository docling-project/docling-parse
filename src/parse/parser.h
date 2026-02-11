//-*-C++-*-

#ifndef PARSER_PDF_H
#define PARSER_PDF_H

#include <parse.h>

namespace plib
{
  class parser
  {
  public:

    parser();
    parser(std::string loglevel);

    ~parser();

    void set_loglevel_with_label(std::string level);

    void parse(std::string filename, pdflib::decode_page_config page_config);
    void parse(nlohmann::json config, pdflib::decode_page_config page_config);

    bool initialise(nlohmann::json& data);

    // Export images from the last parsed document
    void export_images(std::string out_dir, int target_page=-1);

  private:

    void execute_parse(pdflib::decode_page_config page_config);

    bool parse_input(std::string filename);

    bool parse_file(std::string inp_filename,
                    std::string out_filename,
                    nlohmann::json& task,
		    pdflib::decode_page_config page_config,
                    bool pretty_print=true);

  private:

    nlohmann::json input_file;

    std::map<std::string, double> timings;

    // Persisted document decoder (from last parse_file call)
    std::shared_ptr<pdflib::pdf_decoder<pdflib::DOCUMENT>> document_decoder;
  };

  parser::parser()
  {
    //LOG_S(INFO) << "QPDF-version: " << QPDF::QPDFVersion();
  }

  parser::parser(std::string loglevel)
  {
    set_loglevel_with_label(loglevel);
    LOG_S(INFO) << "QPDF-version: " << QPDF::QPDFVersion();
  }
  
  parser::~parser()
  {}

  void parser::set_loglevel_with_label(std::string level)
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
  
  void parser::parse(std::string filename, pdflib::decode_page_config page_config)
  {
    if(not parse_input(filename))
      {
        return;
      }

    execute_parse(page_config);
  }

  void parser::parse(nlohmann::json config, pdflib::decode_page_config page_config)
  {
    input_file = config;

    execute_parse(page_config);
  }
  
  void parser::execute_parse(pdflib::decode_page_config page_config)
  {
    // initialise the fonts
    /*
      {
      utils::timer timer;

      assert(input_file.count("data")==1);
      pdflib::pdf_resource<pdflib::PAGE_FONT>::initialise(input_file["data"],
      timings);

      timings["fonts-initialisation"] = timer.get_time();
      }
    */
    initialise(input_file["data"]);

    // iterate over the files
    nlohmann::json files = input_file["files"];
    for(auto& pair : files.items())
      {
        LOG_S(INFO) << pair.key() << " : " << pair.value();

        nlohmann::json& val = pair.value();

        std::string inp_filename;
        inp_filename = val["filename"];

        std::string out_filename;
        if(val.count("output")==1)
          {
            out_filename = val["output"];
          }
        else
          {
            out_filename = inp_filename+".json";
          }

        std::ifstream ifs(inp_filename);
        if(ifs)
          {
            parse_file(inp_filename, out_filename, val, page_config);
          }
        else
          {
            LOG_S(ERROR) << "filename: " << inp_filename << " does not exists";
          }
      }
  }

  bool parser::parse_input(std::string filename)
  {
    std::ifstream ifs(filename);

    if(ifs)
      {
        ifs >> input_file;

        LOG_S(INFO) << "input-filename: " << filename;
        LOG_S(INFO) << "input: " << input_file.dump(2);
      }
    else
      {
        LOG_S(ERROR) << "input-filename: " << filename << " does not exists";
        return false;
      }

    return true;
  }

  bool parser::initialise(nlohmann::json& data)
  {
    if(timings.count("fonts-initialisation")==0)
      {
        utils::timer timer;

        pdflib::pdf_resource<pdflib::PAGE_FONT>::initialise(data, timings);

        timings["fonts-initialisation"] = timer.get_time();
      }

    return true;
  }

  bool parser::parse_file(std::string inp_filename,
                          std::string out_filename,
                          nlohmann::json& task,
			  pdflib::decode_page_config page_config,
			  bool pretty_print)
  {
    pdflib::pdf_timings pdf_timings;
    document_decoder = std::make_shared<pdflib::pdf_decoder<pdflib::DOCUMENT>>(pdf_timings);

    if(pdf_timings.has_key("fonts-initialisation"))
      {
        LOG_S(ERROR) << "fonts are not initialised";
        return false;
      }

    std::optional<std::string> password;
    if (input_file["password"].is_null())
      {
        password = std::nullopt;
      }
    else
      {
        password = input_file["password"];
      }
    if(not document_decoder->process_document_from_file(inp_filename, password))
      {
        LOG_S(ERROR) << "aborting the parse of file "<< inp_filename;
        return false;
      }

    if(task.count("page-numbers")==0)
      {
        document_decoder->decode_document(page_config);
      }
    else
      {
        std::vector<int> page_numbers = task["page-numbers"];
        document_decoder->decode_document(page_numbers, page_config);
      }

    // Build the output JSON from the typed API
    nlohmann::json json_document;

    json_document["info"]["filename"] = inp_filename;
    json_document["info"]["#-pages"] = document_decoder->get_number_of_pages();
    json_document["annotations"] = document_decoder->get_annotations();

    nlohmann::json json_pages = nlohmann::json::array({});
    for(int p = 0; p < document_decoder->get_number_of_pages(); ++p)
      {
	if(document_decoder->has_page_decoder(p))
	  {
	    auto page_dec = document_decoder->get_page_decoder(p);
	    json_pages.push_back(page_dec->get(page_config));
	  }
      }
    json_document["pages"] = json_pages;

    LOG_S(WARNING) << "writing to: " << out_filename;

    std::ofstream ofs(out_filename);
    if(pretty_print)
      {
        ofs << std::setw(2) << json_document;
      }
    else
      {
        ofs << json_document;
      }

    return true;
  }

  void parser::export_images(std::string out_dir, int target_page)
  {
    namespace fs = std::filesystem;

    if(not document_decoder)
      {
        LOG_S(ERROR) << "no document has been parsed yet";
        return;
      }

    fs::create_directories(out_dir);

    int num_pages = document_decoder->get_number_of_pages();
    int img_index = 0;

    for(int p = 0; p < num_pages; ++p)
      {
        if(target_page >= 0 and p != target_page)
          {
            continue;
          }

        if(not document_decoder->has_page_decoder(p))
          {
            continue;
          }

        auto page_dec = document_decoder->get_page_decoder(p);
        if(not page_dec)
          {
            continue;
          }

        auto& page_images = page_dec->get_page_images();
	LOG_S(INFO) << "page " << p << " has " << page_images.size() << " images.";
	
        for(size_t i = 0; i < page_images.size(); ++i)
          {
            auto& img = page_images[i];

            if(not img.raw_stream_data or img.raw_stream_data->getSize() == 0)
              {
		LOG_S(WARNING) << " -> found no buffer for image " << i; 
                continue;
              }

            std::string ext = img.get_image_extension();

            std::string safe_key = img.xobject_key;
            for(char& c : safe_key)
              {
                if(c == '/' or c == '\\' or c == ':' or c == '*'
                   or c == '?' or c == '"' or c == '<' or c == '>' or c == '|')
                  {
                    c = '_';
                  }
              }

            fs::path out_path = fs::path(out_dir) / (
                "page_" + std::to_string(p + 1) +
                "_xobj_" + safe_key +
                "_img_" + std::to_string(++img_index) +
                ext);

            img.save_to_file(out_path);

            LOG_S(INFO) << "wrote " << out_path.string()
                        << " (" << img.raw_stream_data->getSize() << " bytes"
                        << ", " << img.image_width << "x" << img.image_height << ")";

            if(img.decoded_stream_data and img.decoded_stream_data->getSize() > 0)
              {
                fs::path decoded_path = fs::path(out_dir) / (
                    "page_" + std::to_string(p + 1) +
                    "_xobj_" + safe_key +
                    "_img_" + std::to_string(img_index) +
                    "_decoded.bin");

                img.save_decoded_to_file(decoded_path);

                LOG_S(INFO) << "wrote " << decoded_path.string()
                            << " (" << img.decoded_stream_data->getSize() << " bytes, decoded)";
              }
          }
      }
  }

}

#endif
