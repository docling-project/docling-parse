//-*-C++-*-

#ifndef PDF_PAGE_CELL_RESOURCE_H
#define PDF_PAGE_CELL_RESOURCE_H

namespace pdflib
{

  template<>
  class pdf_resource<PAGE_CELL>
  {
  public:

    pdf_resource();
    ~pdf_resource();

    nlohmann::json get();
    bool init_from(nlohmann::json& data);

    void rotate(int angle, std::pair<double, double> delta);

    double length();

    int number_of_chars();

    double average_char_width();
    
    bool is_adjacent_to(pdf_resource<PAGE_CELL>& other, double delta);

    bool has_same_reading_orientation(pdf_resource<PAGE_CELL>& other);
    
    bool merge_with(pdf_resource<PAGE_CELL>& other, double delta);
    
  public:

    static std::vector<std::string> header;

    bool active;
    bool left_to_right;
    
    double x0;
    double y0;
    double x1;
    double y1;

    double r_x0;
    double r_y0;
    double r_x1;
    double r_y1;
    double r_x2;
    double r_y2;
    double r_x3;
    double r_y3;

    std::string text;
    int         rendering_mode;
    //std::string text_utf8;
    //std::string text_byte;

    double space_width;

    //std::vector<std::string> chars;
    //std::vector<double>      widths;

    std::string enc_name;

    std::string font_enc;
    std::string font_key;

    std::string font_name;
    double      font_size;

    bool italic;
    bool bold;

    bool   ocr;
    double confidence;

    int stack_size;
    int block_count;
    int instr_count;

    bool widget;
  };

  pdf_resource<PAGE_CELL>::pdf_resource():
    active(true),
    left_to_right(true)
    {}

  pdf_resource<PAGE_CELL>::~pdf_resource()
  {}

  std::vector<std::string> pdf_resource<PAGE_CELL>::header = {
    "x0",
    "y0",
    "x1",
    "y1",

    "r_x0",
    "r_y0",
    "r_x1",
    "r_y1",
    "r_x2",
    "r_y2",
    "r_x3",
    "r_y3",

    "text",
    "rendering-mode",
    //"text-utf8",
    //"text-byte",

    "space-width",
    //"chars",
    //"widths",

    "encoding-name",

    "font-encoding",
    "font-key",
    "font-name",
    //"font-size",

    //"italic",
    //"bold",

    //"ocr",
    //"confidence",

    //"stack-size",
    //"block-count",
    //"instr-count",

    "widget",
    "left_to_right"
  };

  void pdf_resource<PAGE_CELL>::rotate(int angle, std::pair<double, double> delta)
  {
    utils::values::rotate_inplace(angle, x0, y0);
    utils::values::rotate_inplace(angle, x1, y1);
    
    utils::values::rotate_inplace(angle, r_x0, r_y0);
    utils::values::rotate_inplace(angle, r_x1, r_y1);
    utils::values::rotate_inplace(angle, r_x2, r_y2);
    utils::values::rotate_inplace(angle, r_x3, r_y3);

    utils::values::translate_inplace(delta, x0, y0);
    utils::values::translate_inplace(delta, x1, y1);
    
    utils::values::translate_inplace(delta, r_x0, r_y0);
    utils::values::translate_inplace(delta, r_x1, r_y1);
    utils::values::translate_inplace(delta, r_x2, r_y2);
    utils::values::translate_inplace(delta, r_x3, r_y3);    
  }
  
  nlohmann::json pdf_resource<PAGE_CELL>::get()
  {
    nlohmann::json cell;

    {
      cell.push_back(utils::values::round(x0)); // 0
      cell.push_back(utils::values::round(y0)); // 1
      cell.push_back(utils::values::round(x1)); // 2
      cell.push_back(utils::values::round(y1)); // 3

      cell.push_back(utils::values::round(r_x0)); // 4
      cell.push_back(utils::values::round(r_y0)); // 5
      cell.push_back(utils::values::round(r_x1)); // 6
      cell.push_back(utils::values::round(r_y1)); // 7
      cell.push_back(utils::values::round(r_x2)); // 8
      cell.push_back(utils::values::round(r_y2)); // 9
      cell.push_back(utils::values::round(r_x3)); // 10
      cell.push_back(utils::values::round(r_y3)); // 11

      cell.push_back(text); // 12
      cell.push_back(rendering_mode); // 13 

      cell.push_back(utils::values::round(space_width)); //14

      cell.push_back(enc_name); // 15

      cell.push_back(font_enc); // 16
      cell.push_back(font_key); // 17
      cell.push_back(font_name); // 18

      cell.push_back(widget); // 19
      cell.push_back(left_to_right); // 20
    }
    assert(cell.size()==header.size());

    return cell;
  }

