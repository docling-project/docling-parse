//-*-C++-*-

#ifndef PDF_PAGE_COLORSPACE_RESOURCE_H
#define PDF_PAGE_COLORSPACE_RESOURCE_H

namespace pdflib
{

  // One resolved /ColorSpace resource entry (8.6). The goal is not full
  // colorimetric accuracy but a faithful RGB approximation for rendering:
  // ICCBased spaces map through their component count (/N), Indexed spaces
  // through their palette, and Separation/DeviceN tints through their tint
  // transform into their alternate space.
  template<>
  class pdf_resource<PAGE_COLORSPACE>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    void set(const std::string& key, QPDFObjectHandle qpdf_obj);

    color_space_family get_family() const;
    int get_num_components() const;

    // True when this is a /Separation or /DeviceN whose tint transform and
    // alternate space both resolved, so map_to_rgb() gives the real colour
    // rather than the darkening approximation. Callers that convert whole
    // images use it to decide whether going through the transform is worth it.
    bool has_tint_transform() const;

    // Number of tint components a Separation/DeviceN colour takes: one per
    // colorant.
    int tint_component_count() const
    {
      return static_cast<int>(colorant_names_.size());
    }

    // Maps the numeric SC/SCN/sc/scn operands to RGB; returns false when
    // the space cannot interpret them (pattern, unknown family, wrong
    // operand count), in which case the caller keeps its own fallback.
    bool map_to_rgb(const std::vector<double>& comps,
                    std::array<int, 3>& rgb) const;

  private:

    void parse(QPDFObjectHandle obj, int depth);
    void parse_name(const std::string& name);
    void parse_lookup(QPDFObjectHandle obj);

    // Resolves the alternate space and the tint transform of a /Separation or
    // /DeviceN array, whose layout differs only in where those two entries sit.
    void parse_tint_transform(QPDFObjectHandle obj,
                              int alternate_index,
                              int function_index,
                              int depth);

    // The fallback for a tint that cannot go through its tint transform:
    // darken towards black with the strongest tint present.
    std::array<int, 3> approximate_tint(const std::vector<double>& comps) const;

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

