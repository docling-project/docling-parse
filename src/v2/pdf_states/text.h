//-*-C++-*-

#ifndef PDF_TEXT_STATE_H
#define PDF_TEXT_STATE_H

namespace pdflib
{

  template<>
  class pdf_state<TEXT>
  {
  public:

    pdf_state(std::array<double, 9>&    trafo_matrix_,
              pdf_resource<PAGE_CELLS>& page_cells_,
              pdf_resource<PAGE_FONTS>& page_fonts_);

    ~pdf_state();

    pdf_state(const pdf_state<TEXT>& other);

    pdf_state<TEXT>& operator=(const pdf_state<TEXT>& other);

    void BT();
    void ET();

    void Tc(std::vector<qpdf_instruction>& instructions);

    void Td(std::vector<qpdf_instruction>& instructions);
    void TD(std::vector<qpdf_instruction>& instructions);

    void Td(double tx, double ty);

    void Tf(std::vector<qpdf_instruction>& instructions);

    void Tj(std::vector<qpdf_instruction>& instructions, int stack_cnt);
    void TJ(std::vector<qpdf_instruction>& instructions, int stack_cnt);

    void TL(std::vector<qpdf_instruction>& instructions);
    void TL(double tl);

    void Tm(std::vector<qpdf_instruction>& instructions);

    void Tr(std::vector<qpdf_instruction>& instructions);

    void Ts(std::vector<qpdf_instruction>& instructions);
    void TStar(std::vector<qpdf_instruction>& instructions);

    void Tw(std::vector<qpdf_instruction>& instructions);

    void Tz(std::vector<qpdf_instruction>& instructions);

  private:

    bool verify(std::vector<qpdf_instruction>& instructions,
		std::size_t num_instr, std::string name);
    
    void move_cursor(double tx, double ty);

    std::vector<pdf_resource<PAGE_CELL> > generate_cells(qpdf_instruction instruction,
                                                         int              stack_size);

    std::vector<std::pair<uint32_t, std::string> > analyse_string(qpdf_instruction instruction);

    void add_cell(pdf_resource<PAGE_FONT>& font,
                  std::string text,  double width,
                  int stack_size,
                  std::vector<pdf_resource<PAGE_CELL> >& cells);

    /*
      std::array<double, 4> compute_bbox(double font_descent,
      double font_ascent,
      double width);
    */

    std::array<double, 8> compute_rect(double font_descent,
                                       double font_ascent,
                                       double width);

    std::array<double, 4> compute_bbox(std::array<double, 8>& rect);

  public:

    static int block_count;
    static int instr_count;

    std::array<double, 9>& trafo_matrix;

    pdf_resource<PAGE_CELLS>& page_cells;
    pdf_resource<PAGE_FONTS>& page_fonts;

    std::array<double, 9> text_matrix;
    std::array<double, 9> line_matrix;

    int rendering_mode = -1;
    int object_index   = 0;

    double char_spacing = 0;
    double word_spacing = 0;

    double h_scaling = 1;
    double leading   = 0;
    double rise      = 0;

    std::string font_name = "NULL";
    double      font_size = 1000;
  };

  int pdf_state<TEXT>::block_count = 0;
  int pdf_state<TEXT>::instr_count = 0;

  pdf_state<TEXT>::pdf_state(std::array<double, 9>&    trafo_matrix_,
                             pdf_resource<PAGE_CELLS>& page_cells_,
                             pdf_resource<PAGE_FONTS>& page_fonts_):
    trafo_matrix(trafo_matrix_),

    page_cells(page_cells_),
    page_fonts(page_fonts_)
  {
    text_matrix = {1.0, 0.0, 0.0,
                   0.0, 1.0, 0.0,
                   0.0, 0.0, 1.0};

    line_matrix = {1.0, 0.0, 0.0,
                   0.0, 1.0, 0.0,
                   0.0, 0.0, 1.0};
  }

  pdf_state<TEXT>::pdf_state(const pdf_state<TEXT>& other):
    trafo_matrix(other.trafo_matrix),

    page_cells(other.page_cells),
    page_fonts(other.page_fonts)
  {
    *this = other;

    LOG_S(WARNING) << "pdf_state<TEXT>(const pdf_state<TEXT>& other): font_name " << font_name;
  }

  pdf_state<TEXT>::~pdf_state()
  {
  }

