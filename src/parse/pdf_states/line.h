//-*-C++-*-

#ifndef PDF_LINE_STATE_H
#define PDF_LINE_STATE_H

namespace pdflib
{

  enum clipping_path_mode_type { 
    NO_CLIPPING_PATH_RULE, 
    NONZERO_WINDING_NUMBER_RULE, 
    EVEN_ODD_RULE};

  template<>
  class pdf_state<LINE>
  {
  public:

    pdf_state(std::array<double, 9>&    trafo_matrix_,
              pdf_resource<PAGE_LINES>& page_lines_);

    pdf_state(const pdf_state<LINE>& other);
              
    ~pdf_state();

    pdf_state<LINE>& operator=(const pdf_state<LINE>& other);

    void update(pdf_resource<PAGE_LINES>& lines);

    void m(std::vector<qpdf_instruction>& instructions);
    void l(std::vector<qpdf_instruction>& instructions);

    void c(std::vector<qpdf_instruction>& instructions);
    void v(std::vector<qpdf_instruction>& instructions);
    void y(std::vector<qpdf_instruction>& instructions);

    void h(std::vector<qpdf_instruction>& instructions);
    void re(std::vector<qpdf_instruction>& instructions);
    
    void s(std::vector<qpdf_instruction>& instructions);
    void S(std::vector<qpdf_instruction>& instructions);

    void f(std::vector<qpdf_instruction>& instructions);
    void fStar(std::vector<qpdf_instruction>& instructions);

    void F(std::vector<qpdf_instruction>& instructions);

    void B(std::vector<qpdf_instruction>& instructions);
    void BStar(std::vector<qpdf_instruction>& instructions);

    void b(std::vector<qpdf_instruction>& instructions);
    void bStar(std::vector<qpdf_instruction>& instructions);

    void W(std::vector<qpdf_instruction>& instructions);
    void WStar(std::vector<qpdf_instruction>& instructions);
    
    void n(std::vector<qpdf_instruction>& instructions);

  private:

    bool verify(std::vector<qpdf_instruction>& instructions,
		std::size_t num_instr, std::string name);
    
    bool keep_line(pdf_resource<PAGE_LINE>& line);

    void close_last_path();

    void register_paths();
   
    void m(double x, double y);
    void l(double x, double y);

    void h();
    void re(double x, double y, 
            double w, double h);

    void interpolate(pdf_resource<PAGE_LINE>& line,
                     double x0, double y0, 
                     double x1, double y1, 
                     double x2, double y2, 
                     double x3, double y3,
                     int N=8);

  private:

    std::array<double, 9>&    trafo_matrix;

    pdf_resource<PAGE_LINES>& page_lines;
    pdf_resource<PAGE_LINES>  curr_lines;

    pdf_resource<PAGE_LINES>  clippings;

    clipping_path_mode_type clipping_path_mode;
  };

  pdf_state<LINE>::pdf_state(std::array<double, 9>&    trafo_matrix_,
                             pdf_resource<PAGE_LINES>& page_lines_):
    trafo_matrix(trafo_matrix_),

    page_lines(page_lines_),

    curr_lines(),
    clippings(),

    clipping_path_mode(NO_CLIPPING_PATH_RULE)
  {
    //LOG_S(INFO) << "pdf_state<LINE>";
  }

  pdf_state<LINE>::pdf_state(const pdf_state<LINE>& other):
    trafo_matrix(other.trafo_matrix),

    page_lines(other.page_lines)
  {
    *this = other;
  }

  pdf_state<LINE>::~pdf_state()
  {
    if(curr_lines.size()>0 and curr_lines[0].size()>0)
      {
	//LOG_S(ERROR) << "~pdf_state<LINE>: " << curr_lines.size();
	
	for(int i=0; i<curr_lines.size(); i++)
	  {
	    curr_lines[i].transform(trafo_matrix);

	    /*
	    LOG_S(INFO) << "line-" << i << " --> len: " << curr_lines[i].size(); 
	    for(int j=0; j<curr_lines[i].size(); j++)
	      {
		LOG_S(INFO) << "\t(" 
			    << curr_lines[i][j].first << ", " 
			    << curr_lines[i][j].second << ")";
	      }
	    */
	  }
      }
  }

  pdf_state<LINE>& pdf_state<LINE>::operator=(const pdf_state<LINE>& other)    
  {
    this->curr_lines = other.curr_lines;
    this->clippings  = other.clippings;

    return *this;
  }

  bool pdf_state<LINE>::verify(std::vector<qpdf_instruction>& instructions,
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
  
  void pdf_state<LINE>::m(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==2);
    if(not verify(instructions, 2, __FUNCTION__) ) { return; }
    
    double x = instructions[0].to_double();
    double y = instructions[1].to_double();

    this->m(x,y);
  }

