//-*-C++-*-

#ifndef PDF_PAGE_XOBJECT_IMAGE_RESOURCE_H
#define PDF_PAGE_XOBJECT_IMAGE_RESOURCE_H

#include <parse/utils/ccitt/ccitt_utils.h>

#include <cstdint>
#include <cstring>

#include <parse/utils/color/icc_utils.h>
#include <parse/utils/jpeg/jpeg_utils.h>
#include <parse/qpdf/qpdf_compat.h>
#include <parse/qpdf/stream_filters.h>

namespace pdflib
{

  namespace detail
  {
    inline int icc_signature_to_components(char const* sig)
    {
      if(std::memcmp(sig, "GRAY", 4) == 0) return 1;
      if(std::memcmp(sig, "RGB ", 4) == 0) return 3;
      if(std::memcmp(sig, "CMYK", 4) == 0) return 4;

      if(sig[1] == 'C' and sig[2] == 'L' and sig[3] == 'R')
        {
          if(sig[0] >= '2' and sig[0] <= '9') return sig[0] - '0';
          if(sig[0] >= 'A' and sig[0] <= 'F') return 10 + (sig[0] - 'A');
        }

      return 0;
    }

    inline int infer_icc_components_from_profile(QPDFObjectHandle icc_stream,
                                                 std::string const& context)
    {
      if(not icc_stream.isStream())
        {
          LOG_S(WARNING) << context << ": ICC object is not a stream";
          return 0;
        }

      try
        {
          auto profile = to_shared_ptr(icc_stream.getStreamData());
          if(not profile or profile->getSize() < 20)
            {
              LOG_S(WARNING) << context << ": ICC profile too small to inspect";
              return 0;
            }

          auto const* bytes = reinterpret_cast<std::uint8_t const*>(profile->getBuffer());
          int const n = icc_signature_to_components(reinterpret_cast<char const*>(bytes + 16));

          if(n > 0)
            {
              LOG_S(INFO) << context << ": inferred ICC components from profile header: N=" << n;
            }
          else
            {
              LOG_S(WARNING) << context << ": unsupported ICC data color space signature";
            }
          return n;
        }
      catch(std::exception const& e)
        {
          LOG_S(WARNING) << context << ": failed to inspect ICC profile stream: " << e.what();
          return 0;
        }
    }

    inline int cmyk_process_component_index(std::string const& name)
    {
      if(name == "/Cyan")    return 0;
      if(name == "/Magenta") return 1;
      if(name == "/Yellow")  return 2;
      if(name == "/Black")   return 3;
      return -1;
    }

    inline bool device_n_names_are_process_cmyk_subset(
      std::vector<std::string> const& names)
    {
      if(names.empty())
        {
          return false;
        }

      for(auto const& name : names)
        {
          if(cmyk_process_component_index(name) < 0)
            {
              return false;
            }
        }

      return true;
    }

    inline std::shared_ptr<std::vector<uint8_t>> expand_device_n_palette_to_cmyk(
      std::shared_ptr<std::vector<uint8_t>> const& palette,
      std::vector<std::string> const&              names)
    {
      if(not palette or names.empty())
        {
          return nullptr;
        }

      const std::size_t src_components = names.size();
      if(src_components == 0 or (palette->size() % src_components) != 0)
        {
          return nullptr;
        }

      const std::size_t entry_count = palette->size() / src_components;
      auto expanded = std::make_shared<std::vector<uint8_t>>();
      expanded->assign(entry_count * 4u, 0u);

      for(std::size_t entry = 0; entry < entry_count; ++entry)
        {
          const std::size_t src_offset = entry * src_components;
          const std::size_t dst_offset = entry * 4u;
          for(std::size_t i = 0; i < src_components; ++i)
            {
              const int dst_component = cmyk_process_component_index(names[i]);
              if(dst_component >= 0)
                {
                  (*expanded)[dst_offset + static_cast<std::size_t>(dst_component)] =
                    (*palette)[src_offset + i];
                }
            }
        }

      return expanded;
    }
  }

  template<>
  class pdf_resource<PAGE_XOBJECT_IMAGE>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get() const;

    std::string              get_key() const;
    xobject_subtype_name     get_subtype() const;

    // extract_pixels=false records everything the XObject dictionary knows --
    // dimensions, colour space, filters, decode array -- but skips defiltering
    // the samples and decoding the soft mask. See decode_config.
    void set(std::string      xobject_key_,
             QPDFObjectHandle qpdf_xobject_,
             bool             extract_pixels = true);

    // Image property getters
    int                      get_image_width() const;
    int                      get_image_height() const;
    int                      get_bits_per_component() const;
    std::string              get_color_space() const;
    int                      get_icc_components() const;
    int                      get_device_n_components() const;
    std::vector<std::string> get_device_n_names() const;
    std::shared_ptr<pdf_resource<PAGE_COLORSPACE>> get_tint_colorspace() const;
    int                      get_tint_components() const;
    int                      get_indexed_hival() const;
    std::string              get_indexed_base_cs() const;
    std::shared_ptr<std::vector<uint8_t>> get_indexed_palette() const;
    std::vector<std::string> get_indexed_base_device_n_names() const;
    bool                     get_indexed_base_device_n_single_black() const;
    std::string              get_intent() const;
    std::vector<std::string> get_filters() const;

    // Optional PDF semantics for images
    bool                     has_decode_array() const;
    std::vector<double>      get_decode_array() const;
    bool                     is_image_mask() const;

    // /CCITTFaxDecode parameters (from /DecodeParms)
    ccitt::decode_parameters get_ccitt_parameters() const;
    bool                     has_jbig2_globals_data() const;
    std::shared_ptr<Buffer>  get_jbig2_globals_data() const;

    bool                     has_raw_stream_data() const;
    std::shared_ptr<Buffer>  get_raw_stream_data() const;

    bool                     has_codec_stream_data() const;
    std::shared_ptr<Buffer>  get_codec_stream_data() const;

    bool                     has_decoded_stream_data() const;
    std::shared_ptr<Buffer>  get_decoded_stream_data() const;
    bool                     has_soft_mask_data() const;
    std::shared_ptr<std::vector<uint8_t>> get_soft_mask_data() const;
    int get_soft_mask_width() const { return soft_mask_width; }
    int get_soft_mask_height() const { return soft_mask_height; }

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
    void init_codec_stream_data();
    void init_soft_mask_data();

    // Resolves a /Separation or /DeviceN /ColorSpace array into a colour space
    // resource, keeping it only when its tint transform is usable.
    void resolve_tint_colorspace(QPDFObjectHandle qpdf_cs);

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
    int              icc_components = 0;  // number of color components from /ICCBased /N entry; 0 if not ICCBased
    int              device_n_components = 0; // number of components from /DeviceN names array; 0 if not DeviceN
    std::vector<std::string> device_n_names; // names from /DeviceN colorant array