  pdf_state<TEXT>& pdf_state<TEXT>::operator=(const pdf_state<TEXT>& other)
  {
    this->text_matrix = other.text_matrix;
    this->line_matrix = other.line_matrix;

    this->rendering_mode = other.rendering_mode;
    this->object_index = other.object_index;

    this->char_spacing = other.char_spacing;
    this->word_spacing = other.word_spacing;

    this->h_scaling = other.h_scaling;
    this->leading = other.leading;
    this->rise = other.rise;

    this->font_name = other.font_name;
    this->font_size = other.font_size;

    return *this;
  }

  bool pdf_state<TEXT>::verify(std::vector<qpdf_instruction>& instructions,
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
  
  void pdf_state<TEXT>::BT()
  {
    //for(int l=0; l<9; l++)
    //LOG_S(INFO) << "\ttrafo_matrix[" << l << "]\t" << trafo_matrix[l];

    block_count += 1;

    text_matrix = {{1.0, 0.0, 0.0,
                    0.0, 1.0, 0.0,
                    0.0, 0.0, 1.0}};

    line_matrix = {{1.0, 0.0, 0.0,
                    0.0, 1.0, 0.0,
                    0.0, 0.0, 1.0}};
  }

  void pdf_state<TEXT>::ET()
  {
  }

  void pdf_state<TEXT>::Tc(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    char_spacing = instructions[0].to_double();
  }

  void pdf_state<TEXT>::Td(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==2);
    if(not verify(instructions, 2, __FUNCTION__) ) { return; }
    
    double tx = instructions[0].to_double();
    double ty = instructions[1].to_double();

    Td(tx, ty);
  }

  void pdf_state<TEXT>::Td(double tx, double ty)
  {
    text_matrix[6] = tx * line_matrix[0] + ty * line_matrix[3] + line_matrix[6];
    text_matrix[7] = tx * line_matrix[1] + ty * line_matrix[4] + line_matrix[7];

    line_matrix = text_matrix;
  }

  void pdf_state<TEXT>::TD(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==2);
    if(not verify(instructions, 2, __FUNCTION__) ) { return; }
    
    double tx = instructions[0].to_double();
    double ty = instructions[1].to_double();

