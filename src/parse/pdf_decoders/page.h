//-*-C++-*-

#ifndef PDF_PAGE_DECODER_H
#define PDF_PAGE_DECODER_H

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <nlohmann/json.hpp>

namespace pdflib
{

  template<>
  class pdf_decoder<PAGE>
  {
  public:

    pdf_decoder(QPDFObjectHandle page, int page_num);
    ~pdf_decoder();

    int get_page_number();

    // Typed accessors for direct pybind11 binding
    pdf_resource<PAGE_CELLS>& get_page_cells() { return page_cells; }
    pdf_resource<PAGE_LINES>& get_page_lines() { return page_lines; }
    pdf_resource<PAGE_IMAGES>& get_page_images() { return page_images; }
    pdf_resource<PAGE_DIMENSION>& get_page_dimension() { return page_dimension; }

    // Char, word and line cells (char_cells is alias for page_cells, word/line are computed)
    pdf_resource<PAGE_CELLS>& get_char_cells() { return page_cells; }
    pdf_resource<PAGE_CELLS>& get_word_cells() { return word_cells; }
    pdf_resource<PAGE_CELLS>& get_line_cells() { return line_cells; }

    bool has_word_cells() const { return word_cells_created; }
    bool has_line_cells() const { return line_cells_created; }

    // Create word/line cells from page_cells
    void create_word_cells(double horizontal_cell_tolerance = 1.0,
			   bool enforce_same_font = true,
			   double space_width_factor_for_merge = 0.33);

    void create_line_cells(double horizontal_cell_tolerance = 1.0,
			   bool enforce_same_font = true,
			   double space_width_factor_for_merge = 1.0,
			   double space_width_factor_for_merge_with_space = 0.33);

    // JSON serialization (kept for backward compatibility)
    nlohmann::json get(bool keep_char_cells=true,
                       bool keep_lines=true,
                       bool keep_bitmaps=true,
		       bool do_sanitization=false);

    void decode_page(std::string page_boundary, bool do_sanitization);

    // Get timing information for this page
    pdf_timings& get_timings() { return timings; }
    const pdf_timings& get_timings() const { return timings; }

  private:

    void decode_dimensions();

    // Resources
    void decode_resources();
    void decode_resources_low_level();

    void decode_grphs();

    void decode_fonts();

    void decode_xobjects();

    // Contents
    void decode_contents();

    void decode_annots();

    void rotate_contents();

    void sanitise_contents(std::string page_boundary);

  private:

    QPDFObjectHandle qpdf_page;

    int page_number;

    QPDFObjectHandle qpdf_parent_resources;
    QPDFObjectHandle qpdf_resources;
    QPDFObjectHandle qpdf_grphs;
    QPDFObjectHandle qpdf_fonts;
    QPDFObjectHandle qpdf_xobjects;

    nlohmann::json json_annots;

    nlohmann::json json_page;
    nlohmann::json json_parent_resources;
    nlohmann::json json_resources;
    nlohmann::json json_grphs;
    nlohmann::json json_fonts;
    nlohmann::json json_xobjects;

    pdf_resource<PAGE_DIMENSION> page_dimension;

    pdf_resource<PAGE_CELLS>  page_cells;
    pdf_resource<PAGE_LINES>  page_lines;
    pdf_resource<PAGE_IMAGES> page_images;

    pdf_resource<PAGE_CELLS>  cells;
    pdf_resource<PAGE_LINES>  lines;
    pdf_resource<PAGE_IMAGES> images;

    // Computed cell aggregations
    pdf_resource<PAGE_CELLS>  word_cells;
    pdf_resource<PAGE_CELLS>  line_cells;
    bool word_cells_created = false;
    bool line_cells_created = false;

    pdf_resource<PAGE_GRPHS>     page_grphs;
    pdf_resource<PAGE_FONTS>     page_fonts;
    pdf_resource<PAGE_XOBJECTS>  page_xobjects;

    pdf_timings timings;
  };

  pdf_decoder<PAGE>::pdf_decoder(QPDFObjectHandle page, int page_num):
    qpdf_page(page),
    page_number(page_num)
  {
  }

