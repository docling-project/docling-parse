//-*-C++-*-

#ifndef PDF_CELLS_SANITATOR_H
#define PDF_CELLS_SANITATOR_H

namespace pdflib
{

  template<>
  class pdf_sanitator<PAGE_CELLS>
  {
  public:
    
    pdf_sanitator();
    ~pdf_sanitator();

    void remove_duplicate_chars(pdf_resource<PAGE_CELLS>& cells, double eps=1.0e-1);
    
    void sanitize_bbox(pdf_resource<PAGE_CELLS>& cells,
		       double horizontal_cell_tolerance, //=1.0,
		       bool enforce_same_font, //=true,
		       double space_width_factor_for_merge, //=1.5,
		       double space_width_factor_for_merge_with_space); //=0.33);

    void sanitize_text(pdf_resource<PAGE_CELLS>& cells);
    
  private:

    // linear
    void contract_cells_into_lines_v1(pdf_resource<PAGE_CELLS>& cells,
				      double horizontal_cell_tolerance=1.0,
				      bool enforce_same_font=true,
				      double space_width_factor_for_merge=1.5,
				      double space_width_factor_for_merge_with_space=0.33);

    // quadratic
    void contract_cells_into_lines_v2(pdf_resource<PAGE_CELLS>& cells,
				      double horizontal_cell_tolerance=1.0,
				      bool enforce_same_font=true,
				      double space_width_factor_for_merge=1.5,
				      double space_width_factor_for_merge_with_space=0.33);
    
  private:

  };

  pdf_sanitator<PAGE_CELLS>::pdf_sanitator()
  {}
  
  pdf_sanitator<PAGE_CELLS>::~pdf_sanitator()
  {}

  void pdf_sanitator<PAGE_CELLS>::remove_duplicate_chars(pdf_resource<PAGE_CELLS>& cells, double eps)
  {
    while(true)
      {
        bool erased_cell=false;
        
        for(int i=0; i<cells.size(); i++)
          {
	    if(not cells[i].active)
	      {
		continue;
	      }

	    for(int j=i+1; j<cells.size(); j++)
	      {
		if(not cells[j].active)
		  {
		    continue;
		  }

		if(cells[i].font_name==cells[j].font_name and
		   cells[i].text==cells[j].text and
		   utils::values::distance(cells[i].r_x0, cells[i].r_y0, cells[j].r_x0, cells[j].r_y0)<eps and
		   utils::values::distance(cells[i].r_x1, cells[i].r_y1, cells[j].r_x1, cells[j].r_y1)<eps and
		   utils::values::distance(cells[i].r_x2, cells[i].r_y2, cells[j].r_x2, cells[j].r_y2)<eps and
		   utils::values::distance(cells[i].r_x3, cells[i].r_y3, cells[j].r_x3, cells[j].r_y3)<eps)
		  {
		    LOG_S(WARNING) << "removing duplicate char with text: '" << cells[j].text << "' "
				   << "with r_0: (" << cells[i].r_x0 << ", " << cells[i].r_y0 << ") "
				   << "with r_2: (" << cells[i].r_x2 << ", " << cells[i].r_y2 << ") "
				   << "with r'_0: (" << cells[j].r_x0 << ", " << cells[j].r_y0 << ") "
				   << "with r'_2: (" << cells[j].r_x2 << ", " << cells[j].r_y2 << ") ";

		    
		    cells[j].active = false;
		    erased_cell = true;		    
		  }		
	      }
	  }
	
	if(not erased_cell)
	  {
	    break;
	  }
      }

    pdf_resource<PAGE_CELLS> cells_;
    for(int i=0; i<cells.size(); i++)
      {
	if(cells[i].active)
	  {
	    cells_.push_back(cells[i]);
	  }
      }

    cells = cells_;        
  }

  void pdf_sanitator<PAGE_CELLS>::sanitize_text(pdf_resource<PAGE_CELLS>& cells)
  {
    for(int i=0; i<cells.size(); i++)
      {
	std::string& text = cells.at(i).text;

	for(const std::pair<std::string, std::string>& pair:text_constants::replacements)
	  {
	    utils::string::replace(text, pair.first, pair.second);
	  }
      }

    {
      std::regex pattern(R"(^\/([A-Za-z])_([A-Za-z])(_([A-Za-z]))?$)");

      for(int i=0; i<cells.size(); i++)
	{
	  std::string text = cells.at(i).text;
	  
	  std::smatch match;
	  if(std::regex_match(text, match, pattern))
	    {
	      std::string replacement = match[1].str() + match[2].str();
	      if(match[3].matched)
		{
		  replacement += match[4].str();
		}
	      
	      LOG_S(WARNING) << "replacing `" << text << "` with `" << replacement << "`";	    
	      cells.at(i).text = replacement;
	    }
	}      
    }
  }
  
