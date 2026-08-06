//-*-C++-*-

#ifndef PDF_PAGE_SHADING_RESOURCE_H
#define PDF_PAGE_SHADING_RESOURCE_H

namespace pdflib
{

  // One resolved /Shading resource entry (8.7.4.5). A shading is painted by
  // the `sh` operator (and, indirectly, by shading patterns): its colours come
  // from its own colour space and function, never from the current fill
  // colour.
  //
  // Axial (/ShadingType 2) and radial (/ShadingType 3) shadings are decoded
  // into a colour ramp of `num_stops` samples that the renderer turns into a
  // gradient. The other shading types are recognised but not painted; they
  // report themselves through get_reason() so the omission is visible.
  template<>
  class pdf_resource<PAGE_SHADING>
  {
    // The colour ramp is resampled at a fixed resolution instead of being
    // evaluated per pixel. 256 steps is the granularity a device-RGB ramp can
    // resolve anyway, and the renderer interpolates linearly in between.
    const static inline std::size_t NUM_STOPS = 256;

  public:

    pdf_resource();
    ~pdf_resource();

    void set(const std::string& key, QPDFObjectHandle qpdf_obj);

    const std::string& get_key() const { return key_; }

    shading_type_name get_shading_type() const { return shading_type_; }

    // True when this shading can be painted. When false, get_reason() says why.
    bool is_paintable() const { return paintable_; }
    const std::string& get_reason() const { return reason_; }

    // Axial: [x0, y0, x1, y1]. Radial: [x0, y0, r0, x1, y1, r1].
    const std::vector<double>& get_coords() const { return coords_; }

    bool get_extend_start() const { return extend_start_; }
    bool get_extend_end() const { return extend_end_; }

    bool has_bbox() const { return has_bbox_; }
    const std::array<double, 4>& get_bbox() const { return bbox_; }

    // The colour ramp, sampled across /Domain and mapped through the
    // shading's colour space. Offsets run from 0.0 to 1.0 and correspond to
    // the axis endpoints, so /Domain is already folded in.
    const std::vector<shading_stop>& get_stops() const { return stops_; }

  private:

    void parse(QPDFObjectHandle dict);

    bool parse_functions(QPDFObjectHandle dict);
    bool parse_coords(QPDFObjectHandle dict, std::size_t expected);

    // Evaluates the function(s) at `t` and maps the result through the colour
    // space.
    bool evaluate_color(double t, std::array<int, 3>& rgb) const;

    bool build_stops();

    void reject(const std::string& reason);

  private:

    std::string key_;

    shading_type_name shading_type_;

    bool        paintable_;
    std::string reason_;

    pdf_resource<PAGE_COLORSPACE> colorspace_;

    // Either one function with n outputs, or n functions with one output each
    // (Table 78).
    std::vector<std::shared_ptr<pdf_function>> functions_;

    std::vector<double> coords_;

    std::array<double, 2> domain_;

    bool extend_start_;
    bool extend_end_;

    bool                  has_bbox_;
    std::array<double, 4> bbox_;

    std::vector<shading_stop> stops_;
  };

  pdf_resource<PAGE_SHADING>::pdf_resource():
    key_(""),
    shading_type_(SHADING_UNKNOWN),
    paintable_(false),
    reason_("not decoded"),
    colorspace_(),
    functions_({}),
    coords_({}),
    domain_({0.0, 1.0}),
    extend_start_(false),
    extend_end_(false),
    has_bbox_(false),
    bbox_({0.0, 0.0, 0.0, 0.0}),
    stops_({})
  {}

  pdf_resource<PAGE_SHADING>::~pdf_resource()
  {}

  void pdf_resource<PAGE_SHADING>::reject(const std::string& reason)
  {
    paintable_ = false;
    reason_ = reason;
  }