  void pdf_state<LINE>::l(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==2);
    if(not verify(instructions, 2, __FUNCTION__) ) { return; }
    
    double x = instructions[0].to_double();
    double y = instructions[1].to_double();

    this->l(x,y);
  }

  void pdf_state<LINE>::c(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==6);
    if(not verify(instructions, 6, __FUNCTION__) ) { return; }
    
    /*
    if(curr_lines.size()==0)
      {
        LOG_S(ERROR) << "applying 'c' on empty lines";
        return;
      }
    */

    auto& line = curr_lines.back();

    /*
    if(line.size()==0)
      {
        LOG_S(ERROR) << "applying 'c' on empty line";
        return;
      }
    */

    std::pair<double, double> coor = line.back();    

    double x0 = coor.first;
    double y0 = coor.second;

    double x1 = instructions[0].to_double();
    double y1 = instructions[1].to_double();

    double x2 = instructions[2].to_double();
    double y2 = instructions[3].to_double();

    double x3 = instructions[4].to_double();
    double y3 = instructions[5].to_double();

    this->interpolate(line, x0,y0, x1,y1, x2,y2, x3, y3, 8);
  }

  void pdf_state<LINE>::v(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==4);
    if(not verify(instructions, 4, __FUNCTION__) ) { return; }
    
    auto& line = curr_lines.back();
    std::pair<double, double> coor = line.back();    

    double x0 = coor.first;
    double y0 = coor.second;

    double x1 = x0;
    double y1 = y0;

    double x2 = instructions[0].to_double();
    double y2 = instructions[1].to_double();

    double x3 = instructions[2].to_double();
    double y3 = instructions[3].to_double();

    this->interpolate(line, x0,y0, x1,y1, x2,y2, x3, y3, 8);
  }

  void pdf_state<LINE>::y(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==4);
    if(not verify(instructions, 4, __FUNCTION__) ) { return; }
    
    auto& line = curr_lines.back();
    std::pair<double, double> coor = line.back();    

    double x0 = coor.first;
    double y0 = coor.second;

    double x1 = instructions[0].to_double();
    double y1 = instructions[1].to_double();

    double x3 = instructions[2].to_double();
    double y3 = instructions[3].to_double();

    double x2 = x3;
    double y2 = y3;

    this->interpolate(line, x0,y0, x1,y1, x2,y2, x3, y3, 8);
  }

  void pdf_state<LINE>::h(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==0);
    if(not verify(instructions, 0, __FUNCTION__) ) { return; }
    
    this->h();
  }
  
  void pdf_state<LINE>::re(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==4);
    if(not verify(instructions, 4, __FUNCTION__) ) { return; }
    
    double x = instructions[0].to_double();
    double y = instructions[1].to_double();

    double w = instructions[2].to_double();
    double h = instructions[3].to_double();

    this->re(x,y, w,h);
  }

  void pdf_state<LINE>::s(std::vector<qpdf_instruction>& instructions)
  {
    close_last_path();

    register_paths();
  }
 
  void pdf_state<LINE>::S(std::vector<qpdf_instruction>& instructions)
  {
    register_paths();
  }

  void pdf_state<LINE>::f(std::vector<qpdf_instruction>& instructions)
  {
    close_last_path();

    register_paths();
  }

  void pdf_state<LINE>::F(std::vector<qpdf_instruction>& instructions)
  {
    this->f(instructions);
  }

  void pdf_state<LINE>::fStar(std::vector<qpdf_instruction>& instructions)
  {
    close_last_path();

    register_paths();
  }

  void pdf_state<LINE>::B(std::vector<qpdf_instruction>& instructions)
  {
    close_last_path();

    register_paths();
  }

  void pdf_state<LINE>::BStar(std::vector<qpdf_instruction>& instructions)
  {
    close_last_path();

    register_paths();
  }
   
  void pdf_state<LINE>::b(std::vector<qpdf_instruction>& instructions)
  {
    close_last_path();

    register_paths();
  }

  void pdf_state<LINE>::bStar(std::vector<qpdf_instruction>& instructions)
  {
    close_last_path();

    register_paths();
  }

  void pdf_state<LINE>::W(std::vector<qpdf_instruction>& instructions)
  {
    clipping_path_mode = NONZERO_WINDING_NUMBER_RULE;    
  }

  void pdf_state<LINE>::WStar(std::vector<qpdf_instruction>& instructions)
  {
    clipping_path_mode = EVEN_ODD_RULE;
  }

  void pdf_state<LINE>::n(std::vector<qpdf_instruction>& instructions)
  {
    //LOG_S(INFO) << __FUNCTION__ << "\t" << "setting up clippings";

    clippings.clear();

    for(int l=0; l<curr_lines.size(); l++)
      {
        auto& line = curr_lines[l];

        if(line.size()>0)
          {
            clippings.push_back(line);
          }
        else
          {
            LOG_S(WARNING) << "ignoring a line of size 0";
          }
      }

    curr_lines.clear();

    pdf_resource<PAGE_LINE> line;
    curr_lines.push_back(line);
  }
   
  bool pdf_state<LINE>::keep_line(pdf_resource<PAGE_LINE>& line)
  {
    if(line.size()<2)
      {
	return false;
      }

    double d=0;
    for(int l=0; l<line.size()-1; l++)
      {
	auto p0 = line[l+0];
	auto p1 = line[l+1];

	double dx = p0.first-p1.first;
	double dy = p0.second-p1.second;

	d = std::max(d, dx*dx+dy*dy);
      }

    if(d<1.e-3)
      {
	return false;
      }

    return true;
  }

  void pdf_state<LINE>::close_last_path()
  {
    if(curr_lines.size()==0)
      {
        LOG_S(WARNING) << "can not close non-existing line";
        return;
      }

    auto& line = curr_lines.back();

    if(line.size()>0)
      {
        auto front = line.back();
        line.append(front.first, front.second);
      }
    else
      {
        LOG_S(WARNING) << "can not close empty line";
      }
  }
                                             
  void pdf_state<LINE>::register_paths()
  {
    //LOG_S(INFO) << "--------------------------------------------------";
    //LOG_S(INFO) << __FUNCTION__ << "\t #-paths: " << curr_lines.size();

    for(int i=0; i<clippings.size(); i++)
      {
        clippings[i].transform(trafo_matrix);

	/*
	LOG_S(INFO) << "clippings: " << clippings.size(); 
	for(int j=0; j<clippings[i].size(); j++)
	  {
	    LOG_S(INFO) << "\t(" 
			<< clippings[i][j].first << ", " 
			<< clippings[i][j].second << ")";
	  }
	*/
      }

    for(int i=0; i<curr_lines.size(); i++)
      {
        curr_lines[i].transform(trafo_matrix);

	/*
	LOG_S(INFO) << "line-" << i << " --> len: " << curr_lines[i].size(); 
	for(int j=0; j<curr_lines[i].size(); j++)
	  {
	    LOG_S(INFO) << "\t(" 
			<< curr_lines[i][j].first << ", " 
			<< curr_lines[i][j].second << ")";
	  }
	*/
	
	if(keep_line(curr_lines[i]))
	  {
	    //LOG_S(INFO) << " --> keeping line";
	    page_lines.push_back(curr_lines[i]);
	  }
	else
	  {
	    //LOG_S(WARNING) << " --> ignoring line";
	  }
      }
    //LOG_S(INFO) << "--------------------------------------------------";

    curr_lines.clear();
  }

  void pdf_state<LINE>::m(double x, double y)
  {
    pdf_resource<PAGE_LINE> line;
    curr_lines.push_back(line);

    this->l(x,y);
  }

  void pdf_state<LINE>::l(double x, double y)
  {
    auto& line = curr_lines.back();

    line.append(x, y);
  }

  void pdf_state<LINE>::h()
  {
    if(curr_lines.size()==0)
      {
        LOG_S(WARNING) << "applying 'h' on empty lines";
        return;
      }

    // first close
    auto& line = curr_lines.back();

    if(line.size()==0)
      {
        LOG_S(WARNING) << "applying 'h' on empty line";
        return;
      }
    
    std::pair<double, double> coor = line.front();

    line.append(coor.first, coor.second);

    // add new line segment
    pdf_resource<PAGE_LINE> line_; 
    line_.append(coor.first, coor.second);

    curr_lines.push_back(line_);
  }

  void pdf_state<LINE>::re(double x, double y, 
                           double w, double h)
  {
    this->m(x, y);

    this->l(x+w, y);

    this->l(x+w, y+h);

    this->l(x, y+h);

    this->h();
  }

  void pdf_state<LINE>::interpolate(pdf_resource<PAGE_LINE>& line,
                                    double x0, double y0, 
                                    double x1, double y1, 
                                    double x2, double y2, 
                                    double x3, double y3,
                                    int N)
  {
    for(int l=1; l<N; l++)
      {
        double t = l/double(N-1);
        
        double x = (1.-t)*(1.-t)*(1.-t)*x0 + 3.*t*(1.-t)*(1.-t)*x1 + 3.*t*t*(1.-t)*x2 + t*t*t*x3;
        double y = (1.-t)*(1.-t)*(1.-t)*y0 + 3.*t*(1.-t)*(1.-t)*y1 + 3.*t*t*(1.-t)*y2 + t*t*t*y3;
        
        line.append(x, y);
      }
  }

}

#endif
