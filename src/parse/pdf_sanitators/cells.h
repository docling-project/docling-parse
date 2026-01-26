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

    nlohmann::json to_records(pdf_resource<PAGE_CELLS>& cells);

    pdf_resource<PAGE_CELLS> create_word_cells(pdf_resource<PAGE_CELLS>& cells,
					       double horizontal_cell_tolerance, //=1.00,
					       bool enforce_same_font, //=true,
					       double space_width_factor_for_merge); //=0.05);

    pdf_resource<PAGE_CELLS> create_line_cells(pdf_resource<PAGE_CELLS>& cells,
					       double horizontal_cell_tolerance, //=1.00,
					       bool enforce_same_font, //=true,
					       double space_width_factor_for_merge, //=1.00,
					       double space_width_factor_for_merge_with_space); //=0.33);

    
    //void remove_duplicate_chars(pdf_resource<PAGE_CELLS>& cells, double eps=1.0e-1);
    void remove_adjacent_cells(pdf_resource<PAGE_CELLS>& cells, double eps); //=1.0e-1);
    void remove_duplicate_cells(pdf_resource<PAGE_CELLS>& cells, double eps, bool same_line);
    
    void sanitize_bbox(pdf_resource<PAGE_CELLS>& cells,
		       double horizontal_cell_tolerance, //=1.0,
		       bool enforce_same_font, //=true,
		       double space_width_factor_for_merge, //=1.5,
		       double space_width_factor_for_merge_with_space); //=0.33);

    void sanitize_text(pdf_resource<PAGE_CELLS>& cells);


    
  private:

    bool applicable_for_merge(pdf_resource<PAGE_CELL>& cell_i,
			      pdf_resource<PAGE_CELL>& cell_j,
			      bool enforce_same_font);

    void contract_cells_into_lines_right_to_left(pdf_resource<PAGE_CELLS>& cells,
						 double horizontal_cell_tolerance,
						 bool enforce_same_font,
						 double space_width_factor_for_merge,
						 double space_width_factor_for_merge_with_space);

    void contract_cells_into_lines_left_to_right(pdf_resource<PAGE_CELLS>& cells,
						 double horizontal_cell_tolerance,
						 bool enforce_same_font,
						 double space_width_factor_for_merge,
						 double space_width_factor_for_merge_with_space,
						 bool allow_reverse);
    
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

  nlohmann::json pdf_sanitator<PAGE_CELLS>::to_records(pdf_resource<PAGE_CELLS>& cells)
  {
    LOG_S(INFO) << __FUNCTION__;

    nlohmann::json result = nlohmann::json::array({});
    
    int order = 0;
    for(auto itr=cells.begin(); itr!=cells.end(); itr++)
      {
	pdflib::pdf_resource<pdflib::PAGE_CELL>& cell = *itr;

	if(not cell.active)
	  {
	    continue;
	  }
	
	nlohmann::json item = nlohmann::json::object({});

	{
	  nlohmann::json rect = nlohmann::json::object({});

	  rect["r_x0"] = cell.r_x0; rect["r_y0"] = cell.r_y0;
	  rect["r_x1"] = cell.r_x1; rect["r_y1"] = cell.r_y1;
	  rect["r_x2"] = cell.r_x2; rect["r_y2"] = cell.r_y2;
	  rect["r_x3"] = cell.r_x3; rect["r_y3"] = cell.r_y3;

	  item["index"] = (order++);
	  
	  item["rect"] = rect;

	  item["text"] = cell.text;
	  item["orig"] = cell.text;

	  item["font_key"] = cell.font_key;
	  item["font_name"] = cell.font_name;

	  item["rendering_mode"] = cell.rendering_mode;

	  item["widget"] = cell.widget;
	  item["left_to_right"] = cell.left_to_right;
	}

	result.push_back(item);
      }
    
    return result;
  }
  
  pdf_resource<PAGE_CELLS> pdf_sanitator<PAGE_CELLS>::create_word_cells(pdf_resource<PAGE_CELLS>& char_cells,
									double horizontal_cell_tolerance,
									bool enforce_same_font,
									double space_width_factor_for_merge)
  {
    LOG_S(INFO) << __FUNCTION__;
    LOG_S(INFO) << "space_width_factor_for_merge (create_word_cells): " << space_width_factor_for_merge;
    
    // do a deep copy
    pdf_resource<PAGE_CELLS> word_cells;
    word_cells = char_cells;

    LOG_S(INFO) << "#-char cells: " << word_cells.size();
    
    // remove all spaces 
    auto itr = word_cells.begin();
    while(itr!=word_cells.end())
      {
	if(utils::string::is_space(itr->text))
	  {
	    itr = word_cells.erase(itr);	    
	  }
	else
	  {
	    itr++;
	  }
      }

    LOG_S(INFO) << "#-char cells (without spaces): " << word_cells.size();
    
    // > space_width_factor_for_merge, so nothing gets merged with a space
    double space_width_factor_for_merge_with_space = 2.0*space_width_factor_for_merge; 
    
    sanitize_bbox(word_cells,
		  horizontal_cell_tolerance,
		  enforce_same_font,
		  space_width_factor_for_merge,
		  space_width_factor_for_merge_with_space);

    LOG_S(INFO) << "#-word cells: " << word_cells.size();

    //return to_records(word_cells);
    return word_cells;
  }

  pdf_resource<PAGE_CELLS> pdf_sanitator<PAGE_CELLS>::create_line_cells(pdf_resource<PAGE_CELLS>& char_cells,
									double horizontal_cell_tolerance,
									bool enforce_same_font,
									double space_width_factor_for_merge,
									double space_width_factor_for_merge_with_space)
  {
    LOG_S(INFO) << __FUNCTION__ << " -> char_cells: " << char_cells.size();
    LOG_S(INFO) << "space_width_factor_for_merge (create_line_cells): " << space_width_factor_for_merge;
    LOG_S(INFO) << "space_width_factor_for_merge_with_space (create_line_cells): " << space_width_factor_for_merge_with_space;
    
    // do a deep copy
    pdf_resource<PAGE_CELLS> line_cells;
    line_cells = char_cells;

    LOG_S(INFO) << "# char-cells: " << line_cells.size();
    
    sanitize_bbox(line_cells,
		  horizontal_cell_tolerance,
		  enforce_same_font,
		  space_width_factor_for_merge,
		  space_width_factor_for_merge_with_space);
    
    LOG_S(INFO) << "# line-cells: " << line_cells.size();
    
    //return to_records(line_cells);
    return line_cells;
  }  

  /*
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
  */

  //void pdf_sanitator<PAGE_CELLS>::remove_duplicate_chars(pdf_resource<PAGE_CELLS>& cells, double eps)
  void pdf_sanitator<PAGE_CELLS>::remove_adjacent_cells(pdf_resource<PAGE_CELLS>& cells, double eps)
  {
    for(int i=0; i<cells.size(); i++)
      {
	if(not cells[i].active)
	  {
	    continue;
	  }
	
	int j = i+1;
	
	if(j+1>=cells.size() or (not cells[j].active))
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
	  }		
      }

    cells.remove_inactive_cells();
  }

  void pdf_sanitator<PAGE_CELLS>::remove_duplicate_cells(pdf_resource<PAGE_CELLS>& cells, double eps, bool same_line)
  {
    for(int i=0; i<cells.size(); i++)
      {
	if(not cells[i].active)
	  {
	    continue;
	  }

	for(int j=i+1; j<cells.size(); j++)
	  {	
	    if(same_line and std::abs(cells[i].r_y0-cells[j].r_y0)>eps)
	      {
		break;
	      }

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
	      }
	  }
      }

    cells.remove_inactive_cells();
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

    /*
    contract_cells_into_lines_v2(cells,
				 horizontal_cell_tolerance,
				 enforce_same_font,
				 space_width_factor_for_merge,
				 space_width_factor_for_merge_with_space);
    */
  }

  bool pdf_sanitator<PAGE_CELLS>::applicable_for_merge(pdf_resource<PAGE_CELL>& cell_i,
						       pdf_resource<PAGE_CELL>& cell_j,
						       bool enforce_same_font)
  {
    if(not cell_i.active)
      {
	return false;
      }
    
    if(not cell_j.active)
      {
	return false;
      }
    
    if(enforce_same_font and cell_i.font_name!=cell_j.font_name)
      {
	return false;
      }
	    
    if(not cell_i.has_same_reading_orientation(cell_j))
      {
	return false;
      }

    return true;
  }
  
  void pdf_sanitator<PAGE_CELLS>::contract_cells_into_lines_v1(pdf_resource<PAGE_CELLS>& cells,
							       double horizontal_cell_tolerance,
							       bool enforce_same_font,
							       double space_width_factor_for_merge,
							       double space_width_factor_for_merge_with_space)
  {
    contract_cells_into_lines_left_to_right(cells, horizontal_cell_tolerance, enforce_same_font, space_width_factor_for_merge, space_width_factor_for_merge_with_space, false);
    
    contract_cells_into_lines_right_to_left(cells, horizontal_cell_tolerance, enforce_same_font, space_width_factor_for_merge, space_width_factor_for_merge_with_space);
    
    contract_cells_into_lines_left_to_right(cells, horizontal_cell_tolerance, enforce_same_font, space_width_factor_for_merge, space_width_factor_for_merge_with_space, true);
  }
  
  void pdf_sanitator<PAGE_CELLS>::contract_cells_into_lines_left_to_right(pdf_resource<PAGE_CELLS>& cells,
									  double horizontal_cell_tolerance,
									  bool enforce_same_font,
									  double space_width_factor_for_merge,
									  double space_width_factor_for_merge_with_space,
									  bool allow_reverse)
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
	    if(not applicable_for_merge(cells[i], cells[j], enforce_same_font))
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
	    else if(allow_reverse and cells[j].is_adjacent_to(cells[i], delta_0))
	      {
		cells[j].merge_with(cells[i], delta_1);
		
		cells[i].active = false;
		LOG_S(INFO) << " -> merging reverse cell-" << j << " with " << i << " '" << cells[i].text << "'"<< ": " << cells[j].text;
	      }	    
	    else
	      {
		break;
	      }
	  }
      }

    {
      auto it = std::remove_if(cells.begin(), cells.end(), 
                        [](const pdf_resource<PAGE_CELL>& cell) {
                            return !cell.active;
                        });
      cells.erase(it, cells.end());
    }    
  }
  
  void pdf_sanitator<PAGE_CELLS>::contract_cells_into_lines_right_to_left(pdf_resource<PAGE_CELLS>& cells,
									  double horizontal_cell_tolerance,
									  bool enforce_same_font,
									  double space_width_factor_for_merge,
									  double space_width_factor_for_merge_with_space)
  {    
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
	    if(not applicable_for_merge(cells[i], cells[j], enforce_same_font))
	      {
		break;
	      }
	    
	    double delta_0 = cells[i].average_char_width()*space_width_factor_for_merge;
	    double delta_1 = cells[i].average_char_width()*space_width_factor_for_merge_with_space;
	    
	    if(cells[j].is_adjacent_to(cells[i], delta_0))
	      {
		cells[j].merge_with(cells[i], delta_1);
		
		cells[i].active = false;
		LOG_S(INFO) << " -> merging cell-" << i << " with " << j << " '" << cells[j].text << "'"<< ": " << cells[i].text;
	      }
	    else
	      {
		break;
	      }
	  }
      }    

    {
      auto it = std::remove_if(cells.begin(), cells.end(), 
                        [](const pdf_resource<PAGE_CELL>& cell) {
                            return !cell.active;
                        });
      cells.erase(it, cells.end());
    }    
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
