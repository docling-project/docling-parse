//-*-C++-*-

#ifndef PAGE_ITEM_IMAGE_H
#define PAGE_ITEM_IMAGE_H

// JPEG correction helpers
#include <parse/utils/jpeg/jpeg_utils.h>

namespace pdflib
{

  template<>
  class page_item<PAGE_IMAGE>
  {
  public:

    page_item();
    ~page_item();

    nlohmann::json get();

    void rotate(int angle, std::pair<double, double> delta);

    // Determine file extension from filters (e.g. ".jpg", ".jp2", ".jb2", ".bin")
    std::string get_image_extension() const;

    // Save raw stream data to a file (convenience wrapper)
    void save_to_file(std::filesystem::path const& path) const;

    // Save decoded stream data to a file
    void save_decoded_to_file(std::filesystem::path const& path) const;

    // Get image format hint: "jpeg", "jp2", "jbig2", or "raw"
    std::string get_image_format() const;

    // Get PIL-compatible mode string: "L", "RGB", "CMYK", or "1"
    std::string get_pil_mode() const;

    // Get image bytes suitable for constructing a PIL Image.
    // For JPEG: returns corrected JPEG bytes (applying /Decode if needed).
    // For JP2: returns raw JP2 stream bytes.
    // For raw/JBIG2: returns decoded pixel bytes.
    std::vector<unsigned char> get_image_as_bytes() const;

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

    // PDF image semantics copied from XObject
    bool decode_present = false;
    std::vector<double> decode_array; // 2*ncomp when present
    bool image_mask = false;

    // graphics state properties
    bool               has_graphics_state = false;
    std::array<int, 3> rgb_stroking_ops = {0, 0, 0};
    std::array<int, 3> rgb_filling_ops  = {0, 0, 0};
  };

  page_item<PAGE_IMAGE>::page_item():
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

  page_item<PAGE_IMAGE>::~page_item()
  {}

  std::vector<std::string> page_item<PAGE_IMAGE>::header = {
    "x0",
    "y0",
    "x1",
    "y1",
    "xobject_key",
    "image_width",
    "image_height",
    "bits_per_component",
    "color_space",
    "intent",
    "has-graphics-state",
    "rgb-stroking",
    "rgb-filling"
  };