  void pdf_resource<PAGE_SHADING>::set(const std::string& key,
                                       QPDFObjectHandle qpdf_obj)
  {
    key_ = key;

    try
      {
        // types 4-7 are streams; types 1-3 are plain dictionaries
        parse(qpdf_obj.isStream() ? qpdf_obj.getDict() : qpdf_obj);
      }
    catch(const std::exception& e)
      {
        reject(std::string("parse failed: ") + e.what());
      }

    if(paintable_)
      {
        LOG_S(INFO) << "shading " << key_ << ": " << to_string(shading_type_)
                    << ", #-stops=" << stops_.size()
                    << ", extend=[" << (extend_start_ ? "true" : "false")
                    << ", " << (extend_end_ ? "true" : "false") << "]"
                    << ", bbox=" << (has_bbox_ ? "yes" : "no");
      }
    else
      {
        LOG_S(WARNING) << "shading " << key_ << " is not paintable: " << reason_;
      }
  }

  void pdf_resource<PAGE_SHADING>::parse(QPDFObjectHandle dict)
  {
    if(not dict.isDictionary())
      {
        reject("shading object is neither a dictionary nor a stream");
        return;
      }

    if(not dict.hasKey("/ShadingType") or not dict.getKey("/ShadingType").isInteger())
      {
        reject("shading has no integer /ShadingType");
        return;
      }

    shading_type_ =
      to_shading_type_name(static_cast<int>(dict.getKey("/ShadingType").getIntValue()));

    if(shading_type_ != SHADING_AXIAL and shading_type_ != SHADING_RADIAL)
      {
        reject("unsupported /ShadingType: " + to_string(shading_type_));
        return;
      }

    if(not dict.hasKey("/ColorSpace"))
      {
        reject("shading has no /ColorSpace");
        return;
      }
    colorspace_.set(key_ + "/ColorSpace", dict.getKey("/ColorSpace"));

    if(colorspace_.get_family() == COLOR_SPACE_UNKNOWN or
       colorspace_.get_family() == COLOR_SPACE_PATTERN)
      {
        reject("shading has an unsupported /ColorSpace");
        return;
      }

    if(not parse_functions(dict))
      {
        return;
      }

    // /Coords is required for both axial and radial shadings (Tables 79, 80)
    if(not parse_coords(dict, shading_type_ == SHADING_AXIAL ? 4 : 6))
      {
        return;
      }

    if(dict.hasKey("/Domain"))
      {
        QPDFObjectHandle qpdf_domain = dict.getKey("/Domain");
        if(qpdf_domain.isArray() and qpdf_domain.getArrayNItems() >= 2)
          {
            domain_[0] = qpdf_domain.getArrayItem(0).getNumericValue();
            domain_[1] = qpdf_domain.getArrayItem(1).getNumericValue();
          }
      }

    if(dict.hasKey("/Extend"))
      {
        QPDFObjectHandle qpdf_extend = dict.getKey("/Extend");
        if(qpdf_extend.isArray() and qpdf_extend.getArrayNItems() >= 2)
          {
            extend_start_ = qpdf_extend.getArrayItem(0).isBool() and
                            qpdf_extend.getArrayItem(0).getBoolValue();
            extend_end_   = qpdf_extend.getArrayItem(1).isBool() and
                            qpdf_extend.getArrayItem(1).getBoolValue();
          }
      }

    if(dict.hasKey("/BBox"))
      {
        QPDFObjectHandle qpdf_bbox = dict.getKey("/BBox");
        if(qpdf_bbox.isArray() and qpdf_bbox.getArrayNItems() >= 4)
          {
            for(int d = 0; d < 4; d++)
              {
                bbox_[static_cast<std::size_t>(d)] =
                  qpdf_bbox.getArrayItem(d).getNumericValue();
              }
            has_bbox_ = true;
          }
      }

    // /Background is deliberately not read: 8.7.4.3 says it shall be ignored
    // by the sh operator, which is the only caller here.

    if(not build_stops())
      {
        return;
      }

    paintable_ = true;
    reason_ = "";
  }

