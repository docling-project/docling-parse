//-*-C++-*-

#ifndef PARSE_UTILS_BITMAP_BITMAP_EXPORTER_H
#define PARSE_UTILS_BITMAP_BITMAP_EXPORTER_H

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include <parse/page_items/render_instructions.h>
#include <parse/utils/ccitt/ccitt_utils.h>
#include <parse/utils/jpeg/jpeg_utils.h>

namespace pdflib
{
namespace bitmap_export
{

  inline std::string sanitize_path_component(std::string value)
  {
    for(char& c : value)
      {
        if(c == '/' or c == '\\' or c == ':' or c == '*'
           or c == '?' or c == '"' or c == '<' or c == '>' or c == '|')
          {
            c = '_';
          }
      }

    if(value.empty())
      {
        value = "bitmap";
      }

    return value;
  }

  inline bool export_bitmap_instruction(bitmap_instruction&   instr,
                                        std::filesystem::path out_dir,
                                        std::string const&    pdf_path,
                                        int                   page_number,
                                        int                   bitmap_index)
  {
    std::filesystem::create_directories(out_dir);

    if(not instr.has_data())
      {
        LOG_S(WARNING) << "bitmap_exporter: skipping bitmap without pixel data"
                       << " key=" << instr.get_key()
                       << " page=" << (page_number + 1);
        return false;
      }

    auto const& shape = instr.get_shape();
    const int height   = shape[0];
    const int width    = shape[1];
    const int channels = shape[2];

    if(height <= 0 or width <= 0 or channels <= 0)
      {
        LOG_S(WARNING) << "bitmap_exporter: skipping bitmap with invalid shape"
                       << " key=" << instr.get_key()
                       << " shape=" << height << "x" << width << "x" << channels;
        return false;
      }

    auto const& data = instr.get_data();
    auto const& alpha_data = instr.get_alpha_data();
    std::string safe_pdf_stem = sanitize_path_component(
      std::filesystem::path(pdf_path).stem().string());
    std::string safe_key = sanitize_path_component(instr.get_key());
    std::string stem =
      safe_pdf_stem + "_p" + std::to_string(page_number + 1) +
      "_xobj_" + safe_key +
      "_bitmap_" + std::to_string(bitmap_index);

    if(instr.get_pixel_format() == PIXEL_FORMAT_GRAY)
      {
        std::filesystem::path out_path = out_dir / (stem + ".png");
        ccitt::save_debug_png(*data, width, height, out_path.string());
        LOG_S(INFO) << "bitmap_exporter: wrote grayscale bitmap to "
                    << out_path.string();
        return true;
      }

    if(instr.get_pixel_format() == PIXEL_FORMAT_RGB
       or instr.get_pixel_format() == PIXEL_FORMAT_CMYK)
      {
        std::vector<uint8_t> composited;
        auto const* export_data = data.get();
        if(instr.get_pixel_format() == PIXEL_FORMAT_RGB
           and instr.has_alpha_data()
           and alpha_data->size() >= static_cast<size_t>(width) * height)
          {
            composited.resize(static_cast<size_t>(width) * height * 3);
            for(int i = 0; i < width * height; ++i)
              {
                const uint8_t alpha = alpha_data->at(i);
                for(int c = 0; c < 3; ++c)
                  {
                    const uint8_t src = data->at(static_cast<size_t>(i) * 3 + c);
                    composited[static_cast<size_t>(i) * 3 + c] =
                      static_cast<uint8_t>((static_cast<unsigned int>(src) * alpha
                                            + 255u * (255u - alpha)) / 255u);
                  }
              }
            export_data = &composited;
          }

        jpeg::jpeg_parameters params;
        params.width              = width;
        params.height             = height;
        params.bits_per_component = 8;
        params.color_space =
          (instr.get_pixel_format() == PIXEL_FORMAT_RGB)
          ? jpeg::ColorSpace::RGB
          : jpeg::ColorSpace::CMYK;

        std::filesystem::path out_path = out_dir / (stem + ".jpg");
        bool ok = jpeg::write_jpeg_from_raw_pixels(
          reinterpret_cast<unsigned char const*>(export_data->data()),
          export_data->size(),
          params,
          out_path);

        if(ok)
          {
            LOG_S(INFO) << "bitmap_exporter: wrote color bitmap to "
                        << out_path.string();
          }
        else
          {
            LOG_S(WARNING) << "bitmap_exporter: failed to write JPEG for "
                           << out_path.string();
          }
        return ok;
      }

    std::filesystem::path out_path = out_dir / (stem + ".bin");
    std::ofstream out(out_path, std::ios::binary);
    if(not out)
      {
        LOG_S(WARNING) << "bitmap_exporter: cannot open fallback output file "
                       << out_path.string();
        return false;
      }

    out.write(reinterpret_cast<char const*>(data->data()),
              static_cast<std::streamsize>(data->size()));
    LOG_S(INFO) << "bitmap_exporter: wrote raw bitmap buffer to "
                << out_path.string();
    return true;
  }

  class bitmap_exporter_visitor
  {
  public:

    bitmap_exporter_visitor(std::filesystem::path out_dir,
                            std::string const&    pdf_path,
                            int                   page_number):
      out_dir(std::move(out_dir)),
      pdf_path(pdf_path),
      page_number(page_number)
    {
      std::filesystem::create_directories(this->out_dir);
    }

    void set_size(size_instruction&)             {}
    void render_text(text_instruction&)          {}
    void render_widget(text_widget_instruction&) {}
    void render_shape(shape_instruction&)        {}

    void render_bitmap(bitmap_instruction& instr)
    {
      ++bitmap_index;
      export_bitmap_instruction(instr, out_dir, pdf_path, page_number, bitmap_index);
    }

  private:

    std::filesystem::path out_dir;
    std::string           pdf_path;
    int                   page_number = 0;
    int                   bitmap_index = 0;
  };

}
}

#endif