    // /Separation and /DeviceN resolved through pdf_resource<PAGE_COLORSPACE>,
    // so the sample tints can be run through the tint transform. Only set when
    // that transform and its alternate space both decoded.
    std::shared_ptr<pdf_resource<PAGE_COLORSPACE>> tint_colorspace;
    int              tint_components = 0;
    int              indexed_hival  = -1; // hival from /Indexed color space; -1 if not Indexed
    std::string      indexed_base_cs;    // base color space name for /Indexed (e.g. "/DeviceRGB")
    std::shared_ptr<std::vector<uint8_t>> indexed_palette; // raw palette bytes: (hival+1)*ncomps bytes
    std::shared_ptr<pdf_resource<PAGE_COLORSPACE> > indexed_tint_space;
    std::shared_ptr<std::vector<uint8_t>> indexed_base_icc_profile;
    int              indexed_base_icc_components = 0;
    std::vector<std::string> indexed_base_device_n_names;
    bool             indexed_base_device_n_single_black = false;
    std::string      intent;
    std::vector<std::string> image_filters;

    // Stream data
    std::shared_ptr<Buffer> raw_stream_data;

    // The raw stream with the transport filters that precede the image codec
    // undone -- the bytes the codec was handed at encode time. Aliases
    // raw_stream_data when the codec is the first filter in the chain.
    std::shared_ptr<Buffer> codec_stream_data;

    std::shared_ptr<Buffer> decoded_stream_data;
    std::shared_ptr<std::vector<uint8_t>> soft_mask_data;

    // set() argument, kept so parse() can consult it
    bool extract_pixels_ = true;
    int soft_mask_width = 0;
    int soft_mask_height = 0;

    // PDF image semantics
    std::vector<double> decode_array; // length 2*ncomp when present
    bool decode_present = false;
    bool image_mask = false;