  bool pdf_resource<PAGE_SHADING>::parse_functions(QPDFObjectHandle dict)
  {
    if(not dict.hasKey("/Function"))
      {
        reject("axial/radial shading has no /Function");
        return false;
      }

    QPDFObjectHandle qpdf_function = dict.getKey("/Function");

    std::vector<QPDFObjectHandle> objects;
    if(qpdf_function.isArray())
      {
        for(int i = 0; i < qpdf_function.getArrayNItems(); i++)
          {
            objects.push_back(qpdf_function.getArrayItem(i));
          }
      }
    else
      {
        objects.push_back(qpdf_function);
      }

    if(objects.empty())
      {
        reject("shading has an empty /Function array");
        return false;
      }

    for(std::size_t i = 0; i < objects.size(); i++)
      {
        auto function = std::make_shared<pdf_function>();
        if(not function->set(objects[i]))
          {
            reject("/Function[" + std::to_string(i) + "]: " + function->get_reason());
            return false;
          }

        // An axial or radial shading parameterises its colour by a single
        // value t (8.7.4.5.3), so anything with more inputs belongs to a
        // shading type this class does not paint.
        if(function->get_num_inputs() != 1)
          {
            reject("/Function[" + std::to_string(i) + "] takes " +
                   std::to_string(function->get_num_inputs()) +
                   " inputs instead of 1");
            return false;
          }

        functions_.push_back(function);
      }

    const int num_components = colorspace_.get_num_components();

    if(functions_.size() == 1)
      {
        if(functions_.front()->get_num_outputs() != num_components)
          {
            reject("/Function has " +
                   std::to_string(functions_.front()->get_num_outputs()) +
                   " outputs but the colour space needs " +
                   std::to_string(num_components));
            return false;
          }
      }
    else if(static_cast<int>(functions_.size()) != num_components)
      {
        reject("/Function array holds " + std::to_string(functions_.size()) +
               " functions but the colour space needs " +
               std::to_string(num_components));
        return false;
      }

    return true;
  }

  bool pdf_resource<PAGE_SHADING>::parse_coords(QPDFObjectHandle dict,
                                                std::size_t expected)
  {
    if(not dict.hasKey("/Coords"))
      {
        reject("shading has no /Coords");
        return false;
      }

    QPDFObjectHandle qpdf_coords = dict.getKey("/Coords");

    if(not qpdf_coords.isArray() or
       static_cast<std::size_t>(qpdf_coords.getArrayNItems()) < expected)
      {
        reject("shading /Coords does not hold " + std::to_string(expected) +
               " numbers");
        return false;
      }

    for(std::size_t d = 0; d < expected; d++)
      {
        coords_.push_back(qpdf_coords.getArrayItem(static_cast<int>(d)).getNumericValue());
      }

    return true;
  }

  bool pdf_resource<PAGE_SHADING>::evaluate_color(double t,
                                                  std::array<int, 3>& rgb) const
  {
    std::vector<double> components;

    if(functions_.size() == 1)
      {
        if(not functions_.front()->evaluate(t, components))
          {
            return false;
          }
      }
    else
      {
        // n one-output functions, one per colour component
        components.reserve(functions_.size());
        for(const auto& function : functions_)
          {
            std::vector<double> single;
            if(not function->evaluate(t, single) or single.empty())
              {
                return false;
              }
            components.push_back(single.front());
          }
      }

    return colorspace_.map_to_rgb(components, rgb);
  }

  bool pdf_resource<PAGE_SHADING>::build_stops()
  {
    stops_.clear();
    stops_.reserve(NUM_STOPS);

    for(std::size_t i = 0; i < NUM_STOPS; i++)
      {
        const double offset = static_cast<double>(i) / static_cast<double>(NUM_STOPS - 1);
        const double t = domain_[0] + offset * (domain_[1] - domain_[0]);

        std::array<int, 3> rgb = {0, 0, 0};
        if(not evaluate_color(t, rgb))
          {
            reject("colour ramp could not be evaluated at t=" + std::to_string(t));
            stops_.clear();
            return false;
          }

        stops_.push_back(shading_stop(offset, rgb));
      }

    return true;
  }

}

#endif
