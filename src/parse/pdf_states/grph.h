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
              pdf_resource<PAGE_GRPHS>& page_grphs_);

    pdf_state(const pdf_state<GRPH>& other);
    
    ~pdf_state();

    pdf_state<GRPH>& operator=(const pdf_state<GRPH>& other);

    // General graphics state [Table 57 – Graphics State Operators p 127/135]

    void w(std::vector<qpdf_instruction>& instructions);
    void J(std::vector<qpdf_instruction>& instructions);
    void j(std::vector<qpdf_instruction>& instructions);
    void M(std::vector<qpdf_instruction>& instructions);

    void d(std::vector<qpdf_instruction>& instructions);
    void ri(std::vector<qpdf_instruction>& instructions);
    void i(std::vector<qpdf_instruction>& instructions);
    void gs(std::vector<qpdf_instruction>& instructions);
    
    // color-scheme Table 74 – Colour Operators [p 171/171]

    void CS(std::vector<qpdf_instruction>& instructions);
    void cs(std::vector<qpdf_instruction>& instructions);

    void SC(std::vector<qpdf_instruction>& instructions);
    void SCN(std::vector<qpdf_instruction>& instructions);

    void sc(std::vector<qpdf_instruction>& instructions);
    void scn(std::vector<qpdf_instruction>& instructions);
    
    void G(std::vector<qpdf_instruction>& instructions);
    void g(std::vector<qpdf_instruction>& instructions);

    void RG(std::vector<qpdf_instruction>& instructions);
    void rg(std::vector<qpdf_instruction>& instructions);

    void K(std::vector<qpdf_instruction>& instructions);
    void k(std::vector<qpdf_instruction>& instructions);

    // shading

    void sh(std::vector<qpdf_instruction>& instructions);

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

  private:

    bool verify(std::vector<qpdf_instruction>& instructions,
		std::size_t num_instr, std::string name);
    
  private:
    
    std::array<double, 9>& trafo_matrix;

    pdf_resource<PAGE_GRPHS>& page_grphs;
    
    std::string null_grph_key;
    std::string curr_grph_key;

    double line_width;
    double miter_limit;

    int line_cap;
    int line_join;

    double              dash_phase;
    std::vector<double> dash_array;

    double flatness;

    std::array<int, 3> rgb_stroking_ops;
    std::array<int, 3> rgb_filling_ops;
  };

  pdf_state<GRPH>::pdf_state(std::array<double, 9>&    trafo_matrix_,
                             pdf_resource<PAGE_GRPHS>& page_grphs_):
    trafo_matrix(trafo_matrix_),

    page_grphs(page_grphs_),
    
    null_grph_key("null"),
    curr_grph_key(null_grph_key),

    line_width(-1),
    miter_limit(-1),

    line_cap(-1),
    line_join(-1),

    dash_phase(0),
    dash_array({}),

    flatness(-1),
    
    rgb_stroking_ops({0,0,0}),
    rgb_filling_ops({0,0,0})
  {}

  pdf_state<GRPH>::pdf_state(const pdf_state<GRPH>& other):
    trafo_matrix(other.trafo_matrix),
    page_grphs(other.page_grphs)
  {
    *this = other;
  }

  pdf_state<GRPH>::~pdf_state()
  {}

  pdf_state<GRPH>& pdf_state<GRPH>::operator=(const pdf_state<GRPH>& other)    
  {
    this->curr_grph_key = other.curr_grph_key;

    this->line_width = other.line_width;
    this->miter_limit = other.miter_limit;

    this->line_cap = other.line_cap;
    this->line_join = other.line_join;

    this->dash_phase = other.dash_phase;
    this->dash_array = other.dash_array;

    this->flatness = other.flatness;

    this->rgb_stroking_ops = other.rgb_stroking_ops;
    this->rgb_filling_ops = other.rgb_filling_ops;

    return *this;
  }

  bool pdf_state<GRPH>::verify(std::vector<qpdf_instruction>& instructions,
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
  
  void pdf_state<GRPH>::w(std::vector<qpdf_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    line_width = instructions[0].to_double();
  }

  void pdf_state<GRPH>::J(std::vector<qpdf_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    line_cap = instructions[0].to_int();
  }

  void pdf_state<GRPH>::j(std::vector<qpdf_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    line_join = instructions[0].to_int();
  }

  void pdf_state<GRPH>::M(std::vector<qpdf_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    miter_limit = instructions[0].to_double();
  }
  
  // Table 56 – Examples of Line Dash Patterns [p 127/135]
  void pdf_state<GRPH>::d(std::vector<qpdf_instruction>& instructions)
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
		double val = item.getNumericValue();
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

  void pdf_state<GRPH>::ri(std::vector<qpdf_instruction>& instructions)
  {
    LOG_S(WARNING) << "implement " << __FUNCTION__ << ": " << instructions.size();
  }

  void pdf_state<GRPH>::i(std::vector<qpdf_instruction>& instructions)
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

  void pdf_state<GRPH>::gs(std::vector<qpdf_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    std::string key = instructions[0].to_utf8_string();

    if(page_grphs.count(key)>0)
      {
	curr_grph_key = key;
      }
    else
      {
	LOG_S(WARNING) << "key (=" << key << ") not found in page_grphs: " << page_grphs.get().dump(2);
	curr_grph_key = null_grph_key;	
      }
  }

  void pdf_state<GRPH>::CS(std::vector<qpdf_instruction>& instructions)
  {
    LOG_S(WARNING) << "implement " << __FUNCTION__ << ": " << instructions.size();
  }

  void pdf_state<GRPH>::cs(std::vector<qpdf_instruction>& instructions)
  {
    LOG_S(WARNING) << "implement " << __FUNCTION__ << ": " << instructions.size();
  }
  
  void pdf_state<GRPH>::SC(std::vector<qpdf_instruction>& instructions)
  {
    LOG_S(WARNING) << "implement " << __FUNCTION__ << ": " << instructions.size();
  }

  void pdf_state<GRPH>::SCN(std::vector<qpdf_instruction>& instructions)
  {
    LOG_S(WARNING) << "implement " << __FUNCTION__ << ": " << instructions.size();
  }
  
  void pdf_state<GRPH>::sc(std::vector<qpdf_instruction>& instructions)
  {
    LOG_S(WARNING) << "implement " << __FUNCTION__ << ": " << instructions.size();
  }

  void pdf_state<GRPH>::scn(std::vector<qpdf_instruction>& instructions)
  {
    LOG_S(WARNING) << "implement " << __FUNCTION__ << ": " << instructions.size();
  }
  
  void pdf_state<GRPH>::G(std::vector<qpdf_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    int r = std::round(255.0*instructions[0].to_double());
    int g = std::round(255.0*instructions[0].to_double());
    int b = std::round(255.0*instructions[0].to_double());

    //LOG_S(INFO) << "rgb: {" << r << ", " << g << ", " << b << "}";

    rgb_stroking_ops = {r, g, b};
  }

  void pdf_state<GRPH>::g(std::vector<qpdf_instruction>& instructions)
  {
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    int r = std::round(255.0*instructions[0].to_double());
    int g = std::round(255.0*instructions[0].to_double());
    int b = std::round(255.0*instructions[0].to_double());

    //LOG_S(INFO) << "rgb: {" << r << ", " << g << ", " << b << "}";

    rgb_stroking_ops = {r, g, b};
  }
  
  void pdf_state<GRPH>::RG(std::vector<qpdf_instruction>& instructions)
  {
    if(not verify(instructions, 3, __FUNCTION__) ) { return; }
    
    int r = std::round(255.0*instructions[0].to_double());
    int g = std::round(255.0*instructions[1].to_double());
    int b = std::round(255.0*instructions[2].to_double());

    LOG_S(INFO) << "rgb: {" << r << ", " << g << ", " << b << "}";

    rgb_stroking_ops = {r, g, b};
  }

  void pdf_state<GRPH>::rg(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==3);
    if(not verify(instructions, 3, __FUNCTION__) ) { return; }
    
    int r = std::round(255.0*instructions[0].to_double());
    int g = std::round(255.0*instructions[1].to_double());
    int b = std::round(255.0*instructions[2].to_double());

    LOG_S(INFO) << "rgb: {" << r << ", " << g << ", " << b << "}";

    rgb_filling_ops = {r, g, b};
  }
  
  void pdf_state<GRPH>::K(std::vector<qpdf_instruction>& instructions)
  {
    if(not verify(instructions, 4, __FUNCTION__) ) { return; }
    
    double c = instructions[0].to_double();
    double m = instructions[1].to_double();
    double y = instructions[2].to_double();
    double k = instructions[3].to_double();

    int r = std::round(255.0 * (1.0 - c) * (1.0 - k));
    int g = std::round(255.0 * (1.0 - m) * (1.0 - k));
    int b = std::round(255.0 * (1.0 - y) * (1.0 - k));

    LOG_S(INFO) << "rgb: {" << r << ", " << g << ", " << b << "}";

    rgb_stroking_ops = {r, g, b};
  }

  void pdf_state<GRPH>::k(std::vector<qpdf_instruction>& instructions)
  {
    if(not verify(instructions, 4, __FUNCTION__) ) { return; }
    
    double c = instructions[0].to_double();
    double m = instructions[1].to_double();
    double y = instructions[2].to_double();
    double k = instructions[3].to_double();

    int r = std::round(255.0 * (1.0 - c) * (1.0 - k));
    int g = std::round(255.0 * (1.0 - m) * (1.0 - k));
    int b = std::round(255.0 * (1.0 - y) * (1.0 - k));

    LOG_S(INFO) << "rgb: {" << r << ", " << g << ", " << b << "}";

    rgb_filling_ops = {r, g, b};
  }
  
}

#endif
