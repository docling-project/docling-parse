//-*-C++-*-

#ifndef PDF_PAGE_IMAGE_RESOURCE_H
#define PDF_PAGE_IMAGE_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_IMAGE>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();

    void rotate(int angle, std::pair<double, double> delta);

    // Determine file extension from filters (e.g. ".jpg", ".jp2", ".jb2", ".bin")
    std::string get_image_extension() const;

    // Save raw stream data to a file (convenience wrapper)
    void save_to_file(std::filesystem::path const& path) const;

    // Save decoded stream data to a file
    void save_decoded_to_file(std::filesystem::path const& path) const;

  public:

    static std::vector<std::string> header;

    // Bounding box (in page coordinates)
    double x0;
    double y0;
    double x1;
    double y1;

    // Image properties (from the XObject)
    std::string              xobject_key;
    int                      image_width;
    int                      image_height;
    int                      bits_per_component;
    std::string              color_space;
    std::string              intent;
    std::vector<std::string> filters;
    std::shared_ptr<Buffer>  raw_stream_data;
    std::shared_ptr<Buffer>  decoded_stream_data;
  };

  pdf_resource<PAGE_IMAGE>::pdf_resource():
    x0(0), y0(0), x1(0), y1(0),
    xobject_key(),
    image_width(0),
    image_height(0),
    bits_per_component(0),
    color_space(),
    intent(),
    filters(),
    raw_stream_data(nullptr),
    decoded_stream_data(nullptr)
  {}

  pdf_resource<PAGE_IMAGE>::~pdf_resource()
  {}

  std::vector<std::string> pdf_resource<PAGE_IMAGE>::header = {
    "x0",
    "y0",
    "x1",
    "y1",
    "xobject_key",
    "image_width",
    "image_height",
    "bits_per_component",
    "color_space",
    "intent"
  };

  nlohmann::json pdf_resource<PAGE_IMAGE>::get()
  {
    nlohmann::json image;

    {
      image.push_back(x0);
      image.push_back(y0);
      image.push_back(x1);
      image.push_back(y1);
      image.push_back(xobject_key);
      image.push_back(image_width);
      image.push_back(image_height);
      image.push_back(bits_per_component);
      image.push_back(color_space);
      image.push_back(intent);
    }
    assert(image.size()==header.size());

    return image;
  }

  void pdf_resource<PAGE_IMAGE>::rotate(int angle, std::pair<double, double> delta)
  {
    utils::values::rotate_inplace(angle, x0, y0);
    utils::values::rotate_inplace(angle, x1, y1);

    utils::values::translate_inplace(delta, x0, y0);
    utils::values::translate_inplace(delta, x1, y1);

    double y_min = std::min(y0, y1);
    double y_max = std::max(y0, y1);

    y0 = y_min;
    y1 = y_max;
  }

  std::string pdf_resource<PAGE_IMAGE>::get_image_extension() const
  {
    for(auto const& f : filters)
      {
        if(f == "/DCTDecode")  return ".jpg";
        if(f == "/JPXDecode")  return ".jp2";
        if(f == "/JBIG2Decode") return ".jb2";
      }
    return ".bin";
  }

  void pdf_resource<PAGE_IMAGE>::save_to_file(std::filesystem::path const& path) const
  {
    if(not raw_stream_data or raw_stream_data->getSize() == 0)
      {
        LOG_S(WARNING) << "no raw stream data to save";
        return;
      }

    std::ofstream out(path, std::ios::binary);
    if(not out)
      {
        LOG_S(ERROR) << "unable to open output file: " << path.string();
        throw std::runtime_error("unable to open output file: " + path.string());
      }

    out.write(reinterpret_cast<char const*>(raw_stream_data->getBuffer()),
              static_cast<std::streamsize>(raw_stream_data->getSize()));

    LOG_S(INFO) << "saved " << raw_stream_data->getSize()
                << " bytes to " << path.string();
  }

  void pdf_resource<PAGE_IMAGE>::save_decoded_to_file(std::filesystem::path const& path) const
  {
    if(not decoded_stream_data or decoded_stream_data->getSize() == 0)
      {
        LOG_S(WARNING) << "no decoded stream data to save";
        return;
      }

    std::ofstream out(path, std::ios::binary);
    if(not out)
      {
        LOG_S(ERROR) << "unable to open output file: " << path.string();
        throw std::runtime_error("unable to open output file: " + path.string());
      }

    out.write(reinterpret_cast<char const*>(decoded_stream_data->getBuffer()),
              static_cast<std::streamsize>(decoded_stream_data->getSize()));

    LOG_S(INFO) << "saved decoded " << decoded_stream_data->getSize()
                << " bytes to " << path.string();
  }

}

#endif
