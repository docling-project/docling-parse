//-*-C++-*-

#ifndef PDF_PAGE_SHADING_RESOURCE_H
#define PDF_PAGE_SHADING_RESOURCE_H

namespace pdflib
{

  // One /Shading resource (ISO 32000-1, 8.7.4.5).
  //
  // Only the axial (type 2) and radial (type 3) shadings are interpreted;
  // those are what ordinary documents use for gradient panels and backgrounds,
  // and both reduce to a colour ramp sampled along one parameter. Anything
  // else is reported once and skipped rather than guessed at.
  //
  // The ramp comes from the shading's /Function, evaluated through the
  // shading's own colour space, so an ICCBased space is colour-managed here
  // exactly as it is for fills.
  template<>
  class pdf_resource<PAGE_SHADING>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    void set(const std::string& key, QPDFObjectHandle obj);

    bool is_supported() const { return supported_; }
    int get_shading_type() const { return type_; }

    // Axis endpoints: (x0, y0, x1, y1) for axial, (x0, y0, r0, x1, y1, r1)
    // for radial, in the shading's own coordinate space.
    const std::vector<double>& get_coords() const { return coords_; }

    bool has_bbox() const { return has_bbox_; }
    const std::array<double, 4>& get_bbox() const { return bbox_; }

    bool extend_start() const { return extend_[0]; }
    bool extend_end() const { return extend_[1]; }

    // Colour at parametric position t, t in [0, 1] over /Domain.
    bool sample(double t, std::array<int, 3>& rgb) const;

  private:

    static std::vector<double> read_numbers(QPDFObjectHandle obj);
    static double interpolate(double x, double x0, double x1, double y0, double y1);

    std::string key_;
    bool supported_ = false;
    int type_ = 0;

    std::vector<double> coords_;
    std::vector<double> domain_;
    std::array<bool, 2> extend_ = {false, false};

    bool has_bbox_ = false;
    std::array<double, 4> bbox_ = {0.0, 0.0, 0.0, 0.0};

    pdf_resource<PAGE_COLORSPACE> colorspace_;