  pdf_decoder<PAGE>::~pdf_decoder()
  {
  }

  int pdf_decoder<PAGE>::get_page_number()
  {
    return page_number;
  }

  nlohmann::json pdf_decoder<PAGE>::get(bool keep_char_cells,
                                        bool keep_lines,
                                        bool keep_bitmaps,
                                        bool do_sanitization)
  {
    LOG_S(INFO) << "pdf_decoder<PAGE>::get "
		<< "keep_char_cells: " << keep_char_cells << ", "
		<< "keep_lines: " << keep_lines << ", "
		<< "keep_bitmaps: " << keep_bitmaps << ", "
		<< "do_sanitization: " << do_sanitization << ", ";
    
    nlohmann::json result;
    {
      result["page_number"] = page_number;

      result["annotations"] = json_annots;

      nlohmann::json& timings_ = result["timings"];
      {
        // Serialize timings as sums for backward compatibility
        auto sum_map = timings.to_sum_map();
        for(auto itr=sum_map.begin(); itr!=sum_map.end(); itr++)
          {
            timings_[itr->first] = itr->second;
          }
      }

      {
        nlohmann::json& original = result["original"];

        original["dimension"] = page_dimension.get();

        if(keep_bitmaps)
          {
            original["images"] = page_images.get();
          }
	else
	  {
	    LOG_S(WARNING) << "skipping the serialization of `images` to json!";
	  }
	
        if(keep_char_cells)
          {
            original["cells"] = page_cells.get();
          }
	else
	  {
	    LOG_S(WARNING) << "skipping the serialization of `cells` to json!";
	  }	

        if(keep_lines)
          {
            original["lines"] = page_lines.get();
          }
	else
	  {
	    LOG_S(WARNING) << "skipping the serialization of `lines` to json!";
	  }	
      }

      if(do_sanitization)
        {
          nlohmann::json& sanitized = result["sanitized"];

          sanitized["dimension"] = page_dimension.get();

          if(keep_bitmaps)
            {
              sanitized["images"] = images.get();
            }

          if(keep_char_cells)
            {
              sanitized["cells"] = cells.get();
            }

          if(keep_lines)
            {
              sanitized["lines"] = lines.get();
            }
        }
      	else
	  {
	    LOG_S(WARNING) << "skipping the serialization of `sanitzed` page to json!";
	  }	
    }

    return result;
  }

  void pdf_decoder<PAGE>::decode_page(std::string page_boundary, bool do_sanitization)
  {
    utils::timer global, local;

    {
      local.reset();
      json_page = to_json(qpdf_page);
      timings.add_timing(pdf_timings::KEY_TO_JSON_PAGE, local.get_time());
    }

    {
      local.reset();
      json_annots = extract_annots_in_json(qpdf_page);
      timings.add_timing(pdf_timings::KEY_EXTRACT_ANNOTS_JSON, local.get_time());
    }

    {
      local.reset();
      decode_dimensions();
      timings.add_timing(pdf_timings::KEY_DECODE_DIMENSIONS, local.get_time());
    }

    {
      local.reset();
      decode_resources();
      timings.add_timing(pdf_timings::KEY_DECODE_RESOURCES, local.get_time());
    }

    {
      local.reset();
      decode_contents();
      timings.add_timing(pdf_timings::KEY_DECODE_CONTENTS, local.get_time());
    }

    {
      local.reset();
      decode_annots();
      timings.add_timing(pdf_timings::KEY_DECODE_ANNOTS, local.get_time());
    }

    {
      local.reset();
      rotate_contents();
      timings.add_timing(pdf_timings::KEY_ROTATE_CONTENTS, local.get_time());
    }

    // fix the orientation
    {
      local.reset();
      pdf_sanitator<PAGE_DIMENSION> sanitator(page_dimension);

      sanitator.sanitize(page_boundary); // update the top-level bbox
      sanitator.sanitize(page_cells, page_boundary);
      sanitator.sanitize(page_lines, page_boundary);
      sanitator.sanitize(page_images, page_boundary);
      timings.add_timing(pdf_timings::KEY_SANITIZE_ORIENTATION, local.get_time());
    }

    {
      local.reset();
      pdf_sanitator<PAGE_CELLS> sanitator;

      {
	sanitator.remove_duplicate_cells(page_cells, 0.5, true);
      }

      {
	sanitator.sanitize_text(page_cells);
      }
      timings.add_timing(pdf_timings::KEY_SANITIZE_CELLS, local.get_time());
    }

    if(do_sanitization)
      {
        local.reset();
        sanitise_contents(page_boundary);
        timings.add_timing(pdf_timings::KEY_SANITISE_CONTENTS, local.get_time());
      }
    else
      {
        LOG_S(WARNING) << "skipping sanitization!";
      }

    timings.add_timing(pdf_timings::KEY_DECODE_PAGE, global.get_time());
  }