  void pdf_sanitator<PAGE_CELLS>::sanitize_bbox(pdf_resource<PAGE_CELLS>& cells,
						double horizontal_cell_tolerance,
						bool enforce_same_font,
						double space_width_factor_for_merge,
						double space_width_factor_for_merge_with_space)
  {
    contract_cells_into_lines_v1(cells,
				 horizontal_cell_tolerance,
				 enforce_same_font,
				 space_width_factor_for_merge,
				 space_width_factor_for_merge_with_space);
  }

  void pdf_sanitator<PAGE_CELLS>::contract_cells_into_lines_v1(pdf_resource<PAGE_CELLS>& cells,
							       double horizontal_cell_tolerance,
							       bool enforce_same_font,
							       double space_width_factor_for_merge,
							       double space_width_factor_for_merge_with_space)
  {
    // take care for left to right printing
    for(int i=0; i<cells.size(); i++)
      {
	if(not cells[i].active)
	  {
	    continue;
	  }
	LOG_S(INFO) << "start merging cell-" << i << ": '" << cells[i].text << "'";

	for(int j=i+1; j<cells.size(); j++)
	  {
	    if(not cells[j].active)
	      {
		break;
	      }
	    
	    if(enforce_same_font and cells[i].font_name!=cells[j].font_name)
	      {
		break;
	      }
	    
	    if(not cells[i].has_same_reading_orientation(cells[j]))
	      {
		break;
	      }
	    
	    double delta_0 = cells[i].average_char_width()*space_width_factor_for_merge;
	    double delta_1 = cells[i].average_char_width()*space_width_factor_for_merge_with_space;
	    
	    if(cells[i].is_adjacent_to(cells[j], delta_0))
	      {
		cells[i].merge_with(cells[j], delta_1);
		
		cells[j].active = false;
		LOG_S(INFO) << " -> merging cell-" << i << " with " << j << " '" << cells[j].text << "'"<< ": " << cells[i].text;
	      }
	    else
	      {
		break;
	      }
	  }
      }

    // take care for right to left printing
    for(int i=cells.size()-1; i>=0; i--)
      {
	if(not cells[i].active)
	  {
	    continue;
	  }
	LOG_S(INFO) << "start merging cell-" << i << ": '" << cells[i].text << "'";

	for(int j=i-1; j>=0; j--)
	  {
	    if(not cells[j].active)
	      {
		break;
	      }
	    
	    if(enforce_same_font and cells[i].font_name!=cells[j].font_name)
	      {
		break;
	      }
	    
	    if(not cells[i].has_same_reading_orientation(cells[j]))
	      {
		break;
	      }
	    
	    double delta_0 = cells[i].average_char_width()*space_width_factor_for_merge;
	    double delta_1 = cells[i].average_char_width()*space_width_factor_for_merge_with_space;
	    
	    if(cells[j].is_adjacent_to(cells[i], delta_0))
	      {
		cells[i].merge_with(cells[j], delta_1);
		
		cells[i].active = false;
		LOG_S(INFO) << " -> merging cell-" << i << " with " << j << " '" << cells[j].text << "'"<< ": " << cells[i].text;
	      }
	    else
	      {
		break;
	      }
	  }
      }    

    pdf_resource<PAGE_CELLS> cells_;
    for(int i=0; i<cells.size(); i++)
      {
	if(cells[i].active)
	  {
	    cells_.push_back(cells[i]);
	  }
      }

    cells = cells_;    
  }

  void pdf_sanitator<PAGE_CELLS>::contract_cells_into_lines_v2(pdf_resource<PAGE_CELLS>& cells,
							       double horizontal_cell_tolerance,
							       bool enforce_same_font,
							       double space_width_factor_for_merge,
							       double space_width_factor_for_merge_with_space)
  {
    while(true)
      {
        bool erased_cell=false;
        
        for(int i=0; i<cells.size(); i++)
          {
	    if(not cells[i].active)
	      {
		continue;
	      }
	    LOG_S(INFO) << "start merging cell-" << i << ": '" << cells[i].text << "'";
	    
	    for(int j=i+1; j<cells.size(); j++)
	      {
		if(not cells[j].active)
		  {
		    continue;
		  }

		if(enforce_same_font and cells[i].font_name!=cells[j].font_name)
		  {
		    continue;
		  }

		if(not cells[i].has_same_reading_orientation(cells[j]))
		  {
		    continue;
		  }
		
		double delta_0 = cells[i].average_char_width()*space_width_factor_for_merge;
		double delta_1 = cells[i].average_char_width()*space_width_factor_for_merge_with_space;
		
		if(cells[i].is_adjacent_to(cells[j], delta_0))
		  {
		    cells[i].merge_with(cells[j], delta_1);

		    cells[j].active = false;
		    erased_cell = true;

		    LOG_S(INFO) << " -> merging cell-" << i << " with " << j << " '" << cells[j].text << "'"<< ": " << cells[i].text;
		  }		
	      }
	  }
	
	if(not erased_cell)
	  {
	    break;
	  }
      }

    pdf_resource<PAGE_CELLS> cells_;
    for(int i=0; i<cells.size(); i++)
      {
	if(cells[i].active)
	  {
	    cells_.push_back(cells[i]);
	  }
      }

    cells = cells_;    
  }
  
}

#endif