    // Separation / DeviceN only: the colorant names, the space the tints are
    // painted in, and the function that maps the tints onto it. The pair is
    // set together or not at all, so either both are usable or neither is.
    std::vector<std::string> colorant_names_;
    std::shared_ptr<pdf_resource<PAGE_COLORSPACE>> alternate_;
    std::shared_ptr<pdf_function> tint_transform_;
  };

  pdf_resource<PAGE_COLORSPACE>::pdf_resource():
    key_(""),
    family_(COLOR_SPACE_UNKNOWN),
    num_components_(0),
    base_(nullptr),
    hival_(0),
    lookup_({}),
    colorant_names_({}),
    alternate_(nullptr),
    tint_transform_(nullptr)
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

  bool pdf_resource<PAGE_COLORSPACE>::has_tint_transform() const
  {
    return tint_transform_ != nullptr and alternate_ != nullptr;
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
    else if(name == "/Separation" and obj.getArrayNItems() >= 2)
      {
        // [/Separation name alternateSpace tintTransform] (8.6.6.4)
        family_ = COLOR_SPACE_SEPARATION;
        num_components_ = 1;

        QPDFObjectHandle colorant = obj.getArrayItem(1);
        colorant_names_.push_back(colorant.isName() ? colorant.getName() : "");

        parse_tint_transform(obj, 2, 3, depth);
      }
    else if(name == "/DeviceN" and obj.getArrayNItems() >= 2 and
            obj.getArrayItem(1).isArray())
      {
        // [/DeviceN names alternateSpace tintTransform attributes] (8.6.6.5)
        QPDFObjectHandle names = obj.getArrayItem(1);

        const int n = names.getArrayNItems();
        if(n < 1)
          {
            LOG_S(WARNING) << "colorspace " << key_
                           << ": /DeviceN has an empty colorant array";
            return;
          }

        family_ = COLOR_SPACE_DEVICE_N;
        num_components_ = n;

        for(int i = 0; i < n; i++)
          {
            QPDFObjectHandle colorant = names.getArrayItem(i);
            colorant_names_.push_back(colorant.isName() ? colorant.getName() : "");
          }

        parse_tint_transform(obj, 2, 3, depth);
      }
    else if(name == "/Pattern")
      {
        family_ = COLOR_SPACE_PATTERN;
        if(obj.getArrayNItems() >= 2)
          {
            base_ = std::make_shared<pdf_resource<PAGE_COLORSPACE>>();
            base_->key_ = key_ + "/base";
            base_->parse(obj.getArrayItem(1), depth + 1);
            num_components_ = base_->get_num_components();
          }
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

  void pdf_resource<PAGE_COLORSPACE>::parse_tint_transform(QPDFObjectHandle obj,
                                                           int alternate_index,
                                                           int function_index,
                                                           int depth)
  {
    // Both entries are required, but a tint can still be approximated without
    // them, so a missing or unusable pair is a warning and not a parse failure.
    if(obj.getArrayNItems() <= function_index)
      {
        LOG_S(WARNING) << "colorspace " << key_
                       << ": no alternate space and tint transform";
        return;
      }

    auto alternate = std::make_shared<pdf_resource<PAGE_COLORSPACE>>();
    alternate->key_ = key_ + "/alternate";
    alternate->parse(obj.getArrayItem(alternate_index), depth + 1);

    // 8.6.6.4: the alternate space is any device or CIE-based space, never a
    // special one, so anything that cannot map its own components is unusable.
    if(alternate->get_num_components() <= 0)
      {
        LOG_S(WARNING) << "colorspace " << key_
                       << ": alternate space could not be resolved";
        return;
      }

    auto tint_transform = std::make_shared<pdf_function>();
    if(not tint_transform->set(obj.getArrayItem(function_index)))
      {
        LOG_S(WARNING) << "colorspace " << key_ << ": tint transform: "
                       << tint_transform->get_reason();
        return;
      }

    if(tint_transform->get_num_inputs() != num_components_)
      {
        LOG_S(WARNING) << "colorspace " << key_ << ": tint transform takes "
                       << tint_transform->get_num_inputs() << " inputs but the "
                       << "space has " << num_components_ << " colorant(s)";
        return;
      }

    if(tint_transform->get_num_outputs() != alternate->get_num_components())
      {
        LOG_S(WARNING) << "colorspace " << key_ << ": tint transform has "
                       << tint_transform->get_num_outputs() << " outputs but the "
                       << "alternate space needs "
                       << alternate->get_num_components();
        return;
      }

    alternate_ = alternate;
    tint_transform_ = tint_transform;
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
    return color::cmyk_to_rgb255(c, m, y, k);
  }

  std::array<int, 3> pdf_resource<PAGE_COLORSPACE>::approximate_tint(
    const std::vector<double>& comps) const
  {
    // tint 0 = no ink (white), tint 1 = full ink (dark). A /None colorant
    // makes no marks at all (8.6.6.4), which this RGB-only interface cannot
    // express, so it is left out of the tint rather than painted.
    double tint = 0.0;
    for(std::size_t i = 0; i < comps.size(); i++)
      {
        if(i < colorant_names_.size() and colorant_names_[i] == "/None")
          {
            continue;
          }

        tint = std::max(tint, clamp_01(comps[i]));
      }

    return gray_to_rgb(1.0 - tint);
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
      case COLOR_SPACE_PATTERN:
        {
          if(base_ == nullptr) { return false; }
          return base_->map_to_rgb(comps, rgb);
        }
      case COLOR_SPACE_SEPARATION:
      case COLOR_SPACE_DEVICE_N:
        {
          if(comps.size() != static_cast<std::size_t>(num_components_))
            {
              return false;
            }

          // 8.6.6.4: a /None colorant makes no marks, so a space whose
          // colorants are all /None must not run its tint transform. What it
          // should do instead is paint nothing, which this RGB-only interface
          // cannot express, so it reports no ink (white).
          bool marks_nothing = not colorant_names_.empty();
          for(const std::string& colorant : colorant_names_)
            {
              marks_nothing = marks_nothing and colorant == "/None";
            }

          // 8.6.6.4 / 8.6.6.5: what gets painted is the alternate space, so
          // the tints have to go through the tint transform to become colour.
          if(not marks_nothing and
             tint_transform_ != nullptr and alternate_ != nullptr)
            {
              std::vector<double> alternate_comps;
              if(tint_transform_->evaluate(comps, alternate_comps) and
                 alternate_->map_to_rgb(alternate_comps, rgb))
                {
                  return true;
                }

              LOG_S(INFO) << "colorspace " << key_
                          << ": tint transform did not evaluate, approximating";
            }

          rgb = approximate_tint(comps);
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