    // One function producing every component, or one function per component.
    std::vector<pdf_function> functions_;
  };

  pdf_resource<PAGE_SHADING>::pdf_resource()
  {}

  pdf_resource<PAGE_SHADING>::~pdf_resource()
  {}

  std::vector<double> pdf_resource<PAGE_SHADING>::read_numbers(QPDFObjectHandle obj)
  {
    std::vector<double> values;
    if(not obj.isArray()) { return values; }

    for(int i = 0; i < obj.getArrayNItems(); i++)
      {
        QPDFObjectHandle item = obj.getArrayItem(i);
        if(item.isNumber()) { values.push_back(item.getNumericValue()); }
      }
    return values;
  }

  double pdf_resource<PAGE_SHADING>::interpolate(double x, double x0, double x1,
                                                 double y0, double y1)
  {
    if(std::abs(x1 - x0) < 1e-12) { return y0; }
    return y0 + (x - x0) * (y1 - y0) / (x1 - x0);
  }

  void pdf_resource<PAGE_SHADING>::set(const std::string& key,
                                       QPDFObjectHandle obj)
  {
    key_ = key;
    supported_ = false;

    QPDFObjectHandle dict = obj.isStream() ? obj.getDict() : obj;
    if(not dict.isDictionary()) { return; }

    if(not (dict.hasKey("/ShadingType") and dict.getKey("/ShadingType").isInteger()))
      {
        return;
      }
    type_ = static_cast<int>(dict.getKey("/ShadingType").getIntValue());

    if(type_ != 2 and type_ != 3)
      {
        LOG_S(INFO) << "shading " << key_ << ": type " << type_
                    << " is not interpreted (only axial and radial are)";
        return;
      }

    if(not dict.hasKey("/Coords")) { return; }
    coords_ = read_numbers(dict.getKey("/Coords"));

    const size_t wanted = (type_ == 2) ? 4u : 6u;
    if(coords_.size() < wanted)
      {
        LOG_S(WARNING) << "shading " << key_ << ": /Coords has " << coords_.size()
                       << " entries, expected " << wanted;
        return;
      }

    domain_ = dict.hasKey("/Domain") ? read_numbers(dict.getKey("/Domain"))
                                     : std::vector<double>{0.0, 1.0};

    if(dict.hasKey("/Extend") and dict.getKey("/Extend").isArray() and
       dict.getKey("/Extend").getArrayNItems() >= 2)
      {
        QPDFObjectHandle ext = dict.getKey("/Extend");
        extend_[0] = ext.getArrayItem(0).isBool() and ext.getArrayItem(0).getBoolValue();
        extend_[1] = ext.getArrayItem(1).isBool() and ext.getArrayItem(1).getBoolValue();
      }

    if(dict.hasKey("/BBox"))
      {
        std::vector<double> box = read_numbers(dict.getKey("/BBox"));
        if(box.size() >= 4)
          {
            has_bbox_ = true;
            bbox_ = {box[0], box[1], box[2], box[3]};
          }
      }

    if(dict.hasKey("/ColorSpace"))
      {
        colorspace_.set(key_ + "/cs", dict.getKey("/ColorSpace"));
      }

    if(not dict.hasKey("/Function"))
      {
        LOG_S(WARNING) << "shading " << key_ << ": no /Function";
        return;
      }

    QPDFObjectHandle fn = dict.getKey("/Function");
    if(fn.isArray())
      {
        // One 1-out function per colour component.
        for(int i = 0; i < fn.getArrayNItems(); i++)
          {
            pdf_function f;
            if(not f.parse(fn.getArrayItem(i)))
              {
                LOG_S(WARNING) << "shading " << key_ << ": component function "
                               << i << " not interpretable";
                return;
              }
            functions_.push_back(std::move(f));
          }
      }
    else
      {
        pdf_function f;
        if(not f.parse(fn))
          {
            LOG_S(WARNING) << "shading " << key_ << ": /Function type not interpretable";
            return;
          }
        functions_.push_back(std::move(f));
      }

    supported_ = not functions_.empty();

    LOG_S(INFO) << "shading " << key_ << ": type " << type_
                << ", " << functions_.size() << " function(s), "
                << (has_bbox_ ? "with /BBox" : "no /BBox");
  }

  bool pdf_resource<PAGE_SHADING>::sample(double t, std::array<int, 3>& rgb) const
  {
    if(not supported_) { return false; }

    const double d0 = (domain_.size() >= 2) ? domain_[0] : 0.0;
    const double d1 = (domain_.size() >= 2) ? domain_[1] : 1.0;
    const double tt = d0 + t * (d1 - d0);

    std::vector<double> comps;
    if(functions_.size() == 1)
      {
        if(not functions_[0].eval(tt, comps)) { return false; }
      }
    else
      {
        for(const auto& f : functions_)
          {
            std::vector<double> one;
            if(not f.eval(tt, one) or one.empty()) { return false; }
            comps.push_back(one[0]);
          }
      }

    if(colorspace_.map_to_rgb(comps, rgb)) { return true; }

    // No usable colour space: fall back on the component count alone.
    if(comps.size() == 1)
      {
        const int g = static_cast<int>(std::lround(255.0 * std::max(0.0, std::min(1.0, comps[0]))));
        rgb = {g, g, g};
        return true;
      }
    if(comps.size() >= 3)
      {
        rgb = {static_cast<int>(std::lround(255.0 * std::max(0.0, std::min(1.0, comps[0])))),
               static_cast<int>(std::lround(255.0 * std::max(0.0, std::min(1.0, comps[1])))),
               static_cast<int>(std::lround(255.0 * std::max(0.0, std::min(1.0, comps[2]))))};
        return true;
      }
    return false;
  }

}

#endif