    // /CCITTFaxDecode parameters from /DecodeParms, carrying the spec defaults
    // (Table 11) until the stream says otherwise.
    ccitt::decode_parameters ccitt_params;
    std::shared_ptr<Buffer> jbig2_globals_data;
  };

  pdf_resource<PAGE_XOBJECT_IMAGE>::pdf_resource():
    image_width(0),
    image_height(0),
    bits_per_component(0),
    color_space(),
    intent(),
    image_filters(),
    raw_stream_data(nullptr),
    codec_stream_data(nullptr),
    decoded_stream_data(nullptr),
    soft_mask_data(nullptr),
    jbig2_globals_data(nullptr)
  {}

  pdf_resource<PAGE_XOBJECT_IMAGE>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_XOBJECT_IMAGE>::get() const
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
					     QPDFObjectHandle qpdf_xobject_,
					     bool             extract_pixels)
  {
    LOG_S(INFO) << __FUNCTION__ << ": " << xobject_key_;

    xobject_key  = xobject_key_;
    qpdf_xobject = qpdf_xobject_;
    extract_pixels_ = extract_pixels;

    parse();

    // only for debug purpose ...
    //{
    //static int image_cnt = 0;
    //image_cnt += 1;
    //std::string fpath = "image_"+std::to_string(image_cnt);
    //save_to_file(fpath.c_str());
    //}
  }

  void pdf_resource<PAGE_XOBJECT_IMAGE>::parse()
  {
    LOG_S(INFO) << __FUNCTION__;

    {
      qpdf_xobject_dict = qpdf_xobject.getDict();
      json_xobject_dict = to_json(qpdf_xobject_dict);
    }

    init_filters();
    init_image_properties();

    // Defiltering the samples and building the alpha plane is the whole cost
    // of an image XObject; everything above comes from the dictionary. A
    // caller that only wants to know where the bitmaps are skips both.
    if(extract_pixels_)
      {
        init_stream_data();
        init_soft_mask_data();
      }
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

    // /ColorSpace -- may be a name ("/DeviceRGB") or an array; store as string.
    // For /ICCBased arrays we additionally resolve the component count (/N) from
    // the referenced stream via the raw QPDF handle, because to_json() loses the
    // stream reference (rendering it as "8 0 R [stream]").
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

            auto qpdf_cs = qpdf_xobject_dict.getKey("/ColorSpace");

            // A device space may legally be written as a one-element array:
            // [/DeviceRGB] means exactly /DeviceRGB. Unwrap it to the bare name,
            // otherwise every later comparison sees the JSON dump
            // ("[\"/DeviceRGB\"]"), the space is reported as unsupported, and
            // the image is dropped in favour of a placeholder rectangle.
            if(qpdf_cs.isArray() and qpdf_cs.getArrayNItems() == 1)
              {
                auto only_obj = qpdf_cs.getArrayItem(0);
                if(only_obj.isName())
                  {
                    color_space = only_obj.getName();
                    LOG_S(INFO) << "unwrapped single-element /ColorSpace array to "
                                << color_space;
                  }
              }

            if(qpdf_cs.isArray() and qpdf_cs.getArrayNItems() >= 2)
              {
                auto name_obj = qpdf_cs.getArrayItem(0);
                if(name_obj.isName() and name_obj.getName() == "/ICCBased")
                  {
                    auto icc_stream = qpdf_cs.getArrayItem(1);
                    if(icc_stream.isStream())
                      {
                        auto icc_dict = icc_stream.getDict();
                        LOG_S(INFO) << "ICCBased stream dict: " << to_json(icc_dict).dump(2);
                        if(icc_dict.hasKey("/N") and icc_dict.getKey("/N").isInteger())
                          {
                            icc_components = icc_dict.getKey("/N").getIntValue();
                            LOG_S(INFO) << "ICCBased color space: N=" << icc_components;
                          }
                        else
                          {
                            LOG_S(WARNING) << "ICCBased stream missing /N entry";
                            icc_components = detail::infer_icc_components_from_profile(
                              icc_stream, "ICCBased");
                          }
                      }
                    else
                      {
                        LOG_S(WARNING) << "ICCBased: second array element is not a stream";
                      }
                  }
                else if(name_obj.isName() and name_obj.getName() == "/DeviceN")
                  {
                    device_n_names.clear();
                    auto names_obj = qpdf_cs.getArrayItem(1);
                    if(names_obj.isArray())
                      {
                        device_n_components = names_obj.getArrayNItems();
                        for(int i = 0; i < names_obj.getArrayNItems(); ++i)
                          {
                            auto name = names_obj.getArrayItem(i);
                            if(name.isName())
                              {
                                device_n_names.push_back(name.getName());
                              }
                          }
                        LOG_S(INFO) << "DeviceN color space: N=" << device_n_components;
                      }
                    else
                      {
                        LOG_S(WARNING) << "DeviceN color space: names array missing";
                      }

                    resolve_tint_colorspace(qpdf_cs);
                  }
                else if(name_obj.isName() and name_obj.getName() == "/Separation")
                  {
                    // The samples are tints of one colorant; only the tint
                    // transform says what colour they stand for.
                    resolve_tint_colorspace(qpdf_cs);
                  }
                else if(name_obj.isName() and name_obj.getName() == "/Indexed"
                        and qpdf_cs.getArrayNItems() >= 3)
                  {
                    // [/Indexed, base, hival, lookup]

                    // base color space
                    auto base_obj = qpdf_cs.getArrayItem(1);
                    indexed_base_device_n_single_black = false;
                    indexed_base_icc_profile.reset();
                    indexed_base_icc_components = 0;
                    indexed_base_device_n_names.clear();
                    if(base_obj.isName())
                      {
                        indexed_base_cs = base_obj.getName();
                      }
                    else if(base_obj.isArray() and base_obj.getArrayNItems() >= 2)
                      {
                        auto base_name = base_obj.getArrayItem(0);
                        if(base_name.isName() and base_name.getName() == "/ICCBased")
                          {
                            auto icc_stream = base_obj.getArrayItem(1);
                            if(icc_stream.isStream())
                              {
                                auto profile_buf = to_shared_ptr(icc_stream.getStreamData());
                                if(profile_buf and profile_buf->getSize() > 0)
                                  {
                                    auto const* ptr = reinterpret_cast<const uint8_t*>(
                                      profile_buf->getBuffer());
                                    indexed_base_icc_profile =
                                      std::make_shared<std::vector<uint8_t>>(
                                        ptr, ptr + profile_buf->getSize());
                                  }

                                auto icc_dict = icc_stream.getDict();
                                if(icc_dict.hasKey("/N") and icc_dict.getKey("/N").isInteger())
                                  {
                                    const int n = icc_dict.getKey("/N").getIntValue();
                                    indexed_base_icc_components = n;
                                    if(n == 1)      { indexed_base_cs = "/DeviceGray"; }
                                    else if(n == 3) { indexed_base_cs = "/DeviceRGB"; }
                                    else if(n == 4) { indexed_base_cs = "/DeviceCMYK"; }
                                    else
                                      {
                                        LOG_S(WARNING) << "Indexed ICCBased base has unsupported /N="
                                                       << n;
                                      }
                                    LOG_S(INFO) << "Indexed ICCBased base: N=" << n
                                                << " -> " << indexed_base_cs;
                                  }
                                else
                                  {
                                    LOG_S(WARNING) << "Indexed ICCBased base missing /N entry";
                                    const int n = detail::infer_icc_components_from_profile(
                                      icc_stream, "Indexed ICCBased base");
                                    indexed_base_icc_components = n;
                                    if(n == 1)      { indexed_base_cs = "/DeviceGray"; }
                                    else if(n == 3) { indexed_base_cs = "/DeviceRGB"; }
                                    else if(n == 4) { indexed_base_cs = "/DeviceCMYK"; }
                                  }
                              }
                            else
                              {
                                LOG_S(WARNING) << "Indexed ICCBased base: second array element is not a stream";
                              }
                          }
                        else if(base_name.isName() and base_name.getName() == "/DeviceN")
                          {
                            auto names_obj = base_obj.getArrayItem(1);
                            if(names_obj.isArray())
                              {
                                std::vector<std::string> nested_names;
                                for(int i = 0; i < names_obj.getArrayNItems(); ++i)
                                  {
                                    auto name = names_obj.getArrayItem(i);
                                    if(name.isName())
                                      {
                                        nested_names.push_back(name.getName());
                                      }
                                  }
                                indexed_base_device_n_names = nested_names;

                                const int nested_n = static_cast<int>(nested_names.size());
                                const bool single_black =
                                  nested_n == 1
                                  and nested_names[0] == "/Black";
                                const bool process_cmyk_subset =
                                  detail::device_n_names_are_process_cmyk_subset(nested_names);
                                indexed_base_device_n_single_black = single_black;

                                if(single_black)       { indexed_base_cs = "/DeviceGray"; }
                                else if(process_cmyk_subset)
                                  {
                                    indexed_base_cs = "/DeviceCMYK";
                                    LOG_S(INFO) << "Indexed DeviceN base uses process CMYK subset; "
                                                << "will expand palette to CMYK";
                                  }
                                else if(nested_n == 3)
                                  {
                                    indexed_base_cs = "/DeviceRGB";
                                  }
                                else if(nested_n == 4)
                                  {
                                    indexed_base_cs = "/DeviceCMYK";
                                  }
                                else
                                  {
                                    indexed_base_cs = "/DeviceN";
                                    LOG_S(WARNING) << "Indexed DeviceN base has unsupported component layout N="
                                                   << nested_n;
                                  }
                                LOG_S(INFO) << "Indexed DeviceN base: N=" << nested_n
                                            << " -> " << indexed_base_cs;
                              }
                          }
                        else if(base_name.isName())
                          {
                            indexed_base_cs = base_name.getName();
                            LOG_S(INFO) << "Indexed array base color space: " << indexed_base_cs;
                          }

                        // A /Separation (or unhandled /DeviceN) base: the
                        // palette entries are TINT values, one sample per
                        // colorant, meaningless until run through the tint
                        // transform. Parse the space now; the palette is
                        // converted to RGB right after it is loaded.
                        if(base_name.isName() and
                           (base_name.getName() == "/Separation" or
                            (base_name.getName() == "/DeviceN" and
                             indexed_base_cs != "/DeviceCMYK" and
                             indexed_base_cs != "/DeviceGray")))
                          {
                            indexed_tint_space =
                              std::make_shared<pdf_resource<PAGE_COLORSPACE> >();
                            indexed_tint_space->set(xobject_key + "/IndexedBase",
                                                    base_obj);
                            if(not indexed_tint_space->has_tint_transform())
                              {
                                LOG_S(WARNING) << "Indexed tint base did not resolve "
                                               << "for xobject_key=" << xobject_key;
                                indexed_tint_space.reset();
                              }
                          }
                      }
                    else
                      {
                        LOG_S(WARNING) << "Indexed color space: unsupported base object type";
                      }

                    // hival
                    auto hival_obj = qpdf_cs.getArrayItem(2);
                    if(hival_obj.isInteger())
                      {
                        indexed_hival = static_cast<int>(hival_obj.getIntValue());
                        LOG_S(INFO) << "Indexed color space: base=" << indexed_base_cs
                                    << " hival=" << indexed_hival;
                      }
                    else
                      {
                        LOG_S(WARNING) << "Indexed color space: hival is not an integer";
                      }

                    // palette (lookup table): string or stream
                    if(qpdf_cs.getArrayNItems() >= 4)
                      {
                        auto lookup_obj = qpdf_cs.getArrayItem(3);
                        if(lookup_obj.isString())
                          {
                            std::string raw = lookup_obj.getStringValue();
                            indexed_palette = std::make_shared<std::vector<uint8_t>>(
                              raw.begin(), raw.end());
                            LOG_S(INFO) << "Indexed palette: " << indexed_palette->size()
                                        << " bytes (string)";
                          }
                        else if(lookup_obj.isStream())
                          {
                            auto stream_buf = to_shared_ptr(lookup_obj.getStreamData());
                            if(stream_buf)
                              {
                                const auto* ptr = reinterpret_cast<const uint8_t*>(
                                  stream_buf->getBuffer());
                                indexed_palette = std::make_shared<std::vector<uint8_t>>(
                                  ptr, ptr + stream_buf->getSize());
                                LOG_S(INFO) << "Indexed palette: " << indexed_palette->size()
                                            << " bytes (stream)";
                              }
                          }
                        else
                          {
                            LOG_S(WARNING) << "Indexed color space: unrecognized lookup table type";
                          }

                        // Tint-space base: run every palette entry through
                        // the tint transform and keep an RGB palette. Without
                        // this the image has no usable base space at all and
                        // is dropped for a placeholder.
                        if(indexed_tint_space and indexed_palette
                           and not indexed_palette->empty())
                          {
                            const int ncomp = std::max<int>(
                              1, indexed_tint_space->tint_component_count());
                            const size_t entries = indexed_palette->size() / ncomp;
                            auto rgb_pal = std::make_shared<std::vector<uint8_t>>();
                            rgb_pal->reserve(entries * 3);
                            std::vector<double> comps(ncomp, 0.0);
                            bool ok = entries > 0;
                            for(size_t e = 0; ok and e < entries; ++e)
                              {
                                for(int c = 0; c < ncomp; ++c)
                                  {
                                    comps[c] = (*indexed_palette)[e * ncomp + c] / 255.0;
                                  }
                                std::array<int, 3> rgb = {0, 0, 0};
                                if(indexed_tint_space->map_to_rgb(comps, rgb))
                                  {
                                    rgb_pal->push_back(static_cast<uint8_t>(rgb[0]));
                                    rgb_pal->push_back(static_cast<uint8_t>(rgb[1]));
                                    rgb_pal->push_back(static_cast<uint8_t>(rgb[2]));
                                  }
                                else { ok = false; }
                              }
                            if(ok)
                              {
                                indexed_palette = std::move(rgb_pal);
                                indexed_base_cs = "/DeviceRGB";
                                LOG_S(INFO) << "Indexed tint-base palette converted to RGB: "
                                            << indexed_palette->size() << " bytes";
                              }
                            else
                              {
                                LOG_S(WARNING) << "Indexed tint-base palette conversion failed";
                              }
                          }

                        if(indexed_base_cs == "/DeviceCMYK"
                           and not indexed_base_device_n_names.empty()
                           and detail::device_n_names_are_process_cmyk_subset(
                             indexed_base_device_n_names))
                          {
                            auto expanded =
                              detail::expand_device_n_palette_to_cmyk(indexed_palette,
                                                                      indexed_base_device_n_names);
                            if(expanded)
                              {
                                indexed_palette = std::move(expanded);
                                LOG_S(INFO) << "Indexed DeviceN palette expanded to CMYK: "
                                            << indexed_palette->size() << " bytes";
                              }
                            else
                              {
                                LOG_S(WARNING) << "Indexed DeviceN palette expansion to CMYK failed";
                              }
                          }

                        if(indexed_base_icc_profile
                           and not indexed_base_icc_profile->empty()
                           and indexed_base_icc_components > 0
                           and indexed_palette
                           and not indexed_palette->empty())
                          {
                            auto rgb_palette = icc::transform_palette_to_rgb(
                              *indexed_palette,
                              indexed_base_icc_components,
                              *indexed_base_icc_profile);
                            if(not rgb_palette.empty())
                              {
                                indexed_palette = std::make_shared<std::vector<uint8_t>>(
                                  std::move(rgb_palette));
                                indexed_base_cs = "/DeviceRGB";
                                indexed_base_device_n_names.clear();
                                indexed_base_device_n_single_black = false;
                                LOG_S(INFO) << "Indexed ICCBased palette converted to RGB: "
                                            << indexed_palette->size() << " bytes";
                              }
                            else
                              {
                                LOG_S(WARNING) << "Indexed ICCBased palette RGB conversion failed";
                              }
                          }
                      }
                  }
              }
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
        if(image_mask)
          {
            LOG_S(INFO) << "no `/Decode` found: using default [0 1] for image mask";
            decode_array = {0.0, 1.0};
            decode_present = true;
          }
        else if(color_space=="/DeviceGray")
	  {
	    // p 210, table 90: Default decode arrays
	    LOG_S(WARNING) << "no `/Decode` found: falling back on default for " << color_space;
	    decode_array = {
	      //1, 0
	      0, 1
	    };
	    decode_present = !decode_array.empty();
	  }
	else if(color_space=="/DeviceRGB")
	  {
	    LOG_S(WARNING) << "no `/Decode` found: falling back on default for " << color_space;
	    decode_array = {
	      //1, 0, 1, 0, 1, 0
	      0, 1, 0, 1, 0, 1
	    };
	    decode_present = !decode_array.empty();
	  }
	else if(color_space=="/DeviceCMYK")
	  {
	    // ISO 32000-1 table 89: the default is the identity, [0 1] per
	    // component. Synthesising the inverted array here flipped every CMYK
	    // sample once; paths whose convention flag inverted a second time
	    // cancelled it by accident, and the one that did not (JPX) rendered
	    // photographic negatives.
	    LOG_S(WARNING) << "no `/Decode` found: falling back on default for " << color_space;
	    decode_array = {
	      0, 1, 0, 1,
	      0, 1, 0, 1
	    };
	    decode_present = !decode_array.empty();
	  }
	else if(icc_components > 0)
	  {
	    // ICCBased: use identity decode [0 1] per component
	    LOG_S(INFO) << "no `/Decode` found: using identity for ICCBased N=" << icc_components;
	    for(int i = 0; i < icc_components; ++i)
	      {
		decode_array.push_back(0.0);
		decode_array.push_back(1.0);
	      }
	    decode_present = not decode_array.empty();
	  }
        else if(tint_components > 0)
          {
            // Table 90: the default for /Separation and /DeviceN is [0 1] per
            // colorant. The samples are tints and the tint transform reads them
            // as such, so there is nothing to invert here.
            LOG_S(INFO) << "no `/Decode` found: using identity for a tint space"
                        << " with " << tint_components << " colorant(s)";
            for(int i = 0; i < tint_components; ++i)
              {
                decode_array.push_back(0.0);
                decode_array.push_back(1.0);
              }
            decode_present = not decode_array.empty();
          }
        else if(device_n_components > 0)
          {
            // No usable tint transform: the tints are shown as if they were
            // device components, and a lone /Black tint has to be inverted to
            // come out as ink rather than as light.
            const bool single_black =
              device_n_components == 1
              and device_n_names.size() == 1
              and device_n_names[0] == "/Black";
            LOG_S(INFO) << "no `/Decode` found: using default for DeviceN N="
                        << device_n_components
                        << " single_black=" << (single_black ? "true" : "false");
            for(int i = 0; i < device_n_components; ++i)
              {
                decode_array.push_back(single_black ? 1.0 : 0.0);
                decode_array.push_back(single_black ? 0.0 : 1.0);
              }
            decode_present = not decode_array.empty();
          }
	else if(indexed_hival >= 0)
	  {
	    // Indexed: default decode is [0, hival] (one component — the palette index)
	    // ISO 32000-1 table 90: the default is [0, 2^bpc - 1], the full range
	    // representable at this bit depth, NOT [0, hival]. A decode maps a raw
	    // sample through (Dmax - Dmin) / (2^bpc - 1), so [0, hival] silently
	    // rescales every index by hival/(2^bpc - 1) and looks up the wrong
	    // palette entry -- a 4-bit flag lost its red and white to index >> 1.
	    const int idx_bpc = (bits_per_component > 0 ? bits_per_component : 8);
	    const double idx_max = static_cast<double>((1u << idx_bpc) - 1u);
	    LOG_S(INFO) << "no `/Decode` found: using [0, " << idx_max
	                << "] for Indexed color space (bpc=" << idx_bpc << ")";
	    decode_array = { 0.0, idx_max };
	    decode_present = true;
	  }
	else
	  {
	    LOG_S(WARNING) << "no `/Decode` found and color space not recognized: " << color_space;
	  }
      }
    // /DecodeParms — extract CCITT-specific keys (/K, /BlackIs1)
    if(json_xobject_dict.count("/DecodeParms"))
      {
        auto& dp = json_xobject_dict["/DecodeParms"];
        int decode_parms_index = -1;
        for(std::size_t i = 0; i < image_filters.size(); ++i)
          {
            if(image_filters[i] == "/JBIG2Decode" or image_filters[i] == "/CCITTFaxDecode")
              {
                decode_parms_index = static_cast<int>(i);
                break;
              }
          }
        LOG_S(INFO) << "DecodeParms lookup for xobject_key=" << xobject_key
                    << " filter_index=" << decode_parms_index
                    << " filters=" << nlohmann::json(image_filters).dump();

        // DecodeParms can be a dict or an array of dicts (one per filter).
        // When it is an array, choose the object corresponding to the relevant
        // filter instead of always assuming index 0.
        auto* parms_ptr = dp.is_object() ? &dp : nullptr;
        if(dp.is_array())
          {
            if(decode_parms_index >= 0
               and decode_parms_index < static_cast<int>(dp.size())
               and dp[decode_parms_index].is_object())
              {
                parms_ptr = &dp[decode_parms_index];
              }
            else if(not dp.empty() and dp[0].is_object())
              {
                LOG_S(WARNING) << "DecodeParms array missing dictionary at filter index "
                               << decode_parms_index << ", falling back to index 0";
                parms_ptr = &dp[0];
              }
          }
        if(parms_ptr)
          {
            auto& parms = *parms_ptr;
            LOG_S(INFO) << "selected DecodeParms for xobject_key=" << xobject_key
                        << ": " << parms.dump();
            if(parms.count("/K") and parms["/K"].is_number())
              {
                ccitt_params.k = parms["/K"].get<int>();
              }
            if(parms.count("/BlackIs1") and parms["/BlackIs1"].is_boolean())
              {
                ccitt_params.black_is_1 = parms["/BlackIs1"].get<bool>();
              }

            // /Columns is the width the row codes were written against; it
            // defaults to 1728 (Table 11), not to /Width. Decoding at the
            // image's width when the encoder used another count loses sync on
            // the first row and leaves the image blank.
            if(parms.count("/Columns") and parms["/Columns"].is_number())
              {
                ccitt_params.columns = parms["/Columns"].get<int>();
              }
            if(parms.count("/EncodedByteAlign") and parms["/EncodedByteAlign"].is_boolean())
              {
                ccitt_params.encoded_byte_align = parms["/EncodedByteAlign"].get<bool>();
              }

            auto qpdf_dp = qpdf_xobject_dict.getKey("/DecodeParms");
            QPDFObjectHandle qpdf_parms;
            if(qpdf_dp.isDictionary())
              {
                qpdf_parms = qpdf_dp;
              }
            else if(qpdf_dp.isArray())
              {
                if(decode_parms_index >= 0
                   and decode_parms_index < qpdf_dp.getArrayNItems()
                   and qpdf_dp.getArrayItem(decode_parms_index).isDictionary())
                  {
                    qpdf_parms = qpdf_dp.getArrayItem(decode_parms_index);
                  }
                else if(qpdf_dp.getArrayNItems() > 0 and qpdf_dp.getArrayItem(0).isDictionary())
                  {
                    LOG_S(WARNING) << "QPDF DecodeParms array missing dictionary at filter index "
                                   << decode_parms_index << ", falling back to index 0";
                    qpdf_parms = qpdf_dp.getArrayItem(0);
                  }
              }

            if(qpdf_parms.isDictionary() and qpdf_parms.hasKey("/JBIG2Globals"))
              {
                auto globals_stream = qpdf_parms.getKey("/JBIG2Globals");
                if(globals_stream.isStream())
                  {
                    try
                      {
                        jbig2_globals_data = to_shared_ptr(globals_stream.getStreamData());
                        LOG_S(INFO) << "JBIG2Globals source=decoded for xobject_key="
                                    << xobject_key;
                        LOG_S(INFO) << "JBIG2Globals size: "
                                    << (jbig2_globals_data ? jbig2_globals_data->getSize() : 0)
                                    << " bytes";
                      }
                    catch(std::exception const& e)
                      {
                        LOG_S(WARNING) << "failed to get decoded JBIG2Globals stream data: "
                                       << e.what() << " -- falling back to raw stream data";
                        try
                          {
                            jbig2_globals_data = to_shared_ptr(globals_stream.getRawStreamData());
                            LOG_S(INFO) << "JBIG2Globals source=raw for xobject_key="
                                        << xobject_key;
                            LOG_S(INFO) << "JBIG2Globals size: "
                                        << (jbig2_globals_data ? jbig2_globals_data->getSize() : 0)
                                        << " bytes";
                          }
                        catch(std::exception const& raw_e)
                          {
                            LOG_S(WARNING) << "failed to get raw JBIG2Globals stream data: "
                                           << raw_e.what();
                            jbig2_globals_data = nullptr;
                          }
                      }
                  }
                else
                  {
                    LOG_S(WARNING) << "/JBIG2Globals present but is not a stream";
                  }
              }
          }
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

    init_codec_stream_data();
  }

  void pdf_resource<PAGE_XOBJECT_IMAGE>::init_codec_stream_data()
  {
    // QPDF declares a whole /Filter chain unfilterable as soon as one of its
    // filters is an image codec it will not invert, so `decoded_stream_data`
    // is empty for /CCITTFaxDecode, /JBIG2Decode and /JPXDecode images and the
    // codec decoders fall back to the stream data here. That fallback is only
    // correct once the transport filters in front of the codec are undone: a
    // `/Filter [/ASCII85Decode /CCITTFaxDecode]` image hands a CCITT decoder
    // ASCII85 text otherwise, which decodes into noise.
    codec_stream_data = raw_stream_data;

    if(not raw_stream_data)
      {
        return;
      }

    const std::size_t codec_index = stream_filters::image_codec_index(image_filters);
    if(codec_index == 0 or codec_index >= image_filters.size())
      {
        return;
      }

    QPDFObjectHandle decode_parms = qpdf_xobject_dict.isDictionary()
      ? qpdf_xobject_dict.getKey("/DecodeParms")
      : QPDFObjectHandle::newNull();

    auto pre_decoded = stream_filters::apply_filters(raw_stream_data,
                                                     image_filters,
                                                     codec_index,
                                                     decode_parms);

    if(pre_decoded)
      {
        codec_stream_data = pre_decoded;
        LOG_S(INFO) << "undid " << codec_index << " transport filter(s) in front of "
                    << image_filters[codec_index] << " for xobject_key=" << xobject_key
                    << ": " << raw_stream_data->getSize() << " -> "
                    << codec_stream_data->getSize() << " bytes";
      }
    else
      {
        LOG_S(WARNING) << "failed to undo the transport filters in front of "
                       << image_filters[codec_index] << " for xobject_key=" << xobject_key
                       << " -- the codec will see encoded bytes";
      }
  }

  void pdf_resource<PAGE_XOBJECT_IMAGE>::init_soft_mask_data()
  {
    soft_mask_data.reset();

    // /SMask carries alpha directly. /Mask (as a stream) is a stencil: a
    // 1-bit image mask whose 1-samples knock the corresponding image pixels
    // OUT (ISO 32000-1, 8.9.6.4). Ignoring it painted the image's own
    // background pixels over whatever the mask was meant to reveal -- a logo
    // shipped as an opaque rectangle plus a stencil rendered as the
    // rectangle.
    bool stencil = false;
    QPDFObjectHandle qpdf_smask;
    if(qpdf_xobject_dict.hasKey("/SMask"))
      {
        qpdf_smask = qpdf_xobject_dict.getKey("/SMask");
      }
    else if(qpdf_xobject_dict.hasKey("/Mask") and
            qpdf_xobject_dict.getKey("/Mask").isStream())
      {
        qpdf_smask = qpdf_xobject_dict.getKey("/Mask");
        stencil = true;
      }
    else
      {
        return;
      }

    if(not qpdf_smask.isStream())
      {
        LOG_S(WARNING) << "SMask present but is not a stream for xobject_key=" << xobject_key;
        return;
      }

    pdf_resource<PAGE_XOBJECT_IMAGE> smask;
    smask.set(xobject_key + "/SMask", qpdf_smask, true);

    const int sm_w = smask.get_image_width();
    const int sm_h = smask.get_image_height();

    if(sm_w <= 0 or sm_h <= 0 or image_width <= 0 or image_height <= 0)
      {
        LOG_S(WARNING) << "SMask with invalid dimensions for xobject_key=" << xobject_key
                       << " image=" << image_width << "x" << image_height
                       << " smask=" << sm_w << "x" << sm_h;
        return;
      }

    // Per PDF spec the soft mask does not need to match the base image's
    // dimensions (both map onto the image's unit square); a differing mask
    // is resampled onto the image grid below.
    if(sm_w != image_width or sm_h != image_height)
      {
        LOG_S(INFO) << "SMask size differs for xobject_key=" << xobject_key
                    << " image=" << image_width << "x" << image_height
                    << " smask=" << sm_w << "x" << sm_h
                    << " -- resampling (nearest neighbor)";
      }

    const bool gray_mask =
      smask.get_color_space() == "/DeviceGray"
      or (smask.get_color_space().find("/ICCBased") != std::string::npos
          and smask.get_icc_components() == 1);
    if(not gray_mask and not stencil)
      {
        LOG_S(WARNING) << "SMask color space unsupported for xobject_key=" << xobject_key
                       << " smask_cs=" << smask.get_color_space()
                       << " smask_icc_components=" << smask.get_icc_components();
        return;
      }

    int smask_bpc = smask.get_bits_per_component();
    if(stencil and smask_bpc <= 0) { smask_bpc = 1; }
    if(smask_bpc != 8 and smask_bpc != 1 and smask_bpc != 2 and smask_bpc != 4)
      {
        LOG_S(WARNING) << "SMask bits/component unsupported for xobject_key=" << xobject_key
                       << " smask_bpc=" << smask_bpc;
        return;
      }

    // A stencil mask is very often CCITTFax-compressed, which qpdf cannot
    // defilter; decode it here. `ccitt::decode` yields one byte per pixel
    // with 0 = dark.
    std::vector<uint8_t> ccitt_samples;
    if(not smask.has_decoded_stream_data())
      {
        QPDFObjectHandle mdict = qpdf_smask.getDict();
        const std::string filter =
          mdict.hasKey("/Filter") ? mdict.getKey("/Filter").unparse() : "";
        if(filter.find("CCITTFaxDecode") != std::string::npos and
           smask.has_codec_stream_data())
          {
            ccitt::decode_parameters ccitt_parms;
            QPDFObjectHandle dp = mdict.getKey("/DecodeParms");
            if(dp.isArray() and dp.getArrayNItems() > 0) { dp = dp.getArrayItem(0); }
            if(dp.isDictionary())
              {
                if(dp.hasKey("/K") and dp.getKey("/K").isInteger())
                  { ccitt_parms.k = static_cast<int>(dp.getKey("/K").getIntValue()); }
                if(dp.hasKey("/BlackIs1") and dp.getKey("/BlackIs1").isBool())
                  { ccitt_parms.black_is_1 = dp.getKey("/BlackIs1").getBoolValue(); }
                if(dp.hasKey("/Columns") and dp.getKey("/Columns").isInteger())
                  { ccitt_parms.columns = static_cast<int>(dp.getKey("/Columns").getIntValue()); }
                if(dp.hasKey("/EncodedByteAlign") and dp.getKey("/EncodedByteAlign").isBool())
                  { ccitt_parms.encoded_byte_align = dp.getKey("/EncodedByteAlign").getBoolValue(); }
              }

            auto encoded = smask.get_codec_stream_data();
            ccitt_samples = ccitt::decode(
              reinterpret_cast<const uint8_t*>(encoded->getBuffer()),
              encoded->getSize(), sm_w, sm_h, ccitt_parms);
            if(ccitt_samples.size() < static_cast<size_t>(sm_w) * sm_h)
              {
                LOG_S(WARNING) << "mask CCITT decode too small for xobject_key="
                               << xobject_key;
                return;
              }
            smask_bpc = 8;
          }
        else
          {
            LOG_S(WARNING) << "SMask has no decoded stream data for xobject_key=" << xobject_key;
            return;
          }
      }

    auto smask_buf = smask.has_decoded_stream_data()
      ? smask.get_decoded_stream_data() : nullptr;

    // A stencil-shaped mask is usually written at 1 bit per pixel, packed
    // several samples to a byte and padded to a byte at the end of each row.
    // Reading those bytes as if each held one sample takes the mask's first
    // eight columns to be the whole row, so the transparent part of the image
    // keeps painting. Expand to one byte per sample first.
    std::vector<uint8_t> smask_unpacked;
    if(smask_bpc != 8 and ccitt_samples.empty())
      {
        const size_t row_bits = static_cast<size_t>(sm_w) * static_cast<size_t>(smask_bpc);
        const size_t row_bytes = (row_bits + 7u) / 8u;
        const size_t needed = row_bytes * static_cast<size_t>(sm_h);
        if(smask_buf->getSize() < needed)
          {
            LOG_S(WARNING) << "SMask packed stream too small for xobject_key=" << xobject_key
                           << " size=" << smask_buf->getSize()
                           << " expected>=" << needed
                           << " smask_bpc=" << smask_bpc;
            return;
          }

        const auto* packed = reinterpret_cast<uint8_t const*>(smask_buf->getBuffer());
        const uint32_t sample_max = (1u << smask_bpc) - 1u;
        smask_unpacked.resize(static_cast<size_t>(sm_w) * sm_h);

        for(int row = 0; row < sm_h; ++row)
          {
            const uint8_t* row_ptr = packed + static_cast<size_t>(row) * row_bytes;
            size_t bit_offset = 0;
            for(int col = 0; col < sm_w; ++col)
              {
                uint32_t raw = 0u;
                for(int bit = 0; bit < smask_bpc; ++bit)
                  {
                    const size_t abs_bit = bit_offset + static_cast<size_t>(bit);
                    const int in_byte = 7 - static_cast<int>(abs_bit % 8u);
                    raw = (raw << 1u)
                      | static_cast<uint32_t>((row_ptr[abs_bit / 8u] >> in_byte) & 1u);
                  }
                bit_offset += static_cast<size_t>(smask_bpc);
                smask_unpacked[static_cast<size_t>(row) * sm_w + col] =
                  static_cast<uint8_t>((raw * 255u) / sample_max);
              }
          }
      }

    const size_t smask_expected = static_cast<size_t>(sm_w) * sm_h;
    if(smask_unpacked.empty() and ccitt_samples.empty()
       and smask_buf->getSize() < smask_expected)
      {
        LOG_S(WARNING) << "SMask decoded stream too small for xobject_key=" << xobject_key
                       << " size=" << smask_buf->getSize()
                       << " expected>=" << smask_expected;
        return;
      }

    // Resolve onto the finer of the two grids. Downsampling a mask that is
    // higher-resolution than its base image would throw away exactly the
    // detail it exists to carry -- a 2x2 colour swatch stencilled by a 141x100
    // glyph mask is a common way to draw text, and collapsing it to 2x2 turns
    // the text into a gradient smear across the whole line.
    const int dst_w = std::max(image_width, sm_w);
    const int dst_h = std::max(image_height, sm_h);

    auto out = std::make_shared<std::vector<uint8_t>>();
    out->resize(static_cast<size_t>(dst_w) * dst_h);

    auto const* src =
      not ccitt_samples.empty() ? ccitt_samples.data()
      : not smask_unpacked.empty() ? smask_unpacked.data()
      : reinterpret_cast<uint8_t const*>(smask_buf->getBuffer());
    auto const decode = smask.get_decode_array();
    const bool has_decode = smask.has_decode_array() and decode.size() >= 2;

    // nearest-neighbor sampling from the mask grid onto the target grid
    // (identity when the dimensions already match)
    for(int row = 0; row < dst_h; ++row)
      {
        const size_t src_row = (static_cast<size_t>(row) * sm_h) / dst_h;
        for(int col = 0; col < dst_w; ++col)
          {
            const size_t src_col = (static_cast<size_t>(col) * sm_w) / dst_w;

            uint8_t alpha = src[src_row * sm_w + src_col];
            if(has_decode)
              {
                alpha = jpeg::apply_decode_component(alpha, decode[0], decode[1]);
              }
            // Stencil (/Mask): a 1-sample masks the pixel OUT, the inverse
            // of /SMask's alpha reading. ccitt::decode returns 0 = dark =
            // sample 0 = painted, so the same inversion applies there.
            (*out)[static_cast<size_t>(row) * dst_w + col] =
              stencil ? static_cast<uint8_t>(255 - alpha) : alpha;
          }
      }

    soft_mask_data   = std::move(out);
    soft_mask_width  = dst_w;
    soft_mask_height = dst_h;

    LOG_S(INFO) << "decoded SMask for xobject_key=" << xobject_key
                << " alpha_size=" << soft_mask_data->size();
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

  int pdf_resource<PAGE_XOBJECT_IMAGE>::get_icc_components() const
  {
    return icc_components;
  }

  int pdf_resource<PAGE_XOBJECT_IMAGE>::get_device_n_components() const
  {
    return device_n_components;
  }

  std::vector<std::string> pdf_resource<PAGE_XOBJECT_IMAGE>::get_device_n_names() const
  {
    return device_n_names;
  }

  std::shared_ptr<pdf_resource<PAGE_COLORSPACE>>
  pdf_resource<PAGE_XOBJECT_IMAGE>::get_tint_colorspace() const
  {
    return tint_colorspace;
  }

  int pdf_resource<PAGE_XOBJECT_IMAGE>::get_tint_components() const
  {
    return tint_components;
  }

  void pdf_resource<PAGE_XOBJECT_IMAGE>::resolve_tint_colorspace(QPDFObjectHandle qpdf_cs)
  {
    auto colorspace = std::make_shared<pdf_resource<PAGE_COLORSPACE>>();
    colorspace->set(xobject_key + "/ColorSpace", qpdf_cs);

    const color_space_family family = colorspace->get_family();
    if(family != COLOR_SPACE_SEPARATION and family != COLOR_SPACE_DEVICE_N)
      {
        return;
      }

    // Without a usable transform the tints cannot become colour. Reading them
    // as device components is wrong, but it still shows the artwork, which the
    // "unsupported colour space" path does not.
    if(not colorspace->has_tint_transform())
      {
        if(family == COLOR_SPACE_SEPARATION and device_n_components == 0)
          {
            device_n_components = 1;
            device_n_names.clear();

            QPDFObjectHandle colorant = qpdf_cs.getArrayItem(1);
            device_n_names.push_back(colorant.isName() ? colorant.getName() : "");
          }

        LOG_S(WARNING) << "tint colorspace for xobject_key=" << xobject_key
                       << " has no usable tint transform, reading the tints as"
                       << " device components";
        return;
      }

    tint_colorspace = colorspace;
    tint_components = colorspace->get_num_components();

    LOG_S(INFO) << "tint colorspace for xobject_key=" << xobject_key
                << ": " << tint_components << " colorant(s) through a tint transform";
  }

  int pdf_resource<PAGE_XOBJECT_IMAGE>::get_indexed_hival() const
  {
    return indexed_hival;
  }

  std::string pdf_resource<PAGE_XOBJECT_IMAGE>::get_indexed_base_cs() const
  {
    return indexed_base_cs;
  }

  std::shared_ptr<std::vector<uint8_t>> pdf_resource<PAGE_XOBJECT_IMAGE>::get_indexed_palette() const
  {
    return indexed_palette;
  }

  std::vector<std::string> pdf_resource<PAGE_XOBJECT_IMAGE>::get_indexed_base_device_n_names() const
  {
    return indexed_base_device_n_names;
  }

  bool pdf_resource<PAGE_XOBJECT_IMAGE>::get_indexed_base_device_n_single_black() const
  {
    return indexed_base_device_n_single_black;
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

  ccitt::decode_parameters pdf_resource<PAGE_XOBJECT_IMAGE>::get_ccitt_parameters() const
  {
    return ccitt_params;
  }

  bool pdf_resource<PAGE_XOBJECT_IMAGE>::has_jbig2_globals_data() const
  {
    return (jbig2_globals_data != nullptr && jbig2_globals_data->getSize() > 0);
  }

  std::shared_ptr<Buffer> pdf_resource<PAGE_XOBJECT_IMAGE>::get_jbig2_globals_data() const
  {
    return jbig2_globals_data;
  }

  bool pdf_resource<PAGE_XOBJECT_IMAGE>::has_raw_stream_data() const
  {
    return (raw_stream_data != nullptr && raw_stream_data->getSize() > 0);
  }

  std::shared_ptr<Buffer> pdf_resource<PAGE_XOBJECT_IMAGE>::get_raw_stream_data() const
  {
    return raw_stream_data;
  }

  bool pdf_resource<PAGE_XOBJECT_IMAGE>::has_codec_stream_data() const
  {
    return (codec_stream_data != nullptr && codec_stream_data->getSize() > 0);
  }

  std::shared_ptr<Buffer> pdf_resource<PAGE_XOBJECT_IMAGE>::get_codec_stream_data() const
  {
    return codec_stream_data;
  }

  bool pdf_resource<PAGE_XOBJECT_IMAGE>::has_decoded_stream_data() const
  {
    return (decoded_stream_data != nullptr && decoded_stream_data->getSize() > 0);
  }

  std::shared_ptr<Buffer> pdf_resource<PAGE_XOBJECT_IMAGE>::get_decoded_stream_data() const
  {
    return decoded_stream_data;
  }

  bool pdf_resource<PAGE_XOBJECT_IMAGE>::has_soft_mask_data() const
  {
    return (soft_mask_data != nullptr and not soft_mask_data->empty());
  }

  std::shared_ptr<std::vector<uint8_t>> pdf_resource<PAGE_XOBJECT_IMAGE>::get_soft_mask_data() const
  {
    return soft_mask_data;
  }

  // --- File I/O ---

  std::string pdf_resource<PAGE_XOBJECT_IMAGE>::pick_extension() const
  {
    // Image-format filters take priority — /FlateDecode is just transport
    // compression and can appear alongside any of these.
    bool has_flate = false;

    for(auto const& f : image_filters)
      {
        if(f == "/DCTDecode")
          {
            return ".jpg";
          }
        else if(f == "/JPXDecode")
          {
            return ".jp2";
          }
        else if(f == "/JBIG2Decode")
          {
            return ".jb2";
          }
        else if(f == "/FlateDecode")
          {
            has_flate = true;
          }
        else
          {
            LOG_S(WARNING) << "pick_extension: unrecognized filter `" << f << "`";
          }
      }

    if(has_flate)
      {
        // /FlateDecode only (no image-format filter) → raw pixels after
        // decompression.  We encode them as JPEG for a viewable output.
        // Future: return ".png" here for lossless export (requires libpng or lodepng).
        return ".jpg";
      }

    LOG_S(WARNING) << "pick_extension: no recognized filter, defaulting to .bin";
    return ".bin";
  }

  void pdf_resource<PAGE_XOBJECT_IMAGE>::save_to_file(std::filesystem::path const& path) const
  {
    // What gets written is the image as its codec encoded it, so the bytes to
    // save are the ones the codec was handed -- the raw stream with any
    // transport filters in front of the codec (e.g. /ASCII85Decode) undone.
    if(not has_codec_stream_data())
      {
        LOG_S(WARNING) << "no stream data to save";
        return;
      }

    auto ext = path.extension().string();
    for(auto& c : ext) c = static_cast<char>(::tolower(c));
    bool is_jpeg_ext = (ext == ".jpg" or ext == ".jpeg");

    bool filters_have_dct = false;
    for(auto const& f : image_filters)
      {
        if(f == "/DCTDecode") { filters_have_dct = true; }
      }

    // Resolve effective JPEG colour space: Device spaces map directly; for
    // /ICCBased we use the /N component count to pick the Device equivalent.
    jpeg::ColorSpace effective_cs = jpeg::to_color_space(color_space);
    if(effective_cs == jpeg::ColorSpace::Unknown and icc_components > 0)
      {
        effective_cs = jpeg::icc_n_to_color_space(icc_components);
      }

    auto is_safe_passthrough = [&]() -> bool {
      if(not is_jpeg_ext) { return false; }
      if(not filters_have_dct) { return false; }
      if(bits_per_component != 8) { return false; }
      if(effective_cs == jpeg::ColorSpace::Unknown) { return false; }
      if(image_mask) { return false; }
      if(decode_present and not decode_array.empty())
        {
          int ncomp = (effective_cs == jpeg::ColorSpace::Gray) ? 1
            : (effective_cs == jpeg::ColorSpace::CMYK) ? 4 : 3;
          if(static_cast<int>(decode_array.size()) < 2*ncomp) { return false; }
          for(int c=0;c<ncomp;++c)
            {
              double dmin = decode_array[2*c+0];
              double dmax = decode_array[2*c+1];
              if(not (std::abs(dmin - 0.0) < 1e-12 and std::abs(dmax - 1.0) < 1e-12))
                { return false; }
            }
        }
      return true;
    }();

    if(is_jpeg_ext and filters_have_dct and (not is_safe_passthrough))
      {
        // The raw stream is already JPEG-encoded (/DCTDecode) but needs
        // /Decode correction — decompress, apply mapping, re-encode.
        jpeg::jpeg_parameters params;
        params.width = image_width;
        params.height = image_height;
        params.bits_per_component = bits_per_component;
        params.color_space = effective_cs;
        params.decode = decode_array;
        params.has_decode = decode_present and not decode_array.empty();
        params.image_mask = image_mask;

        bool ok = jpeg::write_corrected_jpeg_from_memory(
                                                         reinterpret_cast<unsigned char const*>(codec_stream_data->getBuffer()),
                                                         static_cast<std::size_t>(codec_stream_data->getSize()),
                                                         params, path);
        if(ok)
          {
            LOG_S(INFO) << "wrote corrected JPEG to " << path.string();
            return;
          }
        LOG_S(WARNING) << "JPEG correction failed, falling back to raw copy: " << path.string();
      }

    if(is_jpeg_ext and (not filters_have_dct) and has_decoded_stream_data())
      {
        // Raw pixels (e.g. /FlateDecode) — encode to JPEG from the decoded stream.
        // Future: for lossless export, encode to PNG here instead
        // (requires libpng or lodepng and a write_png_from_raw_pixels() helper).
        jpeg::jpeg_parameters params;
        params.width = image_width;
        params.height = image_height;
        params.bits_per_component = bits_per_component;
        params.color_space = effective_cs;
        params.decode = decode_array;
        params.has_decode = decode_present and not decode_array.empty();
        params.image_mask = image_mask;

        bool ok = jpeg::write_jpeg_from_raw_pixels(
                                                    reinterpret_cast<unsigned char const*>(decoded_stream_data->getBuffer()),
                                                    static_cast<std::size_t>(decoded_stream_data->getSize()),
                                                    params, path);
        if(ok)
          {
            LOG_S(INFO) << "wrote JPEG from raw pixels to " << path.string();
            return;
          }
        LOG_S(WARNING) << "JPEG encoding from raw pixels failed, falling back to raw copy: " << path.string();
      }

    std::ofstream out(path, std::ios::binary);
    if(not out)
      {
        LOG_S(ERROR) << "unable to open output file: " << path.string();
        throw std::runtime_error("unable to open output file: " + path.string());
      }

    out.write(reinterpret_cast<char const*>(codec_stream_data->getBuffer()),
              static_cast<std::streamsize>(codec_stream_data->getSize()));

    LOG_S(INFO) << "saved " << codec_stream_data->getSize()
                << " bytes to " << path.string();
  }

  std::shared_ptr<Buffer> pdf_resource<PAGE_XOBJECT_IMAGE>::load_from_file(std::filesystem::path const& path)
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