  bool pdf_resource<PAGE_CELL>::init_from(nlohmann::json& data)
  {
    //LOG_S(INFO) << __FUNCTION__ << "data: " << data.size();

    if(data.is_array() and data.size()>19)
      {
        x0 = data.at(0).get<double>();
        y0 = data.at(1).get<double>();
        x1 = data.at(2).get<double>();
        y1 = data.at(3).get<double>();

        r_x0 = data.at(4).get<double>();
        r_y0 = data.at(5).get<double>();
        r_x1 = data.at(6).get<double>();
        r_y1 = data.at(7).get<double>();
        r_x2 = data.at(8).get<double>();
        r_y2 = data.at(9).get<double>();
        r_x3 = data.at(10).get<double>();
        r_y3 = data.at(11).get<double>();

        text = data.at(12).get<std::string>();
        rendering_mode = data.at(13).get<int>();

        space_width = data.at(14).get<double>();

        enc_name = data.at(15).get<std::string>();

        font_enc = data.at(16).get<std::string>();
        font_key = data.at(17).get<std::string>();
        font_name = data.at(18).get<std::string>();

	widget = data.at(19).get<bool>();
	left_to_right = data.at(20).get<bool>();

        return true;
      }
    else
      {
	std::stringstream ss;
	ss << "can not initialise pdf_resource<PAGE_CELL> from "
	   << data.dump(2);
	
        LOG_S(ERROR) << ss.str();
	throw std::logic_error(ss.str());
      }

    return false;
  }

  double pdf_resource<PAGE_CELL>::length()
  {
    return std::sqrt(std::pow(r_x1-r_x0, 2) + std::pow(r_y1-r_y0, 2));
  }

  int pdf_resource<PAGE_CELL>::number_of_chars()
  {
    return utils::string::count_unicode_characters(text);
  }

  double pdf_resource<PAGE_CELL>::average_char_width()
  {
    double len = length();
    int num_chars = number_of_chars();
    
    return (num_chars>0? len/num_chars : 0.0);
  }
  
  bool pdf_resource<PAGE_CELL>::is_adjacent_to(pdf_resource<PAGE_CELL>& other, double eps)
  {
    double d0 = std::sqrt((r_x1-other.r_x0)*(r_x1-other.r_x0) + (r_y1-other.r_y0)*(r_y1-other.r_y0));
    double d1 = std::sqrt((r_x2-other.r_x3)*(r_x2-other.r_x3) + (r_y2-other.r_y3)*(r_y2-other.r_y3));

    return ((d0<eps) and (d1<eps));
  }

  bool pdf_resource<PAGE_CELL>::has_same_reading_orientation(pdf_resource<PAGE_CELL>& other)
  {
    // it might need is_punctuation function instead of just the space
    bool is_punc = utils::string::is_punctuation_or_space(text);
    bool other_is_punc = utils::string::is_punctuation_or_space(other.text);
    
    //return ((left_to_right==other.left_to_right) or (text==" " or other.text==" "));
    return ((left_to_right==other.left_to_right) or (is_punc or other_is_punc)); 
  }
  
  bool pdf_resource<PAGE_CELL>::merge_with(pdf_resource<PAGE_CELL>& other, double delta)
  {
    if(not has_same_reading_orientation(other))
      {
	LOG_S(ERROR) << "inconsistent merging of cells!";
      }
    
    double d0 = std::sqrt((r_x1-other.r_x0)*(r_x1-other.r_x0) + (r_y1-other.r_y0)*(r_y1-other.r_y0));

    if((not left_to_right) or (not other.left_to_right))
      {
	if(delta<d0)
	  {
	    text = " " + text;
	  }    
	text = other.text + text;

	left_to_right = false;
      }
    else
      {
	if(delta<d0)
	  {
	    text += " ";
	  }    
	text += other.text;

	left_to_right = true;
      }
    
    r_x1 = other.r_x1;
    r_y1 = other.r_y1;

    r_x2 = other.r_x2;
    r_y2 = other.r_y2;

    x0 = r_x0;
    x0 = std::min(x0, r_x1);
    x0 = std::min(x0, r_x2);
    x0 = std::min(x0, r_x3);

    y0 = r_y0;
    y0 = std::min(y0, r_y1);
    y0 = std::min(y0, r_y2);
    y0 = std::min(y0, r_y3);

    x1 = r_x0;
    x1 = std::max(x1, r_x1);
    x1 = std::max(x1, r_x2);
    x1 = std::max(x1, r_x3);
    
    y1 = r_y0;
    y1 = std::max(y1, r_y1);
    y1 = std::max(y1, r_y2);
    y1 = std::max(y1, r_y3);        

    return true;
  }
  
}

#endif
