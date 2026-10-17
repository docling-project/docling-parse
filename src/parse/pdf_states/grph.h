//-*-C++-*-

#ifndef PDF_GRPH_STATE_H
#define PDF_GRPH_STATE_H

namespace pdflib
{

  template<>
  class pdf_state<GRPH>
  {
    /*
      ### General graphics state (Table 57)

      - `w` — Set line width  
      Sets the thickness of stroked paths in user space units.  
      Operands: `lineWidth`  
      Notes: `0` means a device-dependent “hairline” width (often 1 device pixel).
      
      - `J` — Set line cap style  
      Sets the shape used at the ends of open subpaths when stroking.  
      Operands: `lineCap`  
      Values: `0` butt, `1` round, `2` projecting square.

      - `j` — Set line join style  
      Sets how corners are rendered where path segments meet when stroking.  
      Operands: `lineJoin`  
      Values: `0` miter, `1` round, `2` bevel.
      
      - `M` — Set miter limit  
      Limits how far a miter join can extend at sharp angles. If exceeded, bevel join is used.  
      Operands: `miterLimit`
      
      - `d` — Set line dash pattern  
      Sets the dash pattern for stroking.  
      Operands: `dashArray dashPhase`  
      `dashArray` alternates on/off lengths (e.g. `[3 2]`), `dashPhase` is the start offset.  
      Notes: `[] 0 d` disables dashing (solid line).

      - `ri` — Set rendering intent  
      Sets the color rendering intent for color conversions (mainly ICC-based workflows).  
      Operands: `intentName`  
      Typical values: `/Perceptual`, `/RelativeColorimetric`, `/Saturation`, `/AbsoluteColorimetric`  
      Notes: some renderers ignore this.
      
      - `i` — Set flatness tolerance  
      Controls curve flattening accuracy when approximating Bézier curves with line segments.  
      Operands: `flatness`  
      Notes: smaller = higher quality, larger = faster.
      
      - `gs` — Set parameters from graphics state dictionary (ExtGState)  
      Applies an ExtGState resource entry (named graphics state).  
      Operands: `gsName`  
      Common parameters: stroke/fill alpha (`CA`/`ca`), blend mode (`BM`), soft mask (`SMask`), overprint, etc.


      ### Color operators (Table 74)
      
      - `CS` — Set stroking color space  
      Sets the color space used for stroking operations.  
      Operands: `colorSpaceName`  
      Examples: `/DeviceRGB`, `/DeviceCMYK`, `/DeviceGray`, `/Pattern`, `/Cs1` (ICCBased, etc.)
      
      - `cs` — Set nonstroking (fill) color space  
      Sets the color space used for nonstroking (fill) operations.  
      Operands: `colorSpaceName`
      
      - `SC` — Set stroking color (in current stroking color space)  
      Sets stroking color components for the current stroking color space.  
      Operands: `c1 ... cn`  
      Example: DeviceRGB uses `R G B SC`, DeviceCMYK uses `C M Y K SC`.  
      Notes: for patterns and certain special spaces, use `SCN`.
      
      - `SCN` — Set stroking color (special: patterns / Separation / DeviceN, etc.)  
      Like `SC`, but supports Pattern color spaces and other cases needing extra parameters.  
      Operands: `c1 ... cn [name]` (varies by current color space)  
      Notes: for `/Pattern`, typically includes a pattern name (and optional component
      values for uncolored patterns).
      
      - `sc` — Set nonstroking (fill) color (in current nonstroking color space)  
      Sets fill color components for the current nonstroking color space.  
      Operands: `c1 ... cn`
      
      - `scn` — Set nonstroking (fill) color (special: patterns / Separation / DeviceN, etc.)  
      Like `sc`, but supports Pattern color spaces and other cases needing extra parameters.  
      Operands: `c1 ... cn [name]` (varies by current color space)
      
      - `G` — Set stroking gray (DeviceGray)  
      Sets stroking color space to DeviceGray and sets gray level.  
      Operands: `gray`  
      Range: typically `0` (black) to `1` (white).
      
      - `g` — Set nonstroking gray (DeviceGray)  
      Sets nonstroking color space to DeviceGray and sets gray level.  
      Operands: `gray`

      - `RG` — Set stroking RGB (DeviceRGB)  
      Sets stroking color space to DeviceRGB and sets color.  
      Operands: `r g b`  
      Range: typically `0..1` per component.
      
      - `rg` — Set nonstroking RGB (DeviceRGB)  
      Sets nonstroking color space to DeviceRGB and sets color.  
      Operands: `r g b`
      
      - `K` — Set stroking CMYK (DeviceCMYK)  
      Sets stroking color space to DeviceCMYK and sets color.  
      Operands: `c m y k`  
      Range: typically `0..1` per component.
      
      - `k` — Set nonstroking CMYK (DeviceCMYK)  
      Sets nonstroking color space to DeviceCMYK and sets color.  
      Operands: `c m y k`
      
      Note: `sh` (paint shading) is *not* handled here. A shading takes its
      colors from its own /Shading resource rather than from the graphics
      state, and it needs the current clipping path to know what it covers, so
      it is executed by the stream decoder (pdf_decoder<STREAM>::do_shading).
     */
    
