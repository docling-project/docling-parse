//-*-C++-*-

#ifndef PDF_GRPH_STATE_H
#define PDF_GRPH_STATE_H

namespace pdflib
{

  template<>
  class pdf_state<GRPH>
  {
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
    std::array<int, 3> rgb_nonstroking_ops;
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
    rgb_nonstroking_ops({0,0,0})
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
    this->rgb_nonstroking_ops = other.rgb_nonstroking_ops;

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
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    line_width = instructions[0].to_double();
  }

  void pdf_state<GRPH>::J(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    line_cap = instructions[0].to_int();
  }

  void pdf_state<GRPH>::j(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    line_join = instructions[0].to_int();
  }

  void pdf_state<GRPH>::M(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    miter_limit = instructions[0].to_double();
  }
  
  // Table 56 – Examples of Line Dash Patterns [p 127/135]
  void pdf_state<GRPH>::d(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==2);
    if(not verify(instructions, 2, __FUNCTION__) ) { return; }
 
    QPDFObjectHandle arr = instructions[0].obj;

    //assert(arr.isArray());
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
    //assert(instructions.size()==1);
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
    //assert(instructions.size()==1);
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
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    int r = std::round(255.0*instructions[0].to_double());
    int g = std::round(255.0*instructions[0].to_double());
    int b = std::round(255.0*instructions[0].to_double());

    //LOG_S(INFO) << "rgb: {" << r << ", " << g << ", " << b << "}";

    rgb_stroking_ops = {r, g, b};
  }

  void pdf_state<GRPH>::g(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    int r = std::round(255.0*instructions[0].to_double());
    int g = std::round(255.0*instructions[0].to_double());
    int b = std::round(255.0*instructions[0].to_double());

    //LOG_S(INFO) << "rgb: {" << r << ", " << g << ", " << b << "}";

    rgb_stroking_ops = {r, g, b};
  }
  
  void pdf_state<GRPH>::RG(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==3);
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

    rgb_nonstroking_ops = {r, g, b};
  }
  
  void pdf_state<GRPH>::K(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==4);
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
    //assert(instructions.size()==4);
    if(not verify(instructions, 4, __FUNCTION__) ) { return; }
    
    double c = instructions[0].to_double();
    double m = instructions[1].to_double();
    double y = instructions[2].to_double();
    double k = instructions[3].to_double();

    int r = std::round(255.0 * (1.0 - c) * (1.0 - k));
    int g = std::round(255.0 * (1.0 - m) * (1.0 - k));
    int b = std::round(255.0 * (1.0 - y) * (1.0 - k));

    LOG_S(INFO) << "rgb: {" << r << ", " << g << ", " << b << "}";

    rgb_nonstroking_ops = {r, g, b};
  }
  
}

#endif
