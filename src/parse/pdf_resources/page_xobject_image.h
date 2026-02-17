//-*-C++-*-

#ifndef PDF_PAGE_XOBJECT_IMAGE_RESOURCE_H
#define PDF_PAGE_XOBJECT_IMAGE_RESOURCE_H

#include <parse/utils/jpeg/jpeg_utils.h>
#include <parse/qpdf/qpdf_compat.h>

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_XOBJECT_IMAGE>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();

    std::string              get_key() const;
    xobject_subtype_name     get_subtype() const;

    void set(std::string      xobject_key_,
             QPDFObjectHandle qpdf_xobject_);

    // Image property getters
    int                      get_image_width() const;
    int                      get_image_height() const;
    int                      get_bits_per_component() const;
    std::string              get_color_space() const;
    std::string              get_intent() const;
    std::vector<std::string> get_filters() const;

    // Optional PDF semantics for images
    bool                     has_decode_array() const;
    std::vector<double>      get_decode_array() const;
    bool                     is_image_mask() const;

    bool                     has_raw_stream_data() const;
    std::shared_ptr<Buffer>  get_raw_stream_data() const;

    bool                     has_decoded_stream_data() const;
    std::shared_ptr<Buffer>  get_decoded_stream_data() const;

    // Determine file extension from filters (e.g. ".jpg", ".jp2", ".jb2", ".bin")
    std::string pick_extension() const;

    // Save raw stream data to a file
    void save_to_file(std::filesystem::path const& path) const;

    // Load a buffer from a file on disk
    static std::shared_ptr<Buffer> load_from_file(std::filesystem::path const& path);

  private:

    void parse();

    void init_image_properties();

    void init_filters();

    void init_stream_data();

  private:

    QPDFObjectHandle qpdf_xobject;

    QPDFObjectHandle qpdf_xobject_dict;
    nlohmann::json   json_xobject_dict;

    std::string xobject_key;

    // Image-specific properties
    int              image_width;
    int              image_height;
    int              bits_per_component;
    std::string      color_space;
    std::string      intent;
    std::vector<std::string> image_filters;

    // Stream data
    std::shared_ptr<Buffer> raw_stream_data;
    std::shared_ptr<Buffer> decoded_stream_data;

    // PDF image semantics
    std::vector<double> decode_array; // length 2*ncomp when present
    bool decode_present = false;
    bool image_mask = false;
  };

  pdf_resource<PAGE_XOBJECT_IMAGE>::pdf_resource():
    image_width(0),
    image_height(0),
    bits_per_component(0),
    color_space(),
    intent(),
    image_filters(),
    raw_stream_data(nullptr),
    decoded_stream_data(nullptr)
  {}

  pdf_resource<PAGE_XOBJECT_IMAGE>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_XOBJECT_IMAGE>::get()
  {
    return to_json(qpdf_xobject);
  }

  std::string pdf_resource<PAGE_XOBJECT_IMAGE>::get_key() const
  {
    return xobject_key;
  }

  xobject_subtype_name pdf_resource<PAGE_XOBJECT_IMAGE>::get_subtype() const
  {
    return XOBJECT_IMAGE;
  }

  void pdf_resource<PAGE_XOBJECT_IMAGE>::set(std::string      xobject_key_,
                                              QPDFObjectHandle qpdf_xobject_)
  {
    LOG_S(INFO) << __FUNCTION__ << ": " << xobject_key_;

    xobject_key  = xobject_key_;
    qpdf_xobject = qpdf_xobject_;

    parse();
  }

  void pdf_resource<PAGE_XOBJECT_IMAGE>::parse()
  {
    LOG_S(INFO) << __FUNCTION__;

    {
      qpdf_xobject_dict = qpdf_xobject.getDict();
      json_xobject_dict = to_json(qpdf_xobject_dict);
    }

    init_image_properties();
    init_filters();
    init_stream_data();
  }

  void pdf_resource<PAGE_XOBJECT_IMAGE>::init_image_properties()
  {
    LOG_S(INFO) << __FUNCTION__ << ": " << json_xobject_dict.dump(2);

    // /Width
    if(json_xobject_dict.count("/Width") && json_xobject_dict["/Width"].is_number())
      {
        image_width = json_xobject_dict["/Width"].get<int>();
      }
    else
      {
        LOG_S(WARNING) << "no `/Width` found";
      }

    // /Height
    if(json_xobject_dict.count("/Height") && json_xobject_dict["/Height"].is_number())
      {
        image_height = json_xobject_dict["/Height"].get<int>();
      }
    else
      {
        LOG_S(WARNING) << "no `/Height` found";
      }

    // /BitsPerComponent
    if(json_xobject_dict.count("/BitsPerComponent") && json_xobject_dict["/BitsPerComponent"].is_number())
      {
        bits_per_component = json_xobject_dict["/BitsPerComponent"].get<int>();
      }
    else
      {
        LOG_S(WARNING) << "no `/BitsPerComponent` found";
      }

    // /ColorSpace -- may be a name ("/DeviceRGB") or an array; store as string
    if(json_xobject_dict.count("/ColorSpace"))
      {
        auto& cs = json_xobject_dict["/ColorSpace"];
        if(cs.is_string())
          {
            color_space = cs.get<std::string>();
          }
        else
          {
            color_space = cs.dump();
          }
      }
    else
      {
        LOG_S(WARNING) << "no `/ColorSpace` found";
      }

    // /Intent
    if(json_xobject_dict.count("/Intent") && json_xobject_dict["/Intent"].is_string())
      {
        intent = json_xobject_dict["/Intent"].get<std::string>();
      }
    else
      {
        LOG_S(WARNING) << "no `/Intent` found";
      }

    // /ImageMask
    if(json_xobject_dict.count("/ImageMask") && json_xobject_dict["/ImageMask"].is_boolean())
      {
        image_mask = json_xobject_dict["/ImageMask"].get<bool>();
      }
    else
      {
        LOG_S(WARNING) << "no `/ImageMask` found";
      }

    // /Decode (array of pairs per component)
    decode_array.clear();
    decode_present = false;
    if(json_xobject_dict.count("/Decode"))
      {
        auto& dec = json_xobject_dict["/Decode"];
        if(dec.is_array())
          {
            for(auto const& v : dec)
              {
                if(v.is_number())
                  decode_array.push_back(v.get<double>());
              }
            decode_present = !decode_array.empty();
          }
      }
    else
      {
        LOG_S(WARNING) << "no `/Decode` found: falling back on default";
        decode_array = {
          1, 0, 1, 0,
          1, 0, 1, 0
        };
        decode_present = !decode_array.empty();
      }

    LOG_S(INFO) << "image properties: "
                << image_width << "x" << image_height
                << " bpc=" << bits_per_component
                << " cs=" << color_space
                << " intent=" << intent
                << " mask=" << (image_mask?"true":"false")
                << " decode_len=" << decode_array.size();
  }

  void pdf_resource<PAGE_XOBJECT_IMAGE>::init_filters()
  {
    LOG_S(INFO) << __FUNCTION__;

    image_filters.clear();

    if(not json_xobject_dict.count("/Filter"))
      {
        return;
      }

    auto& f = json_xobject_dict["/Filter"];
    if(f.is_string())
      {
        image_filters.push_back(f.get<std::string>());
      }
    else if(f.is_array())
      {
        for(auto const& item : f)
          {
            if(item.is_string())
              image_filters.push_back(item.get<std::string>());
          }
      }

    for(auto const& flt : image_filters)
      {
        LOG_S(INFO) << "filter: " << flt;
      }
  }

  void pdf_resource<PAGE_XOBJECT_IMAGE>::init_stream_data()
  {
    LOG_S(INFO) << __FUNCTION__;

    if(not qpdf_xobject.isStream())
      {
        LOG_S(WARNING) << "xobject is not a stream, cannot extract raw data";
        return;
      }

    try
      {
        raw_stream_data = to_shared_ptr(qpdf_xobject.getRawStreamData());
        LOG_S(INFO) << "raw stream size: " << raw_stream_data->getSize() << " bytes";
      }
    catch(std::exception const& e)
      {
        LOG_S(ERROR) << "failed to get raw stream data: " << e.what();
        raw_stream_data = nullptr;
      }

    try
      {
        decoded_stream_data = to_shared_ptr(qpdf_xobject.getStreamData());
        LOG_S(INFO) << "decoded stream size: " << decoded_stream_data->getSize() << " bytes";
      }
    catch(std::exception const& e)
      {
        LOG_S(WARNING) << "failed to get decoded stream data: " << e.what();
        decoded_stream_data = nullptr;
      }
  }

  // --- Getters ---

  int pdf_resource<PAGE_XOBJECT_IMAGE>::get_image_width() const
  {
    return image_width;
  }

  int pdf_resource<PAGE_XOBJECT_IMAGE>::get_image_height() const
  {
    return image_height;
  }

  int pdf_resource<PAGE_XOBJECT_IMAGE>::get_bits_per_component() const
  {
    return bits_per_component;
  }

  std::string pdf_resource<PAGE_XOBJECT_IMAGE>::get_color_space() const
  {
    return color_space;
  }

  std::string pdf_resource<PAGE_XOBJECT_IMAGE>::get_intent() const
  {
    return intent;
  }

  std::vector<std::string> pdf_resource<PAGE_XOBJECT_IMAGE>::get_filters() const
  {
    return image_filters;
  }

  bool pdf_resource<PAGE_XOBJECT_IMAGE>::has_decode_array() const
  {
    return decode_present && !decode_array.empty();
  }

  std::vector<double> pdf_resource<PAGE_XOBJECT_IMAGE>::get_decode_array() const
  {
    return decode_array;
  }

  bool pdf_resource<PAGE_XOBJECT_IMAGE>::is_image_mask() const
  {
    return image_mask;
  }

  bool pdf_resource<PAGE_XOBJECT_IMAGE>::has_raw_stream_data() const
  {
    return (raw_stream_data != nullptr && raw_stream_data->getSize() > 0);
  }

  std::shared_ptr<Buffer> pdf_resource<PAGE_XOBJECT_IMAGE>::get_raw_stream_data() const
  {
    return raw_stream_data;
  }

  bool pdf_resource<PAGE_XOBJECT_IMAGE>::has_decoded_stream_data() const
  {
    return (decoded_stream_data != nullptr && decoded_stream_data->getSize() > 0);
  }

  std::shared_ptr<Buffer> pdf_resource<PAGE_XOBJECT_IMAGE>::get_decoded_stream_data() const
  {
    return decoded_stream_data;
  }

  // --- File I/O ---

  std::string pdf_resource<PAGE_XOBJECT_IMAGE>::pick_extension() const
  {
    for(auto const& f : image_filters)
      {
        if(f == "/DCTDecode")  return ".jpg";
        if(f == "/JPXDecode")  return ".jp2";
        if(f == "/JBIG2Decode") return ".jb2";
      }
    return ".bin";
  }

  void pdf_resource<PAGE_XOBJECT_IMAGE>::save_to_file(std::filesystem::path const& path) const
  {
    if(not has_raw_stream_data())
      {
        LOG_S(WARNING) << "no raw stream data to save";
        return;
      }

    auto ext = path.extension().string();
    for(auto& c : ext) c = static_cast<char>(::tolower(c));
    bool is_jpeg_ext = (ext == ".jpg" || ext == ".jpeg");

    bool filters_have_dct = false;
    for(auto const& f : image_filters) { if(f == "/DCTDecode") filters_have_dct = true; }

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
        params.color_space = jpeg::to_color_space(color_space);
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

  std::shared_ptr<Buffer> pdf_resource<PAGE_XOBJECT_IMAGE>::load_from_file(
                                                                     std::filesystem::path const& path)
  {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if(not in)
      {
        LOG_S(ERROR) << "unable to open input file: " << path.string();
        throw std::runtime_error("unable to open input file: " + path.string());
      }

    auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    auto buffer = std::make_shared<Buffer>(size);
    in.read(reinterpret_cast<char*>(buffer->getBuffer()),
            static_cast<std::streamsize>(size));

    LOG_S(INFO) << "loaded " << size << " bytes from " << path.string();

    return buffer;
  }

}

#endif
