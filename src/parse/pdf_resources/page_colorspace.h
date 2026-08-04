//-*-C++-*-

#ifndef PDF_PAGE_COLORSPACE_RESOURCE_H
#define PDF_PAGE_COLORSPACE_RESOURCE_H

namespace pdflib
{

  // One resolved /ColorSpace resource entry (8.6). The goal is not full
  // colorimetric accuracy but a faithful RGB approximation for rendering:
  // ICCBased spaces map through their component count (/N), Indexed spaces
  // through their palette, and Separation/DeviceN tints darken towards
  // black (the tint transform function is not evaluated).
  template<>
  class pdf_resource<PAGE_COLORSPACE>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    void set(const std::string& key, QPDFObjectHandle qpdf_obj);

    color_space_family get_family() const;
    int get_num_components() const;

    // Maps the numeric SC/SCN/sc/scn operands to RGB; returns false when
    // the space cannot interpret them (pattern, unknown family, wrong
    // operand count), in which case the caller keeps its own fallback.
    bool map_to_rgb(const std::vector<double>& comps,
                    std::array<int, 3>& rgb) const;

  private:

    void parse(QPDFObjectHandle obj, int depth);
    void parse_name(const std::string& name);
    void parse_lookup(QPDFObjectHandle obj);

    static double clamp_01(double val);
    static std::array<int, 3> gray_to_rgb(double gray);
    static std::array<int, 3> cmyk_to_rgb(double c, double m, double y, double k);

  private:

    std::string key_;

    color_space_family family_;
    int num_components_;

