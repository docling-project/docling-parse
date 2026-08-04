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
      
      ### Shading
      
      - `sh` — Paint shading pattern  
      Paints a shading (e.g., gradient/mesh) defined in the Shading resource dictionary.  
      Operands: `shadingName`  
      Notes: the shading defines its own color space and how colors are computed.      
     */
    
  public:

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

    // shading

    void sh(std::vector<qpdf_stream_instruction>& instructions);

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

    // constant alpha from ExtGState (/CA, /ca); 1.0 = opaque
    double get_stroke_alpha() const { return stroke_alpha; }
    double get_fill_alpha() const { return fill_alpha; }

  private:

    bool verify(std::vector<qpdf_stream_instruction>& instructions,
		std::size_t num_instr, std::string name);

    // Color mapping for SC/sc/SCN/scn: when CS/cs resolved the current
    // color space from the /ColorSpace resources, map the operands through
    // it; otherwise interpret the numeric operands by their count
    // (1 -> gray, 3 -> RGB, 4 -> CMYK). Returns false and leaves rgb
    // untouched for pattern-name operands or unsupported counts.
    bool set_color(std::vector<qpdf_stream_instruction>& instructions,
                   std::array<int, 3>& rgb,
                   const std::string& cs_key,
                   std::string name);

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
    fill_alpha(1.0)
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

    return *this;
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
	LOG_S(ERROR) << "#-instructions " << instructions.size()
		     << " exceeds expected value " << num_instr << " for "
		     << name;
	LOG_S(ERROR) << " => we can continue but might have incorrect results!";
	
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
    int r = static_cast<int>(std::round(255.0 * (1.0 - c) * (1.0 - k)));
    int g = static_cast<int>(std::round(255.0 * (1.0 - m) * (1.0 - k)));
    int b = static_cast<int>(std::round(255.0 * (1.0 - y) * (1.0 - k)));
    return {r, g, b};
  }

  bool pdf_state<GRPH>::set_color(std::vector<qpdf_stream_instruction>& instructions,
                                  std::array<int, 3>& rgb,
                                  const std::string& cs_key,
                                  std::string name)
  {
    std::vector<double> comps;
    for(auto& instr : instructions)
      {
        if(instr.is_number())
          {
            comps.push_back(instr.to_double());
          }
        else // pattern name operand (SCN/scn) or unexpected type
          {
            LOG_S(WARNING) << name << ": non-numeric operand "
                           << instr.unparse() << ", keeping current color";
            return false;
          }
      }

    // resolved /ColorSpace resource from CS/cs (ICCBased, Indexed, ...)
    if(cs_key.size()>0 and
       page_colorspaces!=nullptr and
       page_colorspaces->count(cs_key)>0)
      {
        if((*page_colorspaces)[cs_key].map_to_rgb(comps, rgb))
          {
            return true;
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
          return true;
        }
      case 3:
        {
          rgb = {static_cast<int>(std::round(255.0*comps[0])),
                 static_cast<int>(std::round(255.0*comps[1])),
                 static_cast<int>(std::round(255.0*comps[2]))};
          return true;
        }
      case 4:
        {
          rgb = cmyk_to_rgb(comps[0], comps[1], comps[2], comps[3]);
          return true;
        }
      default:
        {
          LOG_S(WARNING) << name << ": unsupported number of color components ("
                         << comps.size() << "), keeping current color";
          return false;
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

  void pdf_state<GRPH>::gs(std::vector<qpdf_stream_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    std::string key = instructions[0].to_utf8_string();

    if(page_grphs->count(key)>0)
      {
	curr_grph_key = key;

	// a gs operator only changes the parameters present in its
	// ExtGState dictionary
	auto& grph = (*page_grphs)[key];

	if(grph.has_stroke_alpha())
	  {
	    stroke_alpha = grph.get_stroke_alpha();
	  }

	if(grph.has_fill_alpha())
	  {
	    fill_alpha = grph.get_fill_alpha();
	  }
      }
    else
      {
	LOG_S(WARNING) << "key (=" << key << ") not found in page_grphs: " << page_grphs->get().dump(2);
	curr_grph_key = null_grph_key;
      }
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
        // pattern paint is unsupported; keep the current color
      }
    else if(page_colorspaces!=nullptr and page_colorspaces->count("/"+cs_name)>0)
      {
        cs_key = "/"+cs_name;
        rgb = {0, 0, 0};
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
    set_color(instructions, rgb_stroking_ops, stroking_cs_key, __FUNCTION__);
  }

  void pdf_state<GRPH>::sc(std::vector<qpdf_stream_instruction>& instructions)
  {
    set_color(instructions, rgb_filling_ops, filling_cs_key, __FUNCTION__);
  }

  void pdf_state<GRPH>::scn(std::vector<qpdf_stream_instruction>& instructions)
  {
    set_color(instructions, rgb_filling_ops, filling_cs_key, __FUNCTION__);
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
    if(not verify(instructions, 4, __FUNCTION__) ) { return; }

    filling_cs_key = "";
    rgb_filling_ops = cmyk_to_rgb(instructions[0].to_double(),
                                  instructions[1].to_double(),
                                  instructions[2].to_double(),
                                  instructions[3].to_double());
  }
  
}

#endif
