//-*-C++-*-

#ifndef PDF_EXPORTED_IMAGE_H
#define PDF_EXPORTED_IMAGE_H

#include <array>
#include <string>
#include <vector>

namespace pdflib
{
  // One image of a page, detached from the page decoder that produced it:
  // where it sits on the page, what its samples are, and the bytes to save.
  class pdf_exported_image
  {
  public:

    typedef std::array<double, 4> box_type;

    pdf_exported_image();
    pdf_exported_image(const std::string& name_,
                       const box_type& bbox_,
                       int width_,
                       int height_,
                       const std::string& format_,
                       const std::string& mode_,
                       std::vector<unsigned char>&& data_);

    // The /XObject resource name the page draws the image under; empty for
    // an inline image.
    const std::string& get_name() const;

    // [x0, y0, x1, y1] of the image on the page, in the page's coordinates.
    const box_type& get_bbox() const;

    // Size of the sample grid, in pixels.
    int get_width() const;
    int get_height() const;

    // "jpeg", "jp2", "jbig2" or "raw": what `data` is encoded as.
    const std::string& get_format() const;

    // PIL-style mode of the samples: "1", "L", "RGB" or "CMYK".
    const std::string& get_mode() const;

    // Empty when the image data could not be produced.
    const std::vector<unsigned char>& get_data() const;

  private:

    std::string name;
    box_type bbox;

    int width;
    int height;

    std::string format;
    std::string mode;

    std::vector<unsigned char> data;
  };

  inline pdf_exported_image::pdf_exported_image():
    name(""),
    bbox({0.0, 0.0, 0.0, 0.0}),
    width(0),
    height(0),
    format("raw"),
    mode("RGB"),
    data({})
  {}

  inline pdf_exported_image::pdf_exported_image(const std::string& name_,
                                                const box_type& bbox_,
                                                int width_,
                                                int height_,
                                                const std::string& format_,
                                                const std::string& mode_,
                                                std::vector<unsigned char>&& data_):
    name(name_),
    bbox(bbox_),
    width(width_),
    height(height_),
    format(format_),
    mode(mode_),
    data(std::move(data_))
  {}

  inline const std::string& pdf_exported_image::get_name() const { return name; }

  inline const pdf_exported_image::box_type& pdf_exported_image::get_bbox() const { return bbox; }

  inline int pdf_exported_image::get_width() const { return width; }

  inline int pdf_exported_image::get_height() const { return height; }

  inline const std::string& pdf_exported_image::get_format() const { return format; }

  inline const std::string& pdf_exported_image::get_mode() const { return mode; }

  inline const std::vector<unsigned char>& pdf_exported_image::get_data() const { return data; }

}

#endif