  void pdf_decoder<PAGE>::decode_dimensions()
  {
    LOG_S(INFO) << __FUNCTION__;
    //utils::timer timer;

    page_dimension.execute(json_page, qpdf_page);
  }

  void pdf_decoder<PAGE>::decode_resources()
  {
    LOG_S(INFO) << __FUNCTION__;

    if(json_page.count("/Resources") and
       json_page.count("/Parent"))
      {
        auto parent = qpdf_page.getKey("/Parent");
        if(parent.hasKey("/Resources"))
          {
            //LOG_S(INFO) << "parent of page has resources!: " << json_parent_resources.dump(2);

            qpdf_parent_resources = parent.getKey("/Resources");
            json_parent_resources = to_json(qpdf_parent_resources); //json_page["/Resources"];

            // both are used in the decode_resources
            qpdf_resources = qpdf_parent_resources;
            json_resources = json_parent_resources;

            decode_resources_low_level();
          }
        else
          {
            LOG_S(INFO) << "parent of page has no resources!";
          }

        // This might overwrite resources from the parent ...
        qpdf_resources = qpdf_page.getKey("/Resources");
        json_resources = json_page["/Resources"];

        decode_resources_low_level();
      }
    else if(json_page.count("/Resources"))
      {
        qpdf_resources = qpdf_page.getKey("/Resources");
        json_resources = json_page["/Resources"];

        decode_resources_low_level();
      }
    else if(json_page.count("/Parent"))
      {
        auto parent = qpdf_page.getKey("/Parent");
        if(parent.hasKey("/Resources"))
          {
            qpdf_parent_resources = parent.getKey("/Resources");
            json_parent_resources = to_json(qpdf_parent_resources); //json_page["/Resources"];

            LOG_S(INFO) << "parent of page has resources!: " << json_parent_resources.dump(2);

            // both are used in the decode_resources
            qpdf_resources = qpdf_parent_resources;
            json_resources = json_parent_resources;

            decode_resources_low_level();
          }
        else
          {
            LOG_S(ERROR) << "page has no /Resources nor a /Parent with /Resources.";
          }
      }
    else
      {
        LOG_S(WARNING) << "page does not have any resources!: " << json_page.dump(2);
      }

    {
      auto font_keys = page_fonts.keys();

      LOG_S(INFO) << "fonts: " << font_keys.size();
      for(auto key:font_keys)
        {
          LOG_S(INFO) << " -> font-key: '" << key << "'";
        }
    }
  }

  void pdf_decoder<PAGE>::decode_resources_low_level()
  {
    LOG_S(INFO) << __FUNCTION__;

    if(json_resources.count("/ExtGState"))
      {
        qpdf_grphs = qpdf_resources.getKey("/ExtGState");
        json_grphs = json_resources["/ExtGState"];

        decode_grphs();
      }
    else
      {
        LOG_S(WARNING) << "page does not have any graphics state! ";// << json_resources.dump(2);
      }

    if(json_resources.count("/Font"))
      {
        qpdf_fonts = qpdf_resources.getKey("/Font");
        json_fonts = json_resources["/Font"];

        decode_fonts();
      }
    else
      {
        LOG_S(WARNING) << "page does not have any fonts! ";// << json_resources.dump(2);
      }

    if(json_resources.count("/XObject"))
      {
        qpdf_xobjects = qpdf_resources.getKey("/XObject");
        json_xobjects = json_resources["/XObject"];

        decode_xobjects();
      }
    else
      {
        LOG_S(WARNING) << "page does not have any xobjects! ";// << json_resources.dump(2);
      }
  }