  public:
    // Current transformation matrix. A pattern's /Matrix maps pattern space to
    // the page's default space, so placing one needs the CTM in force.
    const std::array<double, 9>& get_trafo_matrix() const { return trafo_matrix; }

    pdf_state(std::array<double, 9>&    trafo_matrix_,
              std::shared_ptr<pdf_resource<PAGE_GRPHS>> page_grphs_,
              std::shared_ptr<pdf_resource<PAGE_COLORSPACES>> page_colorspaces_);

    pdf_state(const pdf_state<GRPH>& other);
    
    ~pdf_state();

    pdf_state<GRPH>& operator=(const pdf_state<GRPH>& other);

    // General graphics state [Table 57 – Graphics State Operators p 127/135]

    void w(std::vector<qpdf_stream_instruction>& instructions);
    void J(std::vector<qpdf_stream_instruction>& instructions);
    void j(std::vector<qpdf_stream_instruction>& instructions);
    void M(std::vector<qpdf_stream_instruction>& instructions);

    void d(std::vector<qpdf_stream_instruction>& instructions);
    void ri(std::vector<qpdf_stream_instruction>& instructions);
    void i(std::vector<qpdf_stream_instruction>& instructions);
    void gs(std::vector<qpdf_stream_instruction>& instructions);
    
    // color-scheme Table 74 – Colour Operators [p 171/171]

    void CS(std::vector<qpdf_stream_instruction>& instructions);
    void cs(std::vector<qpdf_stream_instruction>& instructions);

    void SC(std::vector<qpdf_stream_instruction>& instructions);
    void SCN(std::vector<qpdf_stream_instruction>& instructions);

    void sc(std::vector<qpdf_stream_instruction>& instructions);
    void scn(std::vector<qpdf_stream_instruction>& instructions);
    
    void G(std::vector<qpdf_stream_instruction>& instructions);
    void g(std::vector<qpdf_stream_instruction>& instructions);

    void RG(std::vector<qpdf_stream_instruction>& instructions);
    void rg(std::vector<qpdf_stream_instruction>& instructions);

    void K(std::vector<qpdf_stream_instruction>& instructions);
    void k(std::vector<qpdf_stream_instruction>& instructions);

    // Public getters for graphics state properties
    double get_line_width() const { return line_width; }
    double get_miter_limit() const { return miter_limit; }
    int get_line_cap() const { return line_cap; }
    int get_line_join() const { return line_join; }
    double get_dash_phase() const { return dash_phase; }
    const std::vector<double>& get_dash_array() const { return dash_array; }
    double get_flatness() const { return flatness; }
    const std::array<int, 3>& get_rgb_stroking_ops() const { return rgb_stroking_ops; }
    const std::array<int, 3>& get_rgb_filling_ops() const { return rgb_filling_ops; }
    const std::string& get_curr_grph_key() const { return curr_grph_key; }

    // Constant alpha from ExtGState (/CA, /ca), folded together with the alpha
    // of any enclosing transparency group; 1.0 = opaque.
    double get_stroke_alpha() const { return stroke_alpha * group_alpha; }
    double get_fill_alpha() const { return fill_alpha * group_alpha; }

