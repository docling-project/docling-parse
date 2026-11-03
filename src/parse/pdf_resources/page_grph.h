//-*-C++-*-

#ifndef PDF_PAGE_GRPH_RESOURCE_H
#define PDF_PAGE_GRPH_RESOURCE_H

namespace pdflib
{

  // An ExtGState /SMask entry as parsed (ISO 32000-1, 11.6.5.1, Table 144):
  // the transparency group that paints the mask, which of its planes the mask
  // is read from, the backdrop the group is composited against and the
  // transfer function that maps the result to coverage.
  //
  // The group's content stream is not decoded here. That needs the resources
  // and the CTM in force at the `gs`, so the stream decoder does it, once
  // per entry and CTM, into a soft_mask_instruction.
  struct soft_mask_spec
  {
    std::string        key;                        // ExtGState resource name
    QPDFObjectHandle   group;                      // /G, a form XObject stream
    bool               luminosity = true;          // /S /Luminosity, else /Alpha
    std::array<int, 3> backdrop_rgb = {0, 0, 0};   // /BC in device RGB, black when absent
    std::vector<uint8_t> transfer;                 // /TR sampled at 256 points, empty = identity
  };

  // A single entry of a page's (or form's) /ExtGState resource dictionary,
  // ie the parameter set a `gs` operator installs into the graphics state.
  //
  // A `gs` only changes the parameters *present* in its dictionary
  // (PDF 32000-1, 8.4.5), so every parameter is paired with a has_* flag
  // rather than a default value.
  template<>
  class pdf_resource<PAGE_GRPH>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();

    void set(const std::string& key_,
	     QPDFObjectHandle qpdf_grph);

    // /CA (stroking) and /ca (non-stroking) constant alpha.
    // 1.0 = opaque, 0.0 = fully transparent.
    bool has_stroke_alpha() const { return has_stroke_alpha_; }
    bool has_fill_alpha()   const { return has_fill_alpha_; }

    double get_stroke_alpha() const { return stroke_alpha_; }
    double get_fill_alpha()   const { return fill_alpha_; }

    // Table 58 parameters that duplicate the w/J/j/M/d/i operators
    bool has_line_width()  const { return has_line_width_; }
    bool has_line_cap()    const { return has_line_cap_; }
    bool has_line_join()   const { return has_line_join_; }
    bool has_miter_limit() const { return has_miter_limit_; }
    bool has_dash()        const { return has_dash_; }
    bool has_flatness()    const { return has_flatness_; }

    double get_line_width()  const { return line_width_; }
    int    get_line_cap()    const { return line_cap_; }
    int    get_line_join()   const { return line_join_; }
    double get_miter_limit() const { return miter_limit_; }
    double get_flatness()    const { return flatness_; }

    const std::vector<double>& get_dash_array() const { return dash_array_; }
    double                     get_dash_phase() const { return dash_phase_; }

    // Transparency parameters. /BM and /SMask are composited by the
    // renderer; /AIS is recorded only.
    bool has_blend_mode()    const { return has_blend_mode_; }
    bool has_alpha_is_shape() const { return has_alpha_is_shape_; }

    blend_mode_name get_blend_mode()     const { return blend_mode_; }
    soft_mask_state get_soft_mask()      const { return soft_mask_; }

    // The parsed /SMask when get_soft_mask() is SOFT_MASK_PRESENT, else null.
    const std::shared_ptr<const soft_mask_spec>& get_soft_mask_spec() const { return soft_mask_spec_; }
    bool            get_alpha_is_shape() const { return alpha_is_shape_; }

    // /Font: [font_ref size]. The font is an indirect reference to a font
    // dictionary, not a name in the /Font resources, so only the size is
    // usable without a separate lookup.
    bool   has_font()      const { return has_font_; }
    double get_font_size() const { return font_size_; }

    // /RI rendering intent, kept verbatim ("" when absent)
    const std::string& get_rendering_intent() const { return rendering_intent_; }

  private:

    // Snapshot of the dictionary for diagnostics. Scalar entries are kept
    // verbatim; streams and dictionaries collapse to a type tag, so that a
    // dump stays readable and cheap.
    static nlohmann::json to_json(QPDFObjectHandle qpdf_grph);

    void set_alpha(QPDFObjectHandle& qpdf_grph);
    void set_stroke_parameters(QPDFObjectHandle& qpdf_grph);
    void set_transparency_parameters(QPDFObjectHandle& qpdf_grph);

    // Reads an /SMask dictionary. Null when it cannot be read, in which case
    // the reason has been logged.
    static std::shared_ptr<const soft_mask_spec> parse_soft_mask(const std::string& key,
                                                                 QPDFObjectHandle smask);

  private:

    std::string    key;
    nlohmann::json val;