  nlohmann::json page_item<PAGE_IMAGE>::get()
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
      image.push_back(has_graphics_state);
      image.push_back(rgb_stroking_ops);
      image.push_back(rgb_filling_ops);
    }
    assert(image.size()==header.size());

    return image;
  }

  void page_item<PAGE_IMAGE>::rotate(int angle, std::pair<double, double> delta)
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

  std::string page_item<PAGE_IMAGE>::get_image_extension() const
  {
    for(auto const& f : filters)
      {
        if(f == "/DCTDecode")  return ".jpg";
        if(f == "/JPXDecode")  return ".jp2";
        if(f == "/JBIG2Decode") return ".jb2";
      }
    return ".bin";
  }

  void page_item<PAGE_IMAGE>::save_to_file(std::filesystem::path const& path) const
  {
    if(not raw_stream_data or raw_stream_data->getSize() == 0)
      {
        LOG_S(WARNING) << "no raw stream data to save";
        return;
      }

    auto ext = path.extension().string();
    for(auto& c : ext) c = static_cast<char>(::tolower(c));

    auto is_jpeg_ext = (ext == ".jpg" || ext == ".jpeg");

    auto filters_have_dct = false;
    for(auto const& f : filters) { if(f == "/DCTDecode") filters_have_dct = true; }

    auto color_space_to_enum = [](std::string const& cs){
      return jpeg::to_color_space(cs);
    };

    auto is_safe_passthrough = [&]() -> bool {
      if(!is_jpeg_ext) return false;
      if(!filters_have_dct) return false;
      if(bits_per_component != 8) return false;
      if(!(color_space == "/DeviceRGB" || color_space == "/DeviceGray" || color_space == "/DeviceCMYK")) return false;
      if(image_mask) return false;
      if(decode_present && !decode_array.empty())
      {
        int ncomp = (color_space == "/DeviceGray") ? 1
                  : (color_space == "/DeviceCMYK") ? 4 : 3;
        if(static_cast<int>(decode_array.size()) < 2*ncomp) return false;
        for(int c=0;c<ncomp;++c)
        {
          double dmin = decode_array[2*c+0];
          double dmax = decode_array[2*c+1];
          if(!(std::abs(dmin - 0.0) < 1e-12 && std::abs(dmax - 1.0) < 1e-12))
            return false;
        }
      }
      return true;
    }();

    if(is_jpeg_ext && (!is_safe_passthrough))
      {
        jpeg::jpeg_parameters params;
        params.width = image_width;
        params.height = image_height;
        params.bits_per_component = bits_per_component;
        params.color_space = color_space_to_enum(color_space);
        params.decode = decode_array;
        params.has_decode = decode_present && !decode_array.empty();
        params.image_mask = image_mask;

        bool ok = jpeg::write_corrected_jpeg_from_memory(
            reinterpret_cast<unsigned char const*>(raw_stream_data->getBuffer()),
            static_cast<std::size_t>(raw_stream_data->getSize()),
            params, path);
        if(ok)
          {
            LOG_S(INFO) << "wrote corrected JPEG to " << path.string();
            return;
          }
        LOG_S(WARNING) << "JPEG correction failed, falling back to raw copy: " << path.string();
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

  void page_item<PAGE_IMAGE>::save_decoded_to_file(std::filesystem::path const& path) const
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

  std::string page_item<PAGE_IMAGE>::get_image_format() const
  {
    for(auto const& f : filters)
      {
        if(f == "/DCTDecode")  { return "jpeg"; }
        if(f == "/JPXDecode")  { return "jp2"; }
        if(f == "/JBIG2Decode") { return "jbig2"; }
      }
    return "raw";
  }

  std::string page_item<PAGE_IMAGE>::get_pil_mode() const
  {
    if(image_mask) { return "1"; }
    if(color_space == "/DeviceGray") { return "L"; }
    if(color_space == "/DeviceRGB")  { return "RGB"; }
    if(color_space == "/DeviceCMYK") { return "CMYK"; }

    LOG_S(WARNING) << "unknown color_space '" << color_space
                   << "' for xobject_key=" << xobject_key
                   << ", falling back to RGB";
    return "RGB";
  }

  std::vector<unsigned char> page_item<PAGE_IMAGE>::get_image_as_bytes() const
  {
    std::string fmt = get_image_format();

    if(fmt == "jpeg")
      {
        if(not raw_stream_data or raw_stream_data->getSize() == 0)
          {
            LOG_S(WARNING) << "no raw stream data for JPEG image"
                           << " xobject_key=" << xobject_key;
            return {};
          }

        // Check if safe passthrough (same logic as save_to_file)
        bool needs_correction = false;

        if(bits_per_component != 8)
          {
            needs_correction = true;
          }
        else if(not (color_space == "/DeviceRGB" or
                     color_space == "/DeviceGray" or
                     color_space == "/DeviceCMYK"))
          {
            needs_correction = true;
          }
        else if(image_mask)
          {
            needs_correction = true;
          }
        else if(decode_present and not decode_array.empty())
          {
            int ncomp = (color_space == "/DeviceGray") ? 1
                      : (color_space == "/DeviceCMYK") ? 4 : 3;

            if(static_cast<int>(decode_array.size()) >= 2 * ncomp)
              {
                for(int c = 0; c < ncomp; ++c)
                  {
                    double dmin = decode_array[2 * c + 0];
                    double dmax = decode_array[2 * c + 1];
                    if(not (std::abs(dmin - 0.0) < 1e-12 and
                            std::abs(dmax - 1.0) < 1e-12))
                      {
                        needs_correction = true;
                        break;
                      }
                  }
              }
          }

        if(needs_correction)
          {
            jpeg::jpeg_parameters params;
            params.width              = image_width;
            params.height             = image_height;
            params.bits_per_component = bits_per_component;
            params.color_space        = jpeg::to_color_space(color_space);
            params.decode             = decode_array;
            params.has_decode         = decode_present and not decode_array.empty();
            params.image_mask         = image_mask;

            auto result = jpeg::write_corrected_jpeg_to_memory(
                reinterpret_cast<unsigned char const*>(
                    raw_stream_data->getBuffer()),
                static_cast<std::size_t>(raw_stream_data->getSize()),
                params);

            if(not result.empty())
              {
                return result;
              }

            LOG_S(WARNING) << "JPEG correction failed for xobject_key="
                           << xobject_key
                           << ", falling back to raw passthrough";
          }

        // Safe passthrough: return raw JPEG bytes
        auto* buf = reinterpret_cast<unsigned char const*>(
            raw_stream_data->getBuffer());
        return std::vector<unsigned char>(
            buf, buf + raw_stream_data->getSize());
      }

    if(fmt == "jp2")
      {
        if(not raw_stream_data or raw_stream_data->getSize() == 0)
          {
            LOG_S(WARNING) << "no raw stream data for JP2 image"
                           << " xobject_key=" << xobject_key;
            return {};
          }
        auto* buf = reinterpret_cast<unsigned char const*>(
            raw_stream_data->getBuffer());
        return std::vector<unsigned char>(
            buf, buf + raw_stream_data->getSize());
      }

    // Raw pixels (JBIG2, uncompressed, etc): use decoded_stream_data
    if(decoded_stream_data and decoded_stream_data->getSize() > 0)
      {
        auto* buf = reinterpret_cast<unsigned char const*>(
            decoded_stream_data->getBuffer());
        return std::vector<unsigned char>(
            buf, buf + decoded_stream_data->getSize());
      }

    // Fallback: try raw_stream_data
    if(raw_stream_data and raw_stream_data->getSize() > 0)
      {
        LOG_S(WARNING) << "no decoded stream data for " << fmt << " image"
                       << " xobject_key=" << xobject_key
                       << ", falling back to raw stream data";
        auto* buf = reinterpret_cast<unsigned char const*>(
            raw_stream_data->getBuffer());
        return std::vector<unsigned char>(
            buf, buf + raw_stream_data->getSize());
      }

    LOG_S(WARNING) << "no image data available for xobject_key="
                   << xobject_key
                   << " format=" << fmt;
    return {};
  }

}

#endif