  void pdf_decoder<PAGE>::decode_grphs()
  {
    LOG_S(INFO) << __FUNCTION__;

    page_grphs.set(json_grphs, qpdf_grphs);
  }

  void pdf_decoder<PAGE>::decode_fonts()
  {
    LOG_S(INFO) << __FUNCTION__;

    page_fonts.set(json_fonts, qpdf_fonts, timings);

    //for(auto itr=timings.begin(); itr!=timings.end(); itr++)
    //{
    //LOG_S(INFO) << itr->first << ": " << timings.get_sum(itr->first);
    //}
  }

  void pdf_decoder<PAGE>::decode_xobjects()
  {
    LOG_S(INFO) << __FUNCTION__;

    page_xobjects.set(json_xobjects, qpdf_xobjects);
  }

  void pdf_decoder<PAGE>::decode_contents()
  {
    LOG_S(INFO) << __FUNCTION__;

    QPDFPageObjectHelper          qpdf_page_object(qpdf_page);
    std::vector<QPDFObjectHandle> contents = qpdf_page_object.getPageContents();

    pdf_decoder<STREAM> stream_decoder(page_dimension, page_cells,
                                       page_lines, page_images,
                                       page_fonts, page_grphs,
                                       page_xobjects, timings);

    int cnt = 0;

    std::vector<qpdf_instruction> parameters;
    for(auto content:contents)
      {
        LOG_S(INFO) << "--------------- start decoding content stream (" << (cnt++) << ")... ---------------";

        stream_decoder.decode(content);
        //stream_decoder.print();

        stream_decoder.interprete(parameters);

        if(parameters.size()>0)
          {
            LOG_S(WARNING) << "stream is ending with non-zero number of parameters";
          }
      }
  }

  void pdf_decoder<PAGE>::decode_annots()
  {
    LOG_S(INFO) << __FUNCTION__;

    //LOG_S(INFO) << "analyzing: " << json_annots.dump(2);
    if(json_annots.is_array())
      {
        for(auto item:json_annots)
          {
            LOG_S(INFO) << "analyzing: " << item.dump(2);

            if(item.count("/Type")==1 and item["/Type"].get<std::string>()=="/Annot" and
               item.count("/Subtype")==1 and item["/Subtype"].get<std::string>()=="/Widget" and
               item.count("/Rect")==1 and
               item.count("/V")==1 and //item["/V"].is_string() and
               item.count("/T")==1 and
               true)
              {
                std::array<double, 4> bbox = item["/Rect"].get<std::array<double, 4> >();
                //LOG_S(INFO) << bbox[0] << ", "<< bbox[1] << ", "<< bbox[2] << ", "<< bbox[3];

                std::string text = "<unknown>";
                if(item["/V"].is_string())
                  {
                    text = item["/V"].get<std::string>();
                  }
                //LOG_S(INFO) << "text: " << text;

                pdf_resource<PAGE_CELL> cell;
                {
                  cell.widget = true;

                  cell.x0 = bbox[0];
                  cell.y0 = bbox[1];
                  cell.x1 = bbox[2];
                  cell.y1 = bbox[3];

                  cell.r_x0 = bbox[0];
                  cell.r_y0 = bbox[1];
                  cell.r_x1 = bbox[2];
                  cell.r_y1 = bbox[1];
                  cell.r_x2 = bbox[2];
                  cell.r_y2 = bbox[3];
                  cell.r_x3 = bbox[0];
                  cell.r_y3 = bbox[3];

                  cell.text = text;
                  cell.rendering_mode = 0;

                  cell.space_width = 0;
                  //cell.chars  = {};//chars;
                  //cell.widths = {};//widths;

                  cell.enc_name = "Form-font"; //font.get_encoding_name();

                  cell.font_enc = "Form-font"; //to_string(font.get_encoding());
                  cell.font_key = "Form-font"; //font.get_key();

                  cell.font_name = "Form-font"; //font.get_name();
                  cell.font_size = 0; //font_size/1000.0;

                  cell.italic = false;
                  cell.bold   = false;

                  cell.ocr        = false;
                  cell.confidence = -1.0;

                  cell.stack_size  = -1;
                  cell.block_count = -1;
                  cell.instr_count = -1;
                }

                page_cells.push_back(cell);
              }
          }
      }
  }