    // Indexed only: the resolved base space and the palette bytes.
    std::shared_ptr<pdf_resource<PAGE_COLORSPACE>> base_;
    int hival_;
    std::vector<uint8_t> lookup_;
  };

  pdf_resource<PAGE_COLORSPACE>::pdf_resource():
    key_(""),
    family_(COLOR_SPACE_UNKNOWN),
    num_components_(0),
    base_(nullptr),
    hival_(0),
    lookup_({})
  {}

  pdf_resource<PAGE_COLORSPACE>::~pdf_resource()
  {}

  color_space_family pdf_resource<PAGE_COLORSPACE>::get_family() const
  {
    return family_;
  }

  int pdf_resource<PAGE_COLORSPACE>::get_num_components() const
  {
    return num_components_;
  }

  void pdf_resource<PAGE_COLORSPACE>::set(const std::string& key,
                                          QPDFObjectHandle qpdf_obj)
  {
    key_ = key;

    try
      {
        parse(qpdf_obj, 0);
      }
    catch(const std::exception& e)
      {
        LOG_S(WARNING) << "colorspace " << key_ << ": parse failed: " << e.what();
        family_ = COLOR_SPACE_UNKNOWN;
      }

    LOG_S(INFO) << "colorspace " << key_ << ": family=" << family_
                << " num-components=" << num_components_;
  }

  void pdf_resource<PAGE_COLORSPACE>::parse(QPDFObjectHandle obj, int depth)
  {
    if(depth > 8)
      {
        LOG_S(WARNING) << "colorspace " << key_ << ": nesting too deep";
        return;
      }

    if(obj.isName())
      {
        parse_name(obj.getName());
        return;
      }

    if(not obj.isArray() or obj.getArrayNItems() == 0)
      {
        LOG_S(WARNING) << "colorspace " << key_
                       << ": neither a name nor a non-empty array";
        return;
      }

    QPDFObjectHandle head = obj.getArrayItem(0);
    std::string name = head.isName() ? head.getName() : "";

    if(name == "/ICCBased" and obj.getArrayNItems() >= 2)
      {
        QPDFObjectHandle icc = obj.getArrayItem(1);

        int n = 0;
        if(icc.isStream())
          {
            QPDFObjectHandle icc_dict = icc.getDict();
            if(icc_dict.hasKey("/N") and icc_dict.getKey("/N").isInteger())
              {
                n = static_cast<int>(icc_dict.getKey("/N").getIntValue());
              }
          }

        switch(n)
          {
          case 1: family_ = COLOR_SPACE_GRAY; break;
          case 3: family_ = COLOR_SPACE_RGB;  break;
          case 4: family_ = COLOR_SPACE_CMYK; break;
          default:
            LOG_S(WARNING) << "colorspace " << key_
                           << ": ICCBased with unsupported /N " << n;
            return;
          }
        num_components_ = n;
      }
    else if(name == "/CalGray")
      {
        family_ = COLOR_SPACE_GRAY;
        num_components_ = 1;
      }
    else if(name == "/CalRGB")
      {
        family_ = COLOR_SPACE_RGB;
        num_components_ = 3;
      }
    else if(name == "/Lab")
      {
        family_ = COLOR_SPACE_LAB;
        num_components_ = 3;
      }
    else if(name == "/Indexed" and obj.getArrayNItems() >= 4)
      {
        base_ = std::make_shared<pdf_resource<PAGE_COLORSPACE>>();
        base_->key_ = key_ + "/base";
        base_->parse(obj.getArrayItem(1), depth + 1);

        QPDFObjectHandle hival = obj.getArrayItem(2);
        hival_ = hival.isInteger() ? static_cast<int>(hival.getIntValue()) : 0;

        parse_lookup(obj.getArrayItem(3));

        family_ = COLOR_SPACE_INDEXED;
        num_components_ = 1;
      }
    else if(name == "/Separation")
      {
        family_ = COLOR_SPACE_SEPARATION;
        num_components_ = 1;
      }
    else if(name == "/DeviceN" and obj.getArrayNItems() >= 2 and
            obj.getArrayItem(1).isArray())
      {
        family_ = COLOR_SPACE_DEVICE_N;
        num_components_ = obj.getArrayItem(1).getArrayNItems();
      }
    else if(name == "/Pattern")
      {
        family_ = COLOR_SPACE_PATTERN;
      }
    else
      {
        LOG_S(WARNING) << "colorspace " << key_
                       << ": unsupported family " << name;
      }
  }

  void pdf_resource<PAGE_COLORSPACE>::parse_name(const std::string& name)
  {
    if(name == "/DeviceGray" or name == "/CalGray" or name == "/G")
      {
        family_ = COLOR_SPACE_GRAY;
        num_components_ = 1;
      }
    else if(name == "/DeviceRGB" or name == "/CalRGB" or name == "/RGB")
      {
        family_ = COLOR_SPACE_RGB;
        num_components_ = 3;
      }
    else if(name == "/DeviceCMYK" or name == "/CMYK")
      {
        family_ = COLOR_SPACE_CMYK;
        num_components_ = 4;
      }
    else if(name == "/Pattern")
      {
        family_ = COLOR_SPACE_PATTERN;
      }
    else
      {
        LOG_S(WARNING) << "colorspace " << key_
                       << ": unsupported name " << name;
      }
  }

  void pdf_resource<PAGE_COLORSPACE>::parse_lookup(QPDFObjectHandle obj)
  {
    if(obj.isString())
      {
        const std::string bytes = obj.getStringValue();
        lookup_.assign(bytes.begin(), bytes.end());
      }
    else if(obj.isStream())
      {
        auto buffer = obj.getStreamData(qpdf_dl_generalized);
        const unsigned char* data = buffer->getBuffer();
        lookup_.assign(data, data + buffer->getSize());
      }
    else
      {
        LOG_S(WARNING) << "colorspace " << key_
                       << ": /Indexed lookup is neither string nor stream";
      }
  }

  double pdf_resource<PAGE_COLORSPACE>::clamp_01(double val)
  {
    return std::min(1.0, std::max(0.0, val));
  }

  std::array<int, 3> pdf_resource<PAGE_COLORSPACE>::gray_to_rgb(double gray)
  {
    int v = static_cast<int>(std::round(255.0 * clamp_01(gray)));
    return {v, v, v};
  }

  std::array<int, 3> pdf_resource<PAGE_COLORSPACE>::cmyk_to_rgb(double c, double m,
                                                                double y, double k)
  {
    int r = static_cast<int>(std::round(255.0 * (1.0 - clamp_01(c)) * (1.0 - clamp_01(k))));
    int g = static_cast<int>(std::round(255.0 * (1.0 - clamp_01(m)) * (1.0 - clamp_01(k))));
    int b = static_cast<int>(std::round(255.0 * (1.0 - clamp_01(y)) * (1.0 - clamp_01(k))));
    return {r, g, b};
  }

  bool pdf_resource<PAGE_COLORSPACE>::map_to_rgb(const std::vector<double>& comps,
                                                 std::array<int, 3>& rgb) const
  {
    switch(family_)
      {
      case COLOR_SPACE_GRAY:
        {
          if(comps.size() != 1) { return false; }

          rgb = gray_to_rgb(comps[0]);
          return true;
        }
      case COLOR_SPACE_RGB:
        {
          if(comps.size() != 3) { return false; }

          rgb = {static_cast<int>(std::round(255.0 * clamp_01(comps[0]))),
                 static_cast<int>(std::round(255.0 * clamp_01(comps[1]))),
                 static_cast<int>(std::round(255.0 * clamp_01(comps[2])))};
          return true;
        }
      case COLOR_SPACE_CMYK:
        {
          if(comps.size() != 4) { return false; }

          rgb = cmyk_to_rgb(comps[0], comps[1], comps[2], comps[3]);
          return true;
        }
      case COLOR_SPACE_LAB:
        {
          if(comps.size() != 3) { return false; }

          // approximate by lightness only: L* in [0, 100]
          rgb = gray_to_rgb(comps[0] / 100.0);
          return true;
        }
      case COLOR_SPACE_INDEXED:
        {
          if(comps.size() != 1 or base_ == nullptr) { return false; }

          const int n = base_->get_num_components();
          if(n <= 0) { return false; }

          int index = static_cast<int>(std::round(comps[0]));
          index = std::min(hival_, std::max(0, index));

          const std::size_t offset = static_cast<std::size_t>(index) * n;
          if(offset + n > lookup_.size()) { return false; }

          std::vector<double> base_comps(n, 0.0);
          for(int d = 0; d < n; d++)
            {
              base_comps[d] = lookup_[offset + d] / 255.0;
            }

          // Lab palette entries encode L* in [0, 100], not [0, 1]
          if(base_->get_family() == COLOR_SPACE_LAB)
            {
              base_comps[0] *= 100.0;
            }

          return base_->map_to_rgb(base_comps, rgb);
        }
      case COLOR_SPACE_SEPARATION:
        {
          if(comps.size() != 1) { return false; }

          // tint 0 = no ink (white), tint 1 = full ink (dark)
          rgb = gray_to_rgb(1.0 - clamp_01(comps[0]));
          return true;
        }
      case COLOR_SPACE_DEVICE_N:
        {
          if(comps.empty()) { return false; }

          double tint = 0.0;
          for(double comp : comps)
            {
              tint = std::max(tint, clamp_01(comp));
            }

          rgb = gray_to_rgb(1.0 - tint);
          return true;
        }
      default:
        {
          return false;
        }
      }
  }

}

#endif