    // True while the non-stroking colour is a /Pattern that has not been
    // resolved to a paint. A Pattern colour space has no initial colour value
    // (ISO 32000-1, 8.6.6.2), so the black the state otherwise carries is not
    // "the colour": filling with it paints a black sheet where the pattern
    // should be.
    bool get_fill_is_unresolved_pattern() const { return fill_is_unresolved_pattern; }

    // Name given by the scn that selected a pattern, empty when none.
    const std::string& get_fill_pattern_name() const { return fill_pattern_name; }
    void materialize_pattern_fill_color();

    // ExtGState /BM, likewise folded together with the blend mode of an
    // enclosing group: content that does not blend on its own inherits the
    // group's mode, content that does keeps its own.
    blend_mode_name get_blend_mode() const
    {
      return (blend_mode == BLEND_MODE_NORMAL) ? group_blend_mode : blend_mode;
    }

    // /SMask is part of the graphics state (it survives q/Q). A `gs` that
    // installs one leaves the parsed entry and the CTM of that moment here;
    // the stream decoder then decodes the group -- it has the resources and
    // the state does not -- and hands back the decoded mask together with
    // the instruction every paint under it carries: the mask itself, or its
    // intersection with the mask of an enclosing transparency group.
    soft_mask_state get_soft_mask() const { return soft_mask; }
    const std::shared_ptr<const soft_mask_spec>& get_soft_mask_spec() const { return soft_mask_spec_; }
    const std::array<double, 9>& get_soft_mask_ctm() const { return soft_mask_ctm_; }

    // True between the `gs` that installed a mask and the decode of its group.
    bool has_pending_soft_mask() const
    {
      return soft_mask_spec_ != nullptr and soft_mask_instr_ == nullptr;
    }

    void set_soft_mask_instruction(std::shared_ptr<soft_mask_instruction> instr,
                                   std::shared_ptr<soft_mask_instruction> paint_instr)
    {
      soft_mask_instr_ = std::move(instr);
      paint_soft_mask_ = std::move(paint_instr);
    }

    // The mask a paint carries now, null when nothing masks it.
    const std::shared_ptr<soft_mask_instruction>& get_soft_mask_instruction() const
    {
      return paint_soft_mask_;
    }

    // The mask in force at the `Do` of the enclosing transparency groups.
    const std::shared_ptr<soft_mask_instruction>& get_group_soft_mask() const
    {
      return group_soft_mask_;
    }

    // Enters a transparency group XObject (11.6.6). The group is supposed to
    // be composited as a unit: its contents paint into their own buffer with a
    // fresh alpha and blend mode, and the *result* is then composited into the
    // page with the alpha and blend mode in force at the `Do`. There is no
    // such buffer here, so the group's parameters are instead pushed down onto
    // everything it paints. That is the same picture whenever the group's
    // contents do not overlap each other, and too strong where they do. A
    // soft mask goes the same way: the one in force at the `Do` masks every
    // paint inside, on top of any mask the group's content installs.
    void enter_transparency_group();

  private:
    struct color_set_result
    {
      bool color_set = false;
      std::string pattern_name;
    };

    bool verify(std::vector<qpdf_stream_instruction>& instructions,
		std::size_t num_instr, std::string name);

    // Color mapping for SC/sc/SCN/scn: when CS/cs resolved the current
    // color space from the /ColorSpace resources, map the operands through
    // it; otherwise interpret the numeric operands by their count
    // (1 -> gray, 3 -> RGB, 4 -> CMYK). A trailing name operand is reported
    // as a pattern selection; malformed operands or unsupported counts leave
    // rgb untouched.
    color_set_result set_color(std::vector<qpdf_stream_instruction>& instructions,
                               std::array<int, 3>& rgb,
                               const std::string& cs_key,
                               std::string name,
                               bool allow_pattern_name = false);

    // Shared CS/cs implementation: resolves the operand against the device
    // spaces and the /ColorSpace resources; cs_key receives the resource
    // key ("" for device spaces / unresolved names).
    void set_color_space(std::vector<qpdf_stream_instruction>& instructions,
                         std::array<int, 3>& rgb,
                         std::string& cs_key,
                         std::string name);

    static std::array<int, 3> gray_to_rgb(double gray);
    static std::array<int, 3> cmyk_to_rgb(double c, double m, double y, double k);

  private:
    
    std::array<double, 9>& trafo_matrix;