  void pdf_decoder<PAGE>::rotate_contents()
  {
    LOG_S(INFO) << __FUNCTION__;

    int angle = page_dimension.get_angle();
    
    if((angle%360)==0)
      {
        return;
      }
    else if((angle%90)!=0)
      {
        LOG_S(ERROR) << "the /Rotate angle should be a multiple of 90 ...";
      }

    // see Table 30
    LOG_S(WARNING) << "rotating contents clock-wise with angle: " << angle;

    std::pair<double, double> delta = page_dimension.rotate(angle);
    LOG_S(INFO) << "translation delta: " << delta.first << ", " << delta.second;
    
    page_cells.rotate(angle, delta);
    page_lines.rotate(angle, delta);
    page_images.rotate(angle, delta);
  }

  void pdf_decoder<PAGE>::sanitise_contents(std::string page_boundary)
  {
    LOG_S(INFO) << __FUNCTION__;

    {
      lines = page_lines;
    }

    {
      images = page_images;
    }

    // sanitise the cells
    {
      pdf_sanitator<PAGE_CELLS> sanitator;

      //sanitator.remove_duplicate_chars(page_cells, 0.5);
      //sanitator.sanitize_text(page_cells);

      cells = page_cells;

      double horizontal_cell_tolerance=1.0;
      bool enforce_same_font=true;
      //double space_width_factor_for_merge=1.5;
      double space_width_factor_for_merge=1.0;
      double space_width_factor_for_merge_with_space=0.33;

      sanitator.sanitize_bbox(cells,
                              horizontal_cell_tolerance,
                              enforce_same_font,
                              space_width_factor_for_merge,
                              space_width_factor_for_merge_with_space);

      //sanitator.sanitize_text(cells);

      LOG_S(INFO) << "#-page-cells: " << page_cells.size();
      LOG_S(INFO) << "#-sani-cells: " << cells.size();
    }
  }

  void pdf_decoder<PAGE>::create_word_cells(double horizontal_cell_tolerance,
					    bool enforce_same_font,
					    double space_width_factor_for_merge)
  {
    LOG_S(INFO) << __FUNCTION__;
    utils::timer timer;

    pdf_sanitator<PAGE_CELLS> sanitizer;

    word_cells = sanitizer.create_word_cells(page_cells,
					     horizontal_cell_tolerance,
					     enforce_same_font,
					     space_width_factor_for_merge);

    // Remove duplicates (quadratic but necessary)
    sanitizer.remove_duplicate_cells(word_cells, 0.5, true);

    word_cells_created = true;

    LOG_S(INFO) << "#-page-cells: " << page_cells.size() << " -> #-word-cells: " << word_cells.size();
    timings.add_timing(pdf_timings::KEY_CREATE_WORD_CELLS, timer.get_time());
  }

  void pdf_decoder<PAGE>::create_line_cells(double horizontal_cell_tolerance,
					    bool enforce_same_font,
					    double space_width_factor_for_merge,
					    double space_width_factor_for_merge_with_space)
  {
    LOG_S(INFO) << __FUNCTION__;
    utils::timer timer;

    pdf_sanitator<PAGE_CELLS> sanitizer;

    line_cells = sanitizer.create_line_cells(page_cells,
					     horizontal_cell_tolerance,
					     enforce_same_font,
					     space_width_factor_for_merge,
					     space_width_factor_for_merge_with_space);

    // Remove duplicates (quadratic but necessary)
    sanitizer.remove_duplicate_cells(line_cells, 0.5, true);

    line_cells_created = true;

    LOG_S(INFO) << "#-page-cells: " << page_cells.size() << " -> #-line-cells: " << line_cells.size();
    timings.add_timing(pdf_timings::KEY_CREATE_LINE_CELLS, timer.get_time());
  }

}

#endif
