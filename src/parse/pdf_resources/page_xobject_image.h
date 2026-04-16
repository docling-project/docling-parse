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
    int                      get_icc_components() const;
    int                      get_indexed_hival() const;
    std::string              get_indexed_base_cs() const;
    std::shared_ptr<std::vector<uint8_t>> get_indexed_palette() const;
    std::string              get_intent() const;
    std::vector<std::string> get_filters() const;

    // Optional PDF semantics for images
    bool                     has_decode_array() const;
    std::vector<double>      get_decode_array() const;
    bool                     is_image_mask() const;

    // /CCITTFaxDecode parameters (from /DecodeParms)
    int                      get_ccitt_k() const;
    bool                     get_ccitt_black_is_1() const;
    bool                     has_jbig2_globals_data() const;
    std::shared_ptr<Buffer>  get_jbig2_globals_data() const;

    bool                     has_raw_stream_data() const;
    std::shared_ptr<Buffer>  get_raw_stream_data() const;

    bool                     has_decoded_stream_data() const;
    std::shared_ptr<Buffer>  get_decoded_stream_data() const;
    bool                     has_soft_mask_data() const;
    std::shared_ptr<std::vector<uint8_t>> get_soft_mask_data() const;

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
    void init_soft_mask_data();

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
    int              indexed_hival  = -1; // hival from /Indexed color space; -1 if not Indexed
    std::string      indexed_base_cs;    // base color space name for /Indexed (e.g. "/DeviceRGB")
    std::shared_ptr<std::vector<uint8_t>> indexed_palette; // raw palette bytes: (hival+1)*ncomps bytes
    std::string      intent;
    std::vector<std::string> image_filters;

    // Stream data
    std::shared_ptr<Buffer> raw_stream_data;
    std::shared_ptr<Buffer> decoded_stream_data;
    std::shared_ptr<std::vector<uint8_t>> soft_mask_data;

    // PDF image semantics
    std::vector<double> decode_array; // length 2*ncomp when present
    bool decode_present = false;
    bool image_mask = false;

    // /CCITTFaxDecode parameters from /DecodeParms
    int  ccitt_k          = 0;     // /K default per PDF spec: 0=Group3-1D, <0=Group4, >0=Group3-mixed
    bool ccitt_black_is_1 = false; // /BlackIs1: true means 1-bit=black
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
    decoded_stream_data(nullptr),
    soft_mask_data(nullptr),
    jbig2_globals_data(nullptr)
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
    init_stream_data();
    init_soft_mask_data();
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
                          }
                      }
                    else
                      {
                        LOG_S(WARNING) << "ICCBased: second array element is not a stream";
                      }
                  }
                else if(name_obj.isName() and name_obj.getName() == "/Indexed"
                        and qpdf_cs.getArrayNItems() >= 3)
                  {
                    // [/Indexed, base, hival, lookup]

                    // base color space
                    auto base_obj = qpdf_cs.getArrayItem(1);
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
                                auto icc_dict = icc_stream.getDict();
                                if(icc_dict.hasKey("/N") and icc_dict.getKey("/N").isInteger())
                                  {
                                    const int n = icc_dict.getKey("/N").getIntValue();
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
                                  }
                              }
                            else
                              {
                                LOG_S(WARNING) << "Indexed ICCBased base: second array element is not a stream";
                              }
                          }
                        else if(base_name.isName())
                          {
                            indexed_base_cs = base_name.getName();
                            LOG_S(INFO) << "Indexed array base color space: " << indexed_base_cs;
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
	    LOG_S(WARNING) << "no `/Decode` found: falling back on default for " << color_space;
	    decode_array = {
	      1, 0, 1, 0,
	      1, 0, 1, 0
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
	else if(indexed_hival >= 0)
	  {
	    // Indexed: default decode is [0, hival] (one component — the palette index)
	    LOG_S(INFO) << "no `/Decode` found: using [0, " << indexed_hival << "] for Indexed color space";
	    decode_array = { 0.0, static_cast<double>(indexed_hival) };
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
                ccitt_k = parms["/K"].get<int>();
              }
            if(parms.count("/BlackIs1") and parms["/BlackIs1"].is_boolean())
              {
                ccitt_black_is_1 = parms["/BlackIs1"].get<bool>();
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
  }

  void pdf_resource<PAGE_XOBJECT_IMAGE>::init_soft_mask_data()
  {
    soft_mask_data.reset();

    if(not qpdf_xobject_dict.hasKey("/SMask"))
      {
        return;
      }

    auto qpdf_smask = qpdf_xobject_dict.getKey("/SMask");
    if(not qpdf_smask.isStream())
      {
        LOG_S(WARNING) << "SMask present but is not a stream for xobject_key=" << xobject_key;
        return;
      }

    pdf_resource<PAGE_XOBJECT_IMAGE> smask;
    smask.set(xobject_key + "/SMask", qpdf_smask);

    if(smask.get_image_width() != image_width or smask.get_image_height() != image_height)
      {
        LOG_S(WARNING) << "SMask size mismatch for xobject_key=" << xobject_key
                       << " image=" << image_width << "x" << image_height
                       << " smask=" << smask.get_image_width() << "x" << smask.get_image_height();
        return;
      }

    const bool gray_mask =
      smask.get_color_space() == "/DeviceGray"
      or (smask.get_color_space().find("/ICCBased") != std::string::npos
          and smask.get_icc_components() == 1);
    if(not gray_mask)
      {
        LOG_S(WARNING) << "SMask color space unsupported for xobject_key=" << xobject_key
                       << " smask_cs=" << smask.get_color_space()
                       << " smask_icc_components=" << smask.get_icc_components();
        return;
      }

    if(smask.get_bits_per_component() != 8)
      {
        LOG_S(WARNING) << "SMask bits/component unsupported for xobject_key=" << xobject_key
                       << " smask_bpc=" << smask.get_bits_per_component();
        return;
      }

    if(not smask.has_decoded_stream_data())
      {
        LOG_S(WARNING) << "SMask has no decoded stream data for xobject_key=" << xobject_key;
        return;
      }

    auto smask_buf = smask.get_decoded_stream_data();
    const size_t expected = static_cast<size_t>(image_width) * image_height;
    if(smask_buf->getSize() < expected)
      {
        LOG_S(WARNING) << "SMask decoded stream too small for xobject_key=" << xobject_key
                       << " size=" << smask_buf->getSize()
                       << " expected>=" << expected;
        return;
      }

    auto out = std::make_shared<std::vector<uint8_t>>();
    out->resize(expected);

    auto const* src = reinterpret_cast<uint8_t const*>(smask_buf->getBuffer());
    auto const decode = smask.get_decode_array();
    const bool has_decode = smask.has_decode_array() and decode.size() >= 2;
    for(size_t i = 0; i < expected; ++i)
      {
        uint8_t alpha = src[i];
        if(has_decode)
          {
            alpha = jpeg::apply_decode_component(alpha, decode[0], decode[1]);
          }
        (*out)[i] = alpha;
      }

    soft_mask_data = std::move(out);

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

  int pdf_resource<PAGE_XOBJECT_IMAGE>::get_ccitt_k() const
  {
    return ccitt_k;
  }

  bool pdf_resource<PAGE_XOBJECT_IMAGE>::get_ccitt_black_is_1() const
  {
    return ccitt_black_is_1;
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
    if(not has_raw_stream_data())
      {
        LOG_S(WARNING) << "no raw stream data to save";
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

    out.write(reinterpret_cast<char const*>(raw_stream_data->getBuffer()),
              static_cast<std::streamsize>(raw_stream_data->getSize()));

    LOG_S(INFO) << "saved " << raw_stream_data->getSize()
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