    this->TL(-ty);
    this->Td(tx, ty);
  }

  void pdf_state<TEXT>::Tf(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==2);
    if(not verify(instructions, 2, __FUNCTION__) ) { return; }
    
    font_name = instructions[0].to_utf8_string();
    font_size = instructions[1].to_double();
    
    if(page_fonts.count(font_name) == 0)
      {
        LOG_S(ERROR) << "unknown page-font: '" << font_name << "'";

	auto font_keys = page_fonts.keys();
	LOG_S(WARNING) << "known fonts: " << font_keys.size();
	for(auto key:font_keys)
	  {
	    LOG_S(WARNING) << " -> font-key: '" << key << "'";
	  }
      }
  }

  void pdf_state<TEXT>::Tj(std::vector<qpdf_instruction>& instructions, int stack_size)
  {
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    instr_count += 1;

    std::vector<pdf_resource<PAGE_CELL> > cells = generate_cells(instructions[0],
                                                                 stack_size);

    for(auto& cell:cells)
      {
        //LOG_S(INFO) << "new-cell: " << cell.text;
        page_cells.push_back(cell);
      }
  }

  void pdf_state<TEXT>::TJ(std::vector<qpdf_instruction>& instructions, int stack_size)
  {
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    instr_count += 1;

    for(auto item : instructions[0].obj.getArrayAsVector())
      {
        if(item.isString())
          {
            qpdf_instruction inst(item);

            std::vector<pdf_resource<PAGE_CELL> > cells = generate_cells(item,
                                                                         stack_size);

            for(auto& cell:cells)
              {
                page_cells.push_back(cell);
              }
          }
        else if(item.isNumber())
          {
            double value = item.getNumericValue();

            double tx = - value / 1000.0 * font_size * h_scaling;
            double ty = 0;

            move_cursor(tx, ty);
          }
        else
          {
            LOG_S(ERROR) << "item is not a string nor a value: "
                         << item.unparse() << " [" << item.getTypeName() << "]"
			 << " -> skipping for now ...";
          }
      }
  }

  void pdf_state<TEXT>::move_cursor(double tx, double ty)
  {
    text_matrix[6] += tx * text_matrix[0] + ty * text_matrix[3];
    text_matrix[7] += tx * text_matrix[1] + ty * text_matrix[4];
  }

  std::vector<pdf_resource<PAGE_CELL> > pdf_state<TEXT>::generate_cells(qpdf_instruction instruction,
                                                                        int              stack_size)
  {
    //LOG_S(INFO) << __FUNCTION__;

    auto& font = page_fonts[font_name];

    std::vector<std::pair<uint32_t, std::string> > items = analyse_string(instruction);

    //for(auto item:items)
    //{
    //LOG_S(INFO) << item.first << "\t" << item.second;
    //}

    double width = 0.0;

    //double font_descent = font.get_descent();
    //double font_ascent  = font.get_ascent();

    std::string text="";

    std::vector<double> widths={};
    std::vector<std::string> chars={};

    std::vector<pdf_resource<PAGE_CELL> > cells={};

    /*
    double space_width=0;
    {
      double w0 = font.get_space_width();
      space_width = (w0 / 1000.0 * font_size * h_scaling);// + (char_spacing+word_spacing)*h_scaling;
    }
    */
    
    for(auto item:items)
      {
        double      width_ = font.get_width(item.first);
        std::string chars_ = font.get_string(item.first);

        /*
	  LOG_S(INFO) << item.first << " --> "
	  << item.second << "\twidth_: "
	  << width_ << "\tchars_: '" << chars_ << "'";
	 */

        double char_width = (width_ / 1000.0 * font_size * h_scaling);

	/*
	  LOG_S(INFO) << "char_width: "
	  << char_width << ", width_: " << width_
	  << ", font_size: " << font_size
	  << ", h_scaling: " << h_scaling;	
	 */
	
        double delta_width=0;
        if(chars_==" ")
          {
            delta_width += (char_spacing+word_spacing)*h_scaling;
          }
        else
          {
            delta_width += char_spacing*h_scaling;
          }

	//LOG_S(INFO) << "delta_width: " << delta_width;

	// this is the old way of adding cells
	/*
        if(delta_width >= space_width)
          {
            //LOG_S(WARNING) << "delta_width (="<<delta_width<<") >= space_width ("<<space_width<<")";

            text  += chars_;
            width += char_width;

            //add_cell(font, text, char_width, stack_size, cells);
            add_cell(font, text, width, stack_size, cells);

            move_cursor(delta_width, 0);

            chars  = {};
            widths = {};

            text  = "";
            width = 0;
          }
        else
          {
            double width_new = char_width + delta_width;

            chars.push_back(chars_);
            widths.push_back(width_new);

            text  += chars_;
            width += width_new;
          }
	*/

	if(true) // adding char by char ...
	  {
            text  += chars_;
            width += char_width;

            //add_cell(font, text, char_width, stack_size, cells);
            add_cell(font, text, width, stack_size, cells);

            move_cursor(delta_width, 0);

            chars  = {};
            widths = {};

            text  = "";
            width = 0;
          }
      }

    //LOG_S(INFO) << "text-line: " << text;
    if(text.size()>0)
      {
        add_cell(font, text, width, stack_size, cells);
      }

    return cells;
  }

  void pdf_state<TEXT>::add_cell(pdf_resource<PAGE_FONT>& font,
                                 std::string text, double width,
                                 int stack_size,
                                 std::vector<pdf_resource<PAGE_CELL> >& cells)
  {
    LOG_S(INFO) << __FUNCTION__ << " with text='" << text << "', width=" << width;

    bool left_to_right = (not utils::string::is_right_to_left(text));
    
    double font_descent = font.get_descent();
    double font_ascent  = font.get_ascent();
    double font_capheight  = font.get_capheight();

    if(left_to_right)
      {
	LOG_S(INFO) << "font_descent: " << font_descent << ", "
		    << "font_ascent: " << font_ascent << ", "
		    << "font_capheight: " << font_capheight << ", "
		    << "capheight/ascent: " << font_capheight/font_ascent << ", "
		    << "left_to_right: " << left_to_right << ", text: " << text;
      }
    else
      {
	LOG_S(WARNING) << "font_descent: " << font_descent << ", "
		       << "font_ascent: " << font_ascent << ", "
		       << "font_capheight: " << font_capheight << ", "
		       << "capheight/ascent: " << font_capheight/font_ascent << ", "
		       << "left_to_right: " << left_to_right << ", text: " << text;
      }
    
    double space_width=0;
    {
      double w0 = font.get_space_width();
      double w1 = (w0 / 1000.0 * font_size * h_scaling);// + (char_spacing+word_spacing)*h_scaling;

      LOG_S(INFO) << __FUNCTION__ << " -> w0: " << w0 << ", w1: " << w1 << ", "
		  << "font_size: " << font_size << ", h_scaling: " << h_scaling;
      
      std::array<double, 8> rect = compute_rect(font_descent, font_ascent, w1);
      space_width = std::sqrt((rect[2]-rect[0])*(rect[2]-rect[0])+
                              (rect[3]-rect[1])*(rect[3]-rect[1]));
    }

    {
      pdf_resource<PAGE_CELL> cell;

      cell.active = true;
      cell.left_to_right = left_to_right;
      
      cell.widget = false;

      double ratio = 1.0;
      if(0.05<=font_capheight/font_ascent and font_capheight/font_ascent<=1.0)
	{
	  ratio = font_capheight/font_ascent;
	}
      
      std::array<double, 8> rect = compute_rect(font_descent*ratio, font_ascent*ratio, width);
      {
        cell.r_x0 = rect[0];
        cell.r_y0 = rect[1];
        cell.r_x1 = rect[2];
        cell.r_y1 = rect[3];
        cell.r_x2 = rect[4];
        cell.r_y2 = rect[5];
        cell.r_x3 = rect[6];
        cell.r_y3 = rect[7];
      }

      std::array<double, 4> bbox = compute_bbox(rect);
      {
        cell.x0 = bbox[0];
        cell.y0 = bbox[1];
        cell.x1 = bbox[2];
        cell.y1 = bbox[3];
      }

      cell.text = text;
      cell.rendering_mode = rendering_mode;

      cell.space_width = space_width;

      cell.enc_name = font.get_encoding_name();

      cell.font_enc = to_string(font.get_encoding());
      cell.font_key = font.get_key();

      cell.font_name = font.get_name();
      cell.font_size = font_size/1000.0;

      cell.italic = false;
      cell.bold   = false;

      cell.ocr        = false;
      cell.confidence = -1.0;

      cell.stack_size  = stack_size;
      cell.block_count = block_count;
      cell.instr_count = instr_count;

      cells.push_back(cell);
    }

    if(rendering_mode==3)
      {
	LOG_S(WARNING) << "Found cell with text-rendering mode 3 (=invisible) "
		       << "with text=" << text;
      }

    move_cursor(width, 0);
  }

  std::vector<std::pair<uint32_t, std::string> > pdf_state<TEXT>::analyse_string(qpdf_instruction instruction)
  {
    LOG_S(INFO) << __FUNCTION__ << " fontname: " << font_name << ", key: " << instruction.key << " => val: " << instruction.val;

    auto& font = page_fonts[font_name];

    font_encoding_name encoding = font.get_encoding();

    std::string values = instruction.to_char_string();
    LOG_S(INFO) << "values: " << values.size() << "\t" << values;

    std::vector<std::pair<uint32_t, std::string> > result;

    if(encoding == IDENTITY_H or
       encoding == IDENTITY_V  ) // 2-byte string
      {
	LOG_S(INFO) << "detected encoding: " << to_string(encoding);

        //assert(values.size()%2==0);

	if((values.size()%2)!=0)
	  {
	    LOG_S(WARNING) << "detected non-even values: potential for incorrect paths!";
	  }
	
        for(int l=0; l<values.size(); l+=2)
          {
	    if(l+1>=values.size())
	      {
		LOG_S(ERROR) <<  "skipping last part of the path";
		continue;
	      }
	    
            //LOG_S(INFO) << "1. value-stream: " << int(static_cast<unsigned char>(values[l+0])) << "\t"
            //<< static_cast<int>(values[l+0]);

            uint32_t    c_0 = static_cast<unsigned char>(values[l+0]);
            std::string v_0 = "";
            v_0 += values[l+0];

            //LOG_S(INFO) << "2. value-stream: " << int(static_cast<unsigned char>(values[l+1])) << "\t"
            //<< static_cast<int>(values[l+1]);

            uint32_t    c_1 = static_cast<unsigned char>(values[l+1]);
            std::string v_1 = "";
            v_1 += values[l+1];

            uint32_t    c = (c_0 << 8) + c_1;
            std::string v = v_0+v_1;

            //LOG_S(INFO) << "final value-stream: " << c;

            std::pair<uint32_t, std::string> item(c,v);
            result.push_back(item);
          }
      }
    else if(encoding == CMAP_RESOURCES)
      {
	LOG_S(INFO) << "detected encoding: " << to_string(encoding);

	int l=0; 

	while(l<values.size())
	  {
	    if(l+2<=values.size())
	      {
		uint32_t c0 = static_cast<unsigned char>(values[l+0]);		
		uint32_t c1 = static_cast<unsigned char>(values[l+1]);		

		uint32_t c = 256*c0+c1;
		
		//LOG_S(INFO) << "c: " << c << "\tc0: " << c0 << "\tc1: " << c1;
		if(font.numb_is_in_cmap(c))
		  {
		    std::string v = values.substr(l, 2);

		    std::pair<uint32_t, std::string> item(c,v);		    
		    result.push_back(item);            

		    l += 2;
		  }
		else
		  {
		    std::string v = values.substr(l, 1);

		    std::pair<uint32_t, std::string> item(c0,v);		    
		    result.push_back(item);            

		    l += 1;
		  }
	      }
	    else if(l+1<=values.size())
	      {
		uint32_t    c = static_cast<unsigned char>(values[l+0]);		
		std::string v = values.substr(l, 1);

		std::pair<uint32_t, std::string> item(c,v);		    
		result.push_back(item);            
		
		l += 1;
	      }
	    else
	      {
		LOG_S(ERROR) << "we should never arrive here: " 
			     << l << "\t" << values.size();
		break;
	      }
	  }
      }
    else
      {
	LOG_S(INFO) << "detected encoding: " << to_string(encoding);

        for(int l=0; l<values.size(); l+=1)
          {
            uint32_t    c = static_cast<unsigned char>(values[l]);
            std::string v = "";
            v += values[l];

            std::pair<uint32_t, std::string> item(c,v);
	    LOG_S(INFO) << item.first << ": " << item.second;
	    
            result.push_back(item);
          }
      }

    return result;
  }

  // We follow the coordinate transformations as indicated in 8.3.4 [p 120], i.e.
  //
  //                       |a b 0|
  //   [x' y' 1] = [x y 1]*|c d 0|
  //                       |e f 1|
  //
  // with a = M[0], b = M[1], c = M[3], d = M[4], e = M[6], f = M[8]
  //
  //  x' = a*x + c*y + e = M[0]*x + M[3]*y + M[6]
  //  y' = b*x + b*y + f = M[1]*x + M[4]*y + M[7]
  std::array<double, 8> pdf_state<TEXT>::compute_rect(double font_descent,
                                                      double font_ascent,
                                                      double width)
  {
    // the bounding-box in the text-glyph reference frame
    std::array<double, 8> g_rect;

    {
      /*x_0*/ g_rect[0] = 0;
      /*y_0*/ g_rect[1] = rise + font_descent / 1000.0 * font_size;

      /*x_1*/ g_rect[2] = width;
      /*y_1*/ g_rect[3] = rise + font_descent / 1000.0 * font_size;

      /*x_2*/ g_rect[4] = width;
      /*y_2*/ g_rect[5] = rise + font_ascent / 1000.0 * font_size;

      /*x_3*/ g_rect[6] = 0;
      /*y_3*/ g_rect[7] = rise + font_ascent / 1000.0 * font_size;

      //LOG_S(INFO) << "text-bbox: [" << tbox[0] << ", " << tbox[1] << ", " << tbox[2] << ", " << tbox[3] << "]";
    }

    // apply the text-matrix
    std::array<double, 8> t_rect;
    {
      std::array<double, 9>& T = text_matrix;

      /*
        LOG_S(INFO) << "text-matrix: ";
        for(int k=0; k<3; k++)
        {
        std::stringstream ss;
        for(int l=0; l<3; l++)
        ss << "\t" << T[k*3+l];

        LOG_S(INFO) << ss.str();
        }
      */

      for(int d=0; d<4; d++)
        {
          /*x*/ t_rect[0+2*d] = T[0]*g_rect[0+2*d] + T[3]*g_rect[1+2*d] + T[6];
          /*y*/ t_rect[1+2*d] = T[1]*g_rect[0+2*d] + T[4]*g_rect[1+2*d] + T[7];
        }

      //LOG_S(INFO) << "u-bbox: [" << ubox[0] << ", " << ubox[1] << ", " << ubox[2] << ", " << ubox[3] << "]";
    }

    // apply the current transformation matrix (CTM)
    std::array<double, 8> d_rect;
    {
      std::array<double, 9>& T = trafo_matrix;

      /*
        LOG_S(INFO) << "ctm-matrix: ";
        for(int k=0; k<3; k++)
        {
        std::stringstream ss;
        for(int l=0; l<3; l++)
        ss << "\t" << T[k*3+l];

        LOG_S(INFO) << ss.str();
        }
      */

      for(int d=0; d<4; d++)
        {
          /*x*/ d_rect[0+2*d] = T[0]*t_rect[0+2*d] + T[3]*t_rect[1+2*d] + T[6];
          /*y*/ d_rect[1+2*d] = T[1]*t_rect[0+2*d] + T[4]*t_rect[1+2*d] + T[7];
        }

      //LOG_S(INFO) << "d-bbox: [" << dbox[0] << ", " << dbox[1] << ", " << dbox[2] << ", " << dbox[3] << "]";
    }

    return d_rect;
  }

  std::array<double, 4> pdf_state<TEXT>::compute_bbox(std::array<double, 8>& rect)
  {
    std::array<double, 4> bbox;
    {
      bbox[0] = rect[0];
      bbox[1] = rect[1];

      bbox[2] = rect[0];
      bbox[3] = rect[1];
    }

    for(int d=0; d<4; d++)
      {
        bbox[0] = std::min(bbox[0], rect[0+2*d]);
        bbox[1] = std::min(bbox[1], rect[1+2*d]);

        bbox[2] = std::max(bbox[2], rect[0+2*d]);
        bbox[3] = std::max(bbox[3], rect[1+2*d]);
      }

    return bbox;
  }

  void pdf_state<TEXT>::TL(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    double tl = instructions[0].to_double();

    TL(tl);
  }

  void pdf_state<TEXT>::TL(double tl)
  {
    leading = tl;
  }

  void pdf_state<TEXT>::Tm(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==6);
    if(not verify(instructions, 6, __FUNCTION__) ) { return; }
    
    double a = instructions[0].to_double();
    double b = instructions[1].to_double();

    double c = instructions[2].to_double();
    double d = instructions[3].to_double();

    double e = instructions[4].to_double();
    double f = instructions[5].to_double();

    text_matrix[0] = a;
    text_matrix[1] = b;
    text_matrix[3] = c;
    text_matrix[4] = d;
    text_matrix[6] = e;
    text_matrix[7] = f;

    line_matrix[0] = a;
    line_matrix[1] = b;
    line_matrix[3] = c;
    line_matrix[4] = d;
    line_matrix[6] = e;
    line_matrix[7] = f;
  }

  /*
    Table 106 – Text rendering modes [p 246]

    0 Fill text.
    1 Stroke text.
    2 Fill, then stroke text.
    3 Neither fill nor stroke text (invisible).
    4 Fill text and add to path for clipping (see 9.3.6, "Text Rendering Mode,").
    5 Stroke text and add to path for clipping.
  */
  void pdf_state<TEXT>::Tr(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    int mode = instructions[0].to_int();

    rendering_mode = mode;
  }

  void pdf_state<TEXT>::Ts(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    rise = instructions[0].to_double();
  }

  void pdf_state<TEXT>::TStar(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==0);
    if(not verify(instructions, 0, __FUNCTION__) ) { return; }
    
    this->Td(0, -leading);
  }

  void pdf_state<TEXT>::Tw(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    double tw = instructions[0].to_double();

    word_spacing = tw;
  }

  // section 9.3.4 [p 258]
  void pdf_state<TEXT>::Tz(std::vector<qpdf_instruction>& instructions)
  {
    //assert(instructions.size()==1);
    if(not verify(instructions, 1, __FUNCTION__) ) { return; }
    
    double th = instructions[0].to_double();

    h_scaling = th / 100.0;
  }

}

#endif
