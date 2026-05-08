//-*-C++-*-

/*
example input:

{
    "data":
    {

    }
}
*/

#include <parse.h>

nlohmann::json read_input(std::string filename)
{
  nlohmann::json input;

  std::ifstream ifs(filename);
  
  if(ifs)
    {
      ifs >> input;
      
      LOG_S(INFO) << "input-filename: " << filename;    
      LOG_S(INFO) << "input: " << input.dump(2);    
    }
  else
    {
      LOG_S(FATAL) << "input-filename: " << filename << " does not exists";
    }

  return input;
}

int main(int argc, char *argv[])
{
  std::string glyphs_dir =  "../docling_parse/pdf_resources/glyphs";
  std::string cids_dir = "../docling_parse/pdf_resources/cmap-resources";
  std::string encodings_dir = "../docling_parse/pdf_resources/encodings";
  std::string fonts_dir = "../docling_parse/pdf_resources/fonts";
  
  loguru::init(argc, argv);

  pdflib::font_cids cids;
  cids.initialise(cids_dir);
  cids.decode_all();
  
  pdflib::font_glyphs glyphs;
  glyphs.initialise(glyphs_dir);

  pdflib::font_encodings encodings;
  encodings.initialise(encodings_dir, glyphs);
  
  pdflib::base_fonts fonts;
  fonts.initialise(fonts_dir, glyphs);

  fonts.verify_all();
  glyphs.print_unknown_glyphs();

  return 0;
}
