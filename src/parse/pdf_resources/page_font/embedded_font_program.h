//-*-C++-*-

#ifndef EMBEDDED_FONT_PROGRAM_H
#define EMBEDDED_FONT_PROGRAM_H

namespace pdflib
{

  class embedded_font_program
  {
  public:
    bool found = false;
    embedded_font_file_kind kind = FONT_FILE_NONE;

    std::string source_path;
    std::string declared_subtype;
    std::string base_font;
    std::string font_name;

    bool from_descendant_font = false;

    nlohmann::json descriptor_json;
    nlohmann::json stream_dict_json;
    std::vector<qpdf_stream_instruction> decoded_stream;

    int length  = -1;
    int length1 = -1;
    int length2 = -1;
    int length3 = -1;

    std::shared_ptr<Buffer> raw_data;
    std::shared_ptr<Buffer> decoded_data;

    size_t raw_size = 0;
    size_t decoded_size = 0;
  };

}

#endif