    bool   has_stroke_alpha_ = false;
    bool   has_fill_alpha_   = false;
    double stroke_alpha_     = 1.0;
    double fill_alpha_       = 1.0;

    bool has_line_width_  = false;
    bool has_line_cap_    = false;
    bool has_line_join_   = false;
    bool has_miter_limit_ = false;
    bool has_dash_        = false;
    bool has_flatness_    = false;

    double line_width_  = 1.0;
    int    line_cap_    = 0;
    int    line_join_   = 0;
    double miter_limit_ = 10.0;
    double flatness_    = 1.0;

    std::vector<double> dash_array_ = {};
    double              dash_phase_ = 0.0;

    bool            has_blend_mode_ = false;
    blend_mode_name blend_mode_     = BLEND_MODE_NORMAL;

    soft_mask_state soft_mask_ = SOFT_MASK_ABSENT;
    std::shared_ptr<const soft_mask_spec> soft_mask_spec_;

    bool has_alpha_is_shape_ = false;
    bool alpha_is_shape_     = false;

    bool   has_font_  = false;
    double font_size_ = 0.0;

    std::string rendering_intent_ = "";
  };

  pdf_resource<PAGE_GRPH>::pdf_resource():
    key(""),
    val(nullptr)
  {}

  pdf_resource<PAGE_GRPH>::~pdf_resource()
  {}

  nlohmann::json pdf_resource<PAGE_GRPH>::get()
  {
    return val;
  }

  nlohmann::json pdf_resource<PAGE_GRPH>::to_json(QPDFObjectHandle qpdf_grph)
  {
    nlohmann::json result = nlohmann::json::object();

    if(not qpdf_grph.isDictionary())
      {
        return result;
      }

    for(auto& name : qpdf_grph.getKeys())
      {
        QPDFObjectHandle item = qpdf_grph.getKey(name);

        if(item.isNumber())
          {
            result[name] = utils::numeric::locale_safe_numeric_value(item);
          }
        else if(item.isBool())
          {
            result[name] = item.getBoolValue();
          }
        else if(item.isName())
          {
            result[name] = item.getName();
          }
        else if(item.isArray())
          {
            result[name] = "<array[" + std::to_string(item.getArrayNItems()) + "]>";
          }
        else if(item.isStream())
          {
            result[name] = "<stream>";
          }
        else if(item.isDictionary())
          {
            result[name] = "<dictionary>";
          }
        else if(item.isNull())
          {
            result[name] = nullptr;
          }
        else
          {
            result[name] = "<unhandled>";
          }
      }

    return result;
  }

  void pdf_resource<PAGE_GRPH>::set(const std::string& key_,
				    QPDFObjectHandle qpdf_grph)
  {
    key = key_;
    val = to_json(qpdf_grph);

    if(not qpdf_grph.isDictionary())
      {
        LOG_S(WARNING) << "ExtGState " << key << " is not a dictionary: ignoring it";
        return;
      }

    set_alpha(qpdf_grph);
    set_stroke_parameters(qpdf_grph);
    set_transparency_parameters(qpdf_grph);
  }

  void pdf_resource<PAGE_GRPH>::set_alpha(QPDFObjectHandle& qpdf_grph)
  {
    if(qpdf_grph.hasKey("/CA") and qpdf_grph.getKey("/CA").isNumber())
      {
	QPDFObjectHandle CA = qpdf_grph.getKey("/CA");

	has_stroke_alpha_ = true;
	stroke_alpha_ = utils::numeric::locale_safe_numeric_value(CA);
      }

    if(qpdf_grph.hasKey("/ca") and qpdf_grph.getKey("/ca").isNumber())
      {
	QPDFObjectHandle ca = qpdf_grph.getKey("/ca");

	has_fill_alpha_ = true;
	fill_alpha_ = utils::numeric::locale_safe_numeric_value(ca);
      }
  }

  // /LW, /LC, /LJ, /ML, /D and /FL set exactly the parameters that the w, J,
  // j, M, d and i operators set. /D is [dashArray dashPhase], ie the operands
  // of `d` wrapped in an array.
  void pdf_resource<PAGE_GRPH>::set_stroke_parameters(QPDFObjectHandle& qpdf_grph)
  {
    // locale_safe_numeric_value takes a non-const reference, so every operand
    // has to be held in a named handle first
    if(qpdf_grph.hasKey("/LW"))
      {
        QPDFObjectHandle LW = qpdf_grph.getKey("/LW");

        if(LW.isNumber())
          {
            has_line_width_ = true;
            line_width_ = utils::numeric::locale_safe_numeric_value(LW);
          }
      }

    if(qpdf_grph.hasKey("/LC"))
      {
        QPDFObjectHandle LC = qpdf_grph.getKey("/LC");

        if(LC.isInteger())
          {
            has_line_cap_ = true;
            line_cap_ = LC.getIntValueAsInt();
          }
      }

    if(qpdf_grph.hasKey("/LJ"))
      {
        QPDFObjectHandle LJ = qpdf_grph.getKey("/LJ");

        if(LJ.isInteger())
          {
            has_line_join_ = true;
            line_join_ = LJ.getIntValueAsInt();
          }
      }

    if(qpdf_grph.hasKey("/ML"))
      {
        QPDFObjectHandle ML = qpdf_grph.getKey("/ML");

        if(ML.isNumber())
          {
            has_miter_limit_ = true;
            miter_limit_ = utils::numeric::locale_safe_numeric_value(ML);
          }
      }

    if(qpdf_grph.hasKey("/FL"))
      {
        QPDFObjectHandle FL = qpdf_grph.getKey("/FL");

        if(FL.isNumber())
          {
            has_flatness_ = true;
            flatness_ = utils::numeric::locale_safe_numeric_value(FL);
          }
      }

    if(qpdf_grph.hasKey("/D"))
      {
        QPDFObjectHandle D = qpdf_grph.getKey("/D");

        if(not D.isArray()) { return; }

        if(D.getArrayNItems()!=2 or (not D.getArrayItem(0).isArray()))
          {
            LOG_S(WARNING) << "ExtGState " << key
                           << ": /D is not [dashArray dashPhase], ignoring it";
            return;
          }

        QPDFObjectHandle arr = D.getArrayItem(0);

        // a malformed entry must not leave a half-built dash pattern behind
        std::vector<double> dash_array;

        for(int l=0; l<arr.getArrayNItems(); l++)
          {
            QPDFObjectHandle item = arr.getArrayItem(l);

            if(not item.isNumber())
              {
                LOG_S(WARNING) << "ExtGState " << key
                               << ": non-numeric item in /D dash array, ignoring it";
                return;
              }

            dash_array.push_back(utils::numeric::locale_safe_numeric_value(item));
          }

        QPDFObjectHandle phase = D.getArrayItem(1);

        double dash_phase = 0.0;
        if(phase.isNumber())
          {
            dash_phase = utils::numeric::locale_safe_numeric_value(phase);
          }

        has_dash_   = true;
        dash_array_ = dash_array;
        dash_phase_ = dash_phase;
      }
  }

  std::shared_ptr<const soft_mask_spec>
  pdf_resource<PAGE_GRPH>::parse_soft_mask(const std::string& key,
                                           QPDFObjectHandle smask)
  {
    if(not smask.isDictionary())
      {
        LOG_S(WARNING) << "ExtGState " << key
                       << ": /SMask is neither /None nor a dictionary, paint stays unmasked";
        return nullptr;
      }

    if(not smask.hasKey("/G") or not smask.getKey("/G").isStream())
      {
        LOG_S(WARNING) << "ExtGState " << key
                       << ": soft mask has no /G group stream, paint stays unmasked";
        return nullptr;
      }

    auto spec = std::make_shared<soft_mask_spec>();
    spec->key = key;
    spec->group = smask.getKey("/G");

    if(smask.hasKey("/S") and smask.getKey("/S").isName())
      {
        const std::string type = smask.getKey("/S").getName();
        if(type=="/Alpha")
          {
            spec->luminosity = false;
          }
        else if(type!="/Luminosity")
          {
            LOG_S(WARNING) << "ExtGState " << key << ": unknown soft mask type "
                           << type << ", reading it as /Luminosity";
          }
      }

    // /BC is given in the group's colour space. The component count says
    // which device space to read it as, the way SC/sc operands are read.
    if(smask.hasKey("/BC") and smask.getKey("/BC").isArray())
      {
        QPDFObjectHandle BC = smask.getKey("/BC");

        std::vector<double> comps;
        for(int i=0; i<BC.getArrayNItems(); i++)
          {
            QPDFObjectHandle item = BC.getArrayItem(i);
            if(item.isNumber())
              {
                const double v = utils::numeric::locale_safe_numeric_value(item);
                comps.push_back(std::min(1.0, std::max(0.0, v)));
              }
          }

        auto to_byte = [](double v) { return static_cast<int>(std::lround(255.0 * v)); };

        if(comps.size()==1)
          {
            spec->backdrop_rgb = {to_byte(comps[0]), to_byte(comps[0]), to_byte(comps[0])};
          }
        else if(comps.size()==3)
          {
            spec->backdrop_rgb = {to_byte(comps[0]), to_byte(comps[1]), to_byte(comps[2])};
          }
        else if(comps.size()==4)
          {
            const double k = comps[3];
            spec->backdrop_rgb = {to_byte((1.0 - comps[0]) * (1.0 - k)),
                                  to_byte((1.0 - comps[1]) * (1.0 - k)),
                                  to_byte((1.0 - comps[2]) * (1.0 - k))};
          }
        else
          {
            LOG_S(WARNING) << "ExtGState " << key << ": soft mask /BC has "
                           << comps.size() << " components, using black";
          }
      }

    // /TR maps the group's luminosity (or alpha) to coverage. It is sampled
    // once here so the renderer only indexes a table.
    if(smask.hasKey("/TR"))
      {
        QPDFObjectHandle TR = smask.getKey("/TR");

        if(TR.isName())
          {
            if(TR.getName()!="/Identity")
              {
                LOG_S(WARNING) << "ExtGState " << key << ": soft mask /TR "
                               << TR.getName() << " is not a function, using the identity";
              }
          }
        else
          {
            pdf_function function;
            if(function.set(TR) and function.get_num_inputs()==1)
              {
                std::vector<uint8_t> table(256, 0);
                std::vector<double> out;
                bool ok = true;
                for(int i=0; i<256 and ok; i++)
                  {
                    ok = function.evaluate(i / 255.0, out) and not out.empty();
                    if(ok)
                      {
                        const double v = std::min(1.0, std::max(0.0, out[0]));
                        table[i] = static_cast<uint8_t>(std::lround(255.0 * v));
                      }
                  }

                if(ok)
                  {
                    spec->transfer = std::move(table);
                  }
                else
                  {
                    LOG_S(WARNING) << "ExtGState " << key
                                   << ": soft mask /TR could not be evaluated, using the identity";
                  }
              }
            else
              {
                LOG_S(WARNING) << "ExtGState " << key << ": soft mask /TR unreadable ("
                               << function.get_reason() << "), using the identity";
              }
          }
      }

    return spec;
  }

  // /BM and /SMask are kept for the renderer to composite. /AIS, /Font and
  // /RI are recorded but not applied: /Font needs the text state, which `gs`
  // does not reach.
  //
  // The deviations are reported here rather than in `gs`, so that each
  // affected ExtGState is named exactly once per document instead of once per
  // invocation.
  void pdf_resource<PAGE_GRPH>::set_transparency_parameters(QPDFObjectHandle& qpdf_grph)
  {
    if(qpdf_grph.hasKey("/BM"))
      {
        QPDFObjectHandle BM = qpdf_grph.getKey("/BM");

        // the value may also be an array of names, of which the first
        // supported one is used (11.3.5.2)
        std::string name = "";
        if(BM.isName())
          {
            name = BM.getName();
          }
        else if(BM.isArray() and BM.getArrayNItems()>0 and BM.getArrayItem(0).isName())
          {
            name = BM.getArrayItem(0).getName();
          }

        if(name.size()>0)
          {
            has_blend_mode_ = true;
            blend_mode_ = to_blend_mode_name(name);

            if(blend_mode_==BLEND_MODE_UNKNOWN)
              {
                LOG_S(WARNING) << "ExtGState " << key << ": unknown blend mode "
                               << name << ", treating it as /Normal";
              }
            else if(blend_mode_!=BLEND_MODE_NORMAL)
              {
                LOG_S(WARNING) << "ExtGState " << key << ": blend mode "
                               << to_string(blend_mode_)
                               << " is not composited, painting as /Normal";
              }
          }
      }

    if(qpdf_grph.hasKey("/SMask"))
      {
        QPDFObjectHandle SMask = qpdf_grph.getKey("/SMask");

        if(SMask.isName() and SMask.getName()=="/None")
          {
            soft_mask_ = SOFT_MASK_NONE;
          }
        else
          {
            // A mask that cannot be read clears the current one rather than
            // keeping a stale mask on paint it was never meant for.
            soft_mask_spec_ = parse_soft_mask(key, SMask);
            soft_mask_ = soft_mask_spec_ ? SOFT_MASK_PRESENT : SOFT_MASK_NONE;
          }
      }

    if(qpdf_grph.hasKey("/AIS"))
      {
        QPDFObjectHandle AIS = qpdf_grph.getKey("/AIS");

        if(AIS.isBool())
          {
            has_alpha_is_shape_ = true;
            alpha_is_shape_ = AIS.getBoolValue();
          }
      }

    if(qpdf_grph.hasKey("/Font"))
      {
        QPDFObjectHandle Font = qpdf_grph.getKey("/Font");

        if(Font.isArray() and Font.getArrayNItems()==2)
          {
            QPDFObjectHandle size = Font.getArrayItem(1);

            if(size.isNumber())
              {
                has_font_ = true;
                font_size_ = utils::numeric::locale_safe_numeric_value(size);

                LOG_S(WARNING) << "ExtGState " << key
                               << ": /Font is not applied, the font set by `Tf` is kept";
              }
          }
      }

    if(qpdf_grph.hasKey("/RI"))
      {
        QPDFObjectHandle RI = qpdf_grph.getKey("/RI");

        if(RI.isName())
          {
            rendering_intent_ = RI.getName();
          }
      }
  }

}

#endif
