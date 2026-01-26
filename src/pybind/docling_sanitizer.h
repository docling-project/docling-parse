//-*-C++-*-

#ifndef PYBIND_PDF_SANITIZER_H
#define PYBIND_PDF_SANITIZER_H

#include <parse.h>

namespace docling
{
  class docling_sanitizer: public docling_resources
  {
  public:

    docling_sanitizer();

    docling_sanitizer(std::string level);

    void set_loglevel(int level=0);
    void set_loglevel_with_label(std::string level="error");

    bool set_char_cells(nlohmann::json& data);

    nlohmann::json create_word_cells(double horizontal_cell_tolerance, //=1.00,
				     bool enforce_same_font, //=true,
				     double space_width_factor_for_merge); //=0.05);

    nlohmann::json create_line_cells(double horizontal_cell_tolerance, //=1.00,
				     bool enforce_same_font, //=true,
				     double space_width_factor_for_merge, //=1.00,
				     double space_width_factor_for_merge_with_space); //=0.33);
    
  private:

    pdflib::pdf_sanitator<pdflib::PAGE_CELLS> cell_sanitizer;

    pdflib::pdf_resource<pdflib::PAGE_CELLS> char_cells;
    pdflib::pdf_resource<pdflib::PAGE_CELLS> word_cells;
    pdflib::pdf_resource<pdflib::PAGE_CELLS> line_cells;    
  };

  docling_sanitizer::docling_sanitizer():
    cell_sanitizer(),

    char_cells(),
    word_cells(),
    line_cells()    
  {}

  docling_sanitizer::docling_sanitizer(std::string level)
  {
    set_loglevel_with_label(level);
  }