    std::shared_ptr<pdf_resource<PAGE_GRPHS>> page_grphs;
    std::shared_ptr<pdf_resource<PAGE_COLORSPACES>> page_colorspaces;

    std::string null_grph_key;
    std::string curr_grph_key;

    // /ColorSpace resource keys set by CS/cs; "" = device space or
    // unresolved (SC/sc then fall back to operand-count mapping)
    std::string stroking_cs_key;
    std::string filling_cs_key;

    double line_width;
    double miter_limit;

    int line_cap;
    int line_join;

    double              dash_phase;
    std::vector<double> dash_array;

    double flatness;

    std::array<int, 3> rgb_stroking_ops;
    std::array<int, 3> rgb_filling_ops;

    double stroke_alpha;
    double fill_alpha;
    bool fill_is_unresolved_pattern = false;
    std::string fill_pattern_name;

    blend_mode_name blend_mode;
    soft_mask_state soft_mask;

    // the mask installed by the last `gs` that set one, the CTM at that `gs`
    // (the mask's coordinate system, 11.6.5.1) and the decoded group
    std::shared_ptr<const soft_mask_spec>  soft_mask_spec_;
    std::array<double, 9>                  soft_mask_ctm_;
    std::shared_ptr<soft_mask_instruction> soft_mask_instr_;

    // what paints carry: soft_mask_instr_, group_soft_mask_ or the two
    // intersected
    std::shared_ptr<soft_mask_instruction> paint_soft_mask_;

    // accumulated parameters of the enclosing transparency groups
    double          group_alpha;
    blend_mode_name group_blend_mode;
    std::shared_ptr<soft_mask_instruction> group_soft_mask_;
  };

  pdf_state<GRPH>::pdf_state(std::array<double, 9>&    trafo_matrix_,
                             std::shared_ptr<pdf_resource<PAGE_GRPHS>> page_grphs_,
                             std::shared_ptr<pdf_resource<PAGE_COLORSPACES>> page_colorspaces_):
    trafo_matrix(trafo_matrix_),

    page_grphs(page_grphs_),
    page_colorspaces(page_colorspaces_),

    null_grph_key("null"),
    curr_grph_key(null_grph_key),

    stroking_cs_key(""),
    filling_cs_key(""),

    // PDF-spec defaults (Table 52 – Device-Independent Graphics State):
    // width 1.0, butt cap (0), miter join (0), miter limit 10.0,
    // solid dash, flatness 1.0, black stroke/fill
    line_width(1.0),
    miter_limit(10.0),

    line_cap(0),
    line_join(0),

    dash_phase(0),
    dash_array({}),

    flatness(1.0),

    rgb_stroking_ops({0,0,0}),
    rgb_filling_ops({0,0,0}),

    stroke_alpha(1.0),
    fill_alpha(1.0),

    blend_mode(BLEND_MODE_NORMAL),
    soft_mask(SOFT_MASK_NONE),

    soft_mask_spec_(nullptr),
    soft_mask_ctm_({1.0, 0.0, 0.0,
                    0.0, 1.0, 0.0,
                    0.0, 0.0, 1.0}),
    soft_mask_instr_(nullptr),
    paint_soft_mask_(nullptr),

    group_alpha(1.0),
    group_blend_mode(BLEND_MODE_NORMAL),
    group_soft_mask_(nullptr)
  {}

  pdf_state<GRPH>::pdf_state(const pdf_state<GRPH>& other):
    trafo_matrix(other.trafo_matrix),
    page_grphs(other.page_grphs),
    page_colorspaces(other.page_colorspaces)
  {
    *this = other;
  }

  pdf_state<GRPH>::~pdf_state()
  {}

