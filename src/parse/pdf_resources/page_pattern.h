//-*-C++-*-

#ifndef PDF_PAGE_PATTERN_RESOURCE_H
#define PDF_PAGE_PATTERN_RESOURCE_H

namespace pdflib
{

  // One /Pattern resource (ISO 32000-1, 8.7.3).
  //
  //   PatternType 1  tiling   -- a content stream replayed on a lattice
  //   PatternType 2  shading  -- a shading painted through the fill region
  //
  // Only the data is held here; painting a tiling pattern means running its
  // content stream, which only the stream decoder can do.
  template<>
  class pdf_resource<PAGE_PATTERN>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    void set(const std::string& key, QPDFObjectHandle obj);

    bool is_valid() const { return type_ == 1 or type_ == 2; }
    int get_pattern_type() const { return type_; }

    // Pattern space -> the default space of the page (NOT the CTM in force
    // when the pattern is used; 8.7.3.1).
    const std::array<double, 9>& get_matrix() const { return matrix_; }

    bool has_bbox() const { return has_bbox_; }
    const std::array<double, 4>& get_bbox() const { return bbox_; }

    double get_x_step() const { return x_step_; }
    double get_y_step() const { return y_step_; }

    // Tiling only: the pattern cell's own resources and content stream.
    QPDFObjectHandle get_resources() const { return resources_; }
    // QPDFObjectHandle's accessors are non-const in the qpdf releases we
    // build against, so go through a copy (the handle is a shared reference,
    // copying it costs nothing) -- the same way page_xobject_form does.
    bool has_resources() const
    {
      QPDFObjectHandle resources = resources_;
      return resources.isDictionary();
    }

    std::vector<qpdf_stream_instruction> parse_stream() const;

    // Shading pattern only.
    QPDFObjectHandle get_shading() const { return shading_; }

  private:

    std::string key_;
    int type_ = 0;

    std::array<double, 9> matrix_ = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool has_bbox_ = false;
    std::array<double, 4> bbox_ = {0, 0, 0, 0};
    double x_step_ = 0.0;
    double y_step_ = 0.0;

    QPDFObjectHandle qpdf_pattern_;
    QPDFObjectHandle resources_;
    QPDFObjectHandle shading_;
  };

  pdf_resource<PAGE_PATTERN>::pdf_resource()
  {}

  pdf_resource<PAGE_PATTERN>::~pdf_resource()
  {}

  void pdf_resource<PAGE_PATTERN>::set(const std::string& key, QPDFObjectHandle obj)
  {
    key_ = key;
    qpdf_pattern_ = obj;

    QPDFObjectHandle dict = obj.isStream() ? obj.getDict() : obj;
    if(not dict.isDictionary())
      {
        LOG_S(WARNING) << "pattern " << key_ << ": not a dictionary";
        return;
      }

    if(dict.hasKey("/PatternType") and dict.getKey("/PatternType").isInteger())
      {
        type_ = static_cast<int>(dict.getKey("/PatternType").getIntValue());
      }

    if(dict.hasKey("/Matrix") and dict.getKey("/Matrix").isArray() and
       dict.getKey("/Matrix").getArrayNItems() >= 6)
      {
        QPDFObjectHandle m = dict.getKey("/Matrix");
        double v[6] = {1, 0, 0, 1, 0, 0};
        for(int i = 0; i < 6; i++)
          {
            QPDFObjectHandle item = m.getArrayItem(i);
            if(item.isNumber()) { v[i] = item.getNumericValue(); }
          }
        // Row-vector 3x3, matching the decoder's trafo_matrix layout.
        matrix_ = { v[0], v[1], 0.0,
                    v[2], v[3], 0.0,
                    v[4], v[5], 1.0 };
      }

    if(dict.hasKey("/BBox") and dict.getKey("/BBox").isArray() and
       dict.getKey("/BBox").getArrayNItems() >= 4)
      {
        QPDFObjectHandle b = dict.getKey("/BBox");
        for(int i = 0; i < 4; i++)
          {
            QPDFObjectHandle item = b.getArrayItem(i);
            if(item.isNumber()) { bbox_[i] = item.getNumericValue(); }
          }
        has_bbox_ = true;
      }

    if(dict.hasKey("/XStep") and dict.getKey("/XStep").isNumber())
      {
        x_step_ = dict.getKey("/XStep").getNumericValue();
      }
    if(dict.hasKey("/YStep") and dict.getKey("/YStep").isNumber())
      {
        y_step_ = dict.getKey("/YStep").getNumericValue();
      }

    // A missing or zero step means "one cell, no repetition"; fall back to the
    // bbox extent so the tiling loop always advances.
    if(x_step_ <= 0.0 and has_bbox_) { x_step_ = bbox_[2] - bbox_[0]; }
    if(y_step_ <= 0.0 and has_bbox_) { y_step_ = bbox_[3] - bbox_[1]; }

    if(dict.hasKey("/Resources")) { resources_ = dict.getKey("/Resources"); }
    if(dict.hasKey("/Shading"))   { shading_   = dict.getKey("/Shading"); }

    LOG_S(INFO) << "pattern " << key_ << ": type " << type_
                << " steps=(" << x_step_ << ", " << y_step_ << ")"
                << (has_bbox_ ? " with /BBox" : " no /BBox");
  }

  std::vector<qpdf_stream_instruction> pdf_resource<PAGE_PATTERN>::parse_stream() const
  {
    std::vector<qpdf_stream_instruction> insts;

    QPDFObjectHandle stream = qpdf_pattern_;
    if(not stream.isStream())
      {
        return insts;
      }

    try
      {
        qpdf_stream_decoder decoder(insts);
        decoder.decode(stream);
      }
    catch(const std::exception& e)
      {
        LOG_S(WARNING) << "pattern " << key_ << ": content stream unreadable: " << e.what();
        insts.clear();
      }

    return insts;
  }

}

#endif