  void docling_sanitizer::set_loglevel(int level)
  {
    if(level>=3)
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
      }
    else if(level==2)
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
      }
    else if(level==1)
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
      }
    else if(level==0)
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_FATAL;
      }
    else
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
      }
  }

  void docling_sanitizer::set_loglevel_with_label(std::string level)
  {
    if(level=="info")
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
      }
    else if(level=="warning" or level=="warn")
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_WARNING;
      }
    else if(level=="error")
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
      }
    else if(level=="fatal")
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_FATAL;
      }
    else
      {
        loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
      }
  }
  
  bool docling_sanitizer::set_char_cells(nlohmann::json& data)
  {
    LOG_S(INFO) << __FUNCTION__;
    
    if(data.is_array())
      {
	char_cells.clear();
	
	for(auto& item:data)
	  {
	    pdflib::pdf_resource<pdflib::PAGE_CELL> char_cell;

	    {
	      char_cell.active = true;
	    
	      char_cell.r_x0 = item.at("rect").at("r_x0").get<double>();
	      char_cell.r_y0 = item.at("rect").at("r_y0").get<double>();

	      char_cell.r_x1 = item.at("rect").at("r_x1").get<double>();
	      char_cell.r_y1 = item.at("rect").at("r_y1").get<double>();

	      char_cell.r_x2 = item.at("rect").at("r_x2").get<double>();
	      char_cell.r_y2 = item.at("rect").at("r_y2").get<double>();

	      char_cell.r_x3 = item.at("rect").at("r_x3").get<double>();
	      char_cell.r_y3 = item.at("rect").at("r_y3").get<double>();

	      /*
	      char_cell.x0
		= std::min(char_cell.r_x0, std::min(char_cell.r_x1, std::min(char_cell.r_x2, char_cell.r_x3)));
	      char_cell.y0
		= std::min(char_cell.r_y0, std::min(char_cell.r_y1, std::min(char_cell.r_y2, char_cell.r_y3)));

	      char_cell.x1
		= std::max(char_cell.r_x0, std::max(char_cell.r_x1, std::max(char_cell.r_x2, char_cell.r_x3)));
	      char_cell.y1
	      = std::max(char_cell.r_y0, std::max(char_cell.r_y1, std::max(char_cell.r_y2, char_cell.r_y3)));	   
	      */
	      
	      char_cell.text = item.at("text").get<std::string>();
	      //char_cell.orig = item.at("text").get<std::string>();
	      
	      char_cell.rendering_mode = item.at("rendering_mode").get<int>();

	      char_cell.font_name = item.at("font_name").get<std::string>();
	      char_cell.font_key = item.at("font_key").get<std::string>();

	      char_cell.left_to_right = item.at("left_to_right").get<bool>();
	      char_cell.widget = item.at("widget").get<bool>();	      
	    }
	    char_cells.push_back(char_cell);	    
	  }

	LOG_S(INFO) << "read " << char_cells.size() << " char-cells";
	
	return true;
      }
    else if(data.is_object())
      {
	char_cells.init_from(data);	
	LOG_S(INFO) << "read " << char_cells.size() << " char-cells";
	
	return true;
      }
    else
      {
	LOG_S(ERROR) << "could not interprete data as char_cells: " << data.dump(2); 
      }

    return false;
  }

  /*
  nlohmann::json docling_sanitizer::to_records(std::string label)
  {
    LOG_S(INFO) << __FUNCTION__;

    nlohmann::json result = nlohmann::json::array({});
    
    pdflib::pdf_resource<pdflib::PAGE_CELLS>* cells = NULL;
    
    if(label=="char")
      {
	cells = &char_cells;
      }
    else if(label=="word")
      {
	cells = &word_cells;
      }
    else if(label=="line")
      {
	cells = &line_cells;
      }
    else
      {
	return result;
      }

    int order = 0;
    for(auto itr=cells->begin(); itr!=cells->end(); itr++)
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
  */
  
  nlohmann::json docling_sanitizer::create_word_cells(double horizontal_cell_tolerance,
						      bool enforce_same_font,
						      double space_width_factor_for_merge)
  {
    LOG_S(INFO) << __FUNCTION__;

    /*
    // do a deep copy
    word_cells = char_cells;

    LOG_S(INFO) << "#-word cells: " << word_cells.size();
    
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

    LOG_S(INFO) << "#-word cells: " << word_cells.size();
    
    // > space_width_factor_for_merge, so nothing gets merged with a space
    double space_width_factor_for_merge_with_space = 2.0*space_width_factor_for_merge; 
    
    cell_sanitizer.sanitize_bbox(word_cells,
    horizontal_cell_tolerance,
				 enforce_same_font,
				 space_width_factor_for_merge,
				 space_width_factor_for_merge_with_space);

    LOG_S(INFO) << "#-wordcells: " << word_cells.size();

    return to_records("word");
    */

    word_cells = cell_sanitizer.create_word_cells(char_cells,
						  horizontal_cell_tolerance,
						  enforce_same_font,
						  space_width_factor_for_merge);

    return cell_sanitizer.to_records(word_cells);
  }

  nlohmann::json docling_sanitizer::create_line_cells(double horizontal_cell_tolerance,
						      bool enforce_same_font,
						      double space_width_factor_for_merge,
						      double space_width_factor_for_merge_with_space)
  {
    LOG_S(INFO) << __FUNCTION__ << " -> char_cells: " << char_cells.size();

    /*
    // do a deep copy
    line_cells = char_cells;

    LOG_S(INFO) << "initial line-cells: " << line_cells.size();
    
    cell_sanitizer.sanitize_bbox(line_cells,
				 horizontal_cell_tolerance,
				 enforce_same_font,
				 space_width_factor_for_merge,
				 space_width_factor_for_merge_with_space);
    
    LOG_S(INFO) << "initial line-cells: " << line_cells.size();

    return to_records("line");
    */

    line_cells = cell_sanitizer.create_line_cells(char_cells,
						  horizontal_cell_tolerance,
						  enforce_same_font,
						  space_width_factor_for_merge,
						  space_width_factor_for_merge_with_space);

    return cell_sanitizer.to_records(line_cells);
  }  
  
}

#endif