  pdf_state<GRPH>& pdf_state<GRPH>::operator=(const pdf_state<GRPH>& other)
  {
    this->curr_grph_key = other.curr_grph_key;

    // the current color spaces are part of the graphics state (q/Q)
    this->stroking_cs_key = other.stroking_cs_key;
    this->filling_cs_key = other.filling_cs_key;

    this->line_width = other.line_width;
    this->miter_limit = other.miter_limit;

    this->line_cap = other.line_cap;
    this->line_join = other.line_join;

    this->dash_phase = other.dash_phase;
    this->dash_array = other.dash_array;

    this->flatness = other.flatness;

    this->rgb_stroking_ops = other.rgb_stroking_ops;
    this->rgb_filling_ops = other.rgb_filling_ops;

    // alpha is part of the graphics state and must survive q/Q save/restore
    this->stroke_alpha = other.stroke_alpha;
    this->fill_alpha = other.fill_alpha;
    this->fill_is_unresolved_pattern = other.fill_is_unresolved_pattern;
    this->fill_pattern_name = other.fill_pattern_name;

    this->blend_mode = other.blend_mode;
    this->soft_mask = other.soft_mask;
    this->soft_mask_spec_ = other.soft_mask_spec_;
    this->soft_mask_ctm_ = other.soft_mask_ctm_;
    this->soft_mask_instr_ = other.soft_mask_instr_;
    this->paint_soft_mask_ = other.paint_soft_mask_;

    this->group_alpha = other.group_alpha;
    this->group_blend_mode = other.group_blend_mode;
    this->group_soft_mask_ = other.group_soft_mask_;

    return *this;
  }

  void pdf_state<GRPH>::enter_transparency_group()
  {
    // Fold what is in force at the `Do` into the group parameters, then reset
    // the state itself: 11.6.6 starts a group's contents at alpha 1, Normal
    // and no soft mask, and the content's own ExtGState will say otherwise if
    // it means to.
    group_alpha = get_fill_alpha();
    group_blend_mode = get_blend_mode();
    group_soft_mask_ = paint_soft_mask_;

    stroke_alpha = 1.0;
    fill_alpha = 1.0;
    blend_mode = BLEND_MODE_NORMAL;

    soft_mask = SOFT_MASK_NONE;
    soft_mask_spec_ = nullptr;
    soft_mask_instr_ = nullptr;
  }

  bool pdf_state<GRPH>::verify(std::vector<qpdf_stream_instruction>& instructions,
			       std::size_t num_instr, std::string name)
  {
    if(instructions.size()==num_instr)
      {
	return true;
      }

    if(instructions.size()>num_instr)
      {
        const std::size_t excess = instructions.size() - num_instr;
        LOG_S(WARNING) << "#-instructions " << instructions.size()
                       << " exceeds expected value " << num_instr << " for "
                       << name;
        LOG_S(WARNING) << " => ignoring " << excess
                       << " leading operand(s)";
        instructions.erase(instructions.begin(),
                           instructions.begin() + excess);
	
	return true;
      }

    if(instructions.size()<num_instr) // fatal ...
      {
	std::stringstream ss;
	ss << "#-instructions " << instructions.size()
	   << " does not match expected value " << num_instr
	   << " for PDF operation: "
	   << name;
	
	LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());	
      }    

    return false;
  }

  std::array<int, 3> pdf_state<GRPH>::gray_to_rgb(double gray)
  {
    int v = static_cast<int>(std::round(255.0*gray));
    return {v, v, v};
  }

  std::array<int, 3> pdf_state<GRPH>::cmyk_to_rgb(double c, double m, double y, double k)
  {
    return color::cmyk_to_rgb255(c, m, y, k);
  }

  pdf_state<GRPH>::color_set_result pdf_state<GRPH>::set_color(
    std::vector<qpdf_stream_instruction>& instructions,
    std::array<int, 3>& rgb,
    const std::string& cs_key,
    std::string name,
    bool allow_pattern_name)
  {
    color_set_result result;
    std::vector<double> comps;
    for(std::size_t i = 0; i < instructions.size(); i++)
      {
        auto& instr = instructions[i];
        if(instr.is_number())
          {
            comps.push_back(instr.to_double());
          }
        else if(allow_pattern_name and i + 1 == instructions.size())
          {
            std::string nm = instr.unparse();
            if(not nm.empty() and nm.front() == '/')
              {
                result.pattern_name = nm;
                continue;
              }

            LOG_S(WARNING) << name << ": invalid pattern name operand "
                           << instr.unparse() << ", keeping current color";
            return result;
          }
        else // unexpected type
          {
            LOG_S(WARNING) << name << ": non-numeric operand "
                           << instr.unparse() << ", keeping current color";
            return result;
          }
      }

    if(not result.pattern_name.empty() and comps.empty())
      {
        return result;
      }

    // resolved /ColorSpace resource from CS/cs (ICCBased, Indexed, ...)
    if(cs_key.size()>0 and
       page_colorspaces!=nullptr and
       page_colorspaces->count(cs_key)>0)
      {
        if((*page_colorspaces)[cs_key].map_to_rgb(comps, rgb))
          {
            result.color_set = true;
            return result;
          }

        LOG_S(WARNING) << name << ": color space " << cs_key
                       << " cannot map " << comps.size()
                       << " operand(s), falling back to operand count";
      }

    switch(comps.size())
      {
      case 1:
        {
          rgb = gray_to_rgb(comps[0]);
          result.color_set = true;
          return result;
        }
      case 3:
        {
          rgb = {static_cast<int>(std::round(255.0*comps[0])),
                 static_cast<int>(std::round(255.0*comps[1])),
                 static_cast<int>(std::round(255.0*comps[2]))};
          result.color_set = true;
          return result;
        }
      case 4:
        {
          rgb = cmyk_to_rgb(comps[0], comps[1], comps[2], comps[3]);
          result.color_set = true;
          return result;
        }
      default:
        {
          LOG_S(WARNING) << name << ": unsupported number of color components ("
                         << comps.size() << "), keeping current color";
          return result;
        }
      }
  }

  void pdf_state<GRPH>::w(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    line_width = instructions[0].to_double();
  }

  void pdf_state<GRPH>::J(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    line_cap = instructions[0].to_int();
  }

  void pdf_state<GRPH>::j(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    line_join = instructions[0].to_int();
  }

  void pdf_state<GRPH>::M(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    miter_limit = instructions[0].to_double();
  }
  
  // Table 56 – Examples of Line Dash Patterns [p 127/135]
  void pdf_state<GRPH>::d(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not verify(instructions, 2, __FUNCTION__) ) { return; }
 
    QPDFObjectHandle arr = instructions[0].obj;

    //if(not arr.isArray()) { LOG_S(ERROR) << "instructions[0].obj is not an array"; return; }
    
    if(arr.isArray())
      {
	for(int l=0; l<arr.getArrayNItems(); l++)
	  {
	    QPDFObjectHandle item = arr.getArrayItem(l);
	    
	    //assert(item.isNumber());
	    if(item.isNumber())
	      {
		double val = utils::numeric::locale_safe_numeric_value(item);
		dash_array.push_back(val);
	      }
	    else
	      {
		LOG_S(WARNING) << "skipping items for dash_array ...";
	      }
	  }
      }
    else if(arr.isNull())
      {
	LOG_S(WARNING) << "instructions[0].obj is null, re-interpreting it as an empty array";
	dash_array = {};
      }
    else
      {
	LOG_S(ERROR) << "instructions[0].obj is not an array nor null, defualting to an empty array";
	dash_array = {};
      }
    
    if(instructions[1].is_integer())
      {
	dash_phase = instructions[1].to_int();
      }
    else if(instructions[1].is_number())
      {
	dash_phase = instructions[1].to_double();
      }
    else
      {
	dash_phase = 0;
	LOG_S(ERROR) << "failed instructions[1] with is_integer() and is_number"
		       << instructions[1].unparse();
      }
  }

  void pdf_state<GRPH>::ri(std::vector<qpdf_stream_instruction>& instructions)
  {
    LOG_S(WARNING) << "implement " << __FUNCTION__ << ": " << instructions.size();
  }

  void pdf_state<GRPH>::i(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    if(instructions[0].is_number())
      {
	flatness = instructions[0].to_double();
      }
    else
      {
	flatness = 0;
	LOG_S(ERROR) << "failed instructions[0].is_number(): "
		       << instructions[0].unparse();	
      }
  }

  // `gs` installs the parameters of a named ExtGState dictionary into the
  // graphics state (8.4.5). It only changes the parameters *present* in that
  // dictionary, hence the has_* guards; the /LW, /LC, /LJ, /ML, /D and /FL
  // entries set exactly what the w, J, j, M, d and i operators set.
  //
  // The name is resolved against the currently active resource scope, which
  // for a form XObject is the form's own /ExtGState with the page's as parent.
  void pdf_state<GRPH>::gs(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }

    std::string key = instructions[0].to_utf8_string();

    if(page_grphs->count(key)==0)
      {
	LOG_S(WARNING) << "key (=" << key << ") not found in page_grphs: " << page_grphs->get().dump(2);
	curr_grph_key = null_grph_key;
	return;
      }

    curr_grph_key = key;

    auto& grph = (*page_grphs)[key];

    if(grph.has_stroke_alpha()) { stroke_alpha = grph.get_stroke_alpha(); }
    if(grph.has_fill_alpha())   { fill_alpha   = grph.get_fill_alpha(); }

    if(grph.has_line_width())  { line_width  = grph.get_line_width(); }
    if(grph.has_line_cap())    { line_cap    = grph.get_line_cap(); }
    if(grph.has_line_join())   { line_join   = grph.get_line_join(); }
    if(grph.has_miter_limit()) { miter_limit = grph.get_miter_limit(); }
    if(grph.has_flatness())    { flatness    = grph.get_flatness(); }

    if(grph.has_dash())
      {
	// user-space quantities, scaled by the CTM where the shapes are emitted
	dash_array = grph.get_dash_array();
	dash_phase = grph.get_dash_phase();
      }

    if(grph.has_blend_mode()) { blend_mode = grph.get_blend_mode(); }

    if(grph.get_soft_mask()==SOFT_MASK_NONE)
      {
        soft_mask = SOFT_MASK_NONE;
        soft_mask_spec_ = nullptr;
        soft_mask_instr_ = nullptr;
        paint_soft_mask_ = group_soft_mask_;
      }
    else if(grph.get_soft_mask()==SOFT_MASK_PRESENT)
      {
        // The mask's coordinate system is the CTM at this `gs`, not the one
        // at the paint (11.6.5.1), so it is captured now. The group itself is
        // decoded by the stream decoder right after this returns.
        soft_mask = SOFT_MASK_PRESENT;
        soft_mask_spec_ = grph.get_soft_mask_spec();
        soft_mask_ctm_ = trafo_matrix;
        soft_mask_instr_ = nullptr;
        paint_soft_mask_ = group_soft_mask_;
      }

    LOG_S(INFO) << "gs " << key
		<< ": fill-alpha " << fill_alpha
		<< ", stroke-alpha " << stroke_alpha
		<< ", blend-mode " << to_string(blend_mode);
  }

  // Per PDF spec, CS/cs also reset the current color to the color space's
  // initial value: black for the device spaces and the resolved resource
  // spaces we approximate. Named spaces are looked up in the /ColorSpace
  // resources; only when that fails do SC/SCN/sc/scn fall back to the
  // operand-count mapping.
  void pdf_state<GRPH>::set_color_space(std::vector<qpdf_stream_instruction>& instructions,
                                        std::array<int, 3>& rgb,
                                        std::string& cs_key,
                                        std::string name)
  {
    if(not verify(instructions, 1, name) ) { return; }

    std::string cs_name = instructions[0].is_string()?
      instructions[0].to_utf8_string() : instructions[0].unparse();
    if(not cs_name.empty() and cs_name.front()=='/') { cs_name = cs_name.substr(1); }

    cs_key = "";

    if(cs_name=="DeviceGray" or cs_name=="DeviceRGB" or cs_name=="DeviceCMYK" or
       cs_name=="CalGray" or cs_name=="CalRGB")
      {
        rgb = {0, 0, 0};
      }
    else if(cs_name=="Pattern")
      {
        // A Pattern space carries no initial colour; the paint comes from the
        // pattern named by a later scn. Flagged so a fill made before that
        // pattern can be resolved paints nothing rather than black.
        if(&rgb == &rgb_filling_ops)
          {
            fill_is_unresolved_pattern = true;
            fill_pattern_name.clear();
          }
      }
    else if(page_colorspaces!=nullptr and page_colorspaces->count("/"+cs_name)>0)
      {
        cs_key = "/"+cs_name;
        auto& colorspace = (*page_colorspaces)[cs_key];
        if(colorspace.get_family() == COLOR_SPACE_PATTERN)
          {
            if(&rgb == &rgb_filling_ops)
              {
                fill_is_unresolved_pattern = true;
                fill_pattern_name.clear();
              }
          }
        else
          {
            rgb = {0, 0, 0};
          }
      }
    else
      {
        LOG_S(WARNING) << name << ": unresolved color space " << cs_name
                       << ", colors fall back to operand-count mapping";
      }
  }

  void pdf_state<GRPH>::CS(std::vector<qpdf_stream_instruction>& instructions)
  {
    set_color_space(instructions, rgb_stroking_ops, stroking_cs_key, __FUNCTION__);
  }

  void pdf_state<GRPH>::cs(std::vector<qpdf_stream_instruction>& instructions)
  {
    set_color_space(instructions, rgb_filling_ops, filling_cs_key, __FUNCTION__);
  }

  void pdf_state<GRPH>::SC(std::vector<qpdf_stream_instruction>& instructions)
  {
    set_color(instructions, rgb_stroking_ops, stroking_cs_key, __FUNCTION__);
  }

  void pdf_state<GRPH>::SCN(std::vector<qpdf_stream_instruction>& instructions)
  {
    set_color(instructions, rgb_stroking_ops, stroking_cs_key, __FUNCTION__, true);
  }

  void pdf_state<GRPH>::sc(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(set_color(instructions, rgb_filling_ops, filling_cs_key, __FUNCTION__).color_set)
      {
        fill_is_unresolved_pattern = false;
      }
  }

  void pdf_state<GRPH>::scn(std::vector<qpdf_stream_instruction>& instructions)
  {
    color_set_result result =
      set_color(instructions, rgb_filling_ops, filling_cs_key, __FUNCTION__, true);

    if(not result.pattern_name.empty())
      {
        fill_is_unresolved_pattern = true;
        fill_pattern_name = result.pattern_name;
      }
    else if(result.color_set)
      {
        fill_is_unresolved_pattern = false;
        fill_pattern_name.clear();
      }
  }

  void pdf_state<GRPH>::materialize_pattern_fill_color()
  {
    fill_is_unresolved_pattern = false;
    fill_pattern_name.clear();
    filling_cs_key.clear();
  }
  
  // G/g/RG/rg/K/k also switch the current color space to the device space,
  // so the resolved /ColorSpace resource key must be dropped.

  void pdf_state<GRPH>::G(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }

    stroking_cs_key = "";
    rgb_stroking_ops = gray_to_rgb(instructions[0].to_double());
  }

  // `g` is the NON-stroking (fill) gray operator
  void pdf_state<GRPH>::g(std::vector<qpdf_stream_instruction>& instructions)
  {
    fill_is_unresolved_pattern = false;
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }

    filling_cs_key = "";
    rgb_filling_ops = gray_to_rgb(instructions[0].to_double());
  }

  void pdf_state<GRPH>::RG(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not verify(instructions, 3, __FUNCTION__) ) { return; }

    int r = static_cast<int>(std::round(255.0*instructions[0].to_double()));
    int g = static_cast<int>(std::round(255.0*instructions[1].to_double()));
    int b = static_cast<int>(std::round(255.0*instructions[2].to_double()));

    stroking_cs_key = "";
    rgb_stroking_ops = {r, g, b};
  }

  void pdf_state<GRPH>::rg(std::vector<qpdf_stream_instruction>& instructions)
  {
    fill_is_unresolved_pattern = false;
    if(not verify(instructions, 3, __FUNCTION__) ) { return; }

    int r = static_cast<int>(std::round(255.0*instructions[0].to_double()));
    int g = static_cast<int>(std::round(255.0*instructions[1].to_double()));
    int b = static_cast<int>(std::round(255.0*instructions[2].to_double()));

    filling_cs_key = "";
    rgb_filling_ops = {r, g, b};
  }

  void pdf_state<GRPH>::K(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not verify(instructions, 4, __FUNCTION__) ) { return; }

    stroking_cs_key = "";
    rgb_stroking_ops = cmyk_to_rgb(instructions[0].to_double(),
                                   instructions[1].to_double(),
                                   instructions[2].to_double(),
                                   instructions[3].to_double());
  }

  void pdf_state<GRPH>::k(std::vector<qpdf_stream_instruction>& instructions)
  {
    fill_is_unresolved_pattern = false;
    if(not verify(instructions, 4, __FUNCTION__) ) { return; }

    filling_cs_key = "";
    rgb_filling_ops = cmyk_to_rgb(instructions[0].to_double(),
                                  instructions[1].to_double(),
                                  instructions[2].to_double(),
                                  instructions[3].to_double());
  }
  
}

#endif
