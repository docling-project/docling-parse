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
    pdf_resource<PAGE_SHAPES>& get_page_shapes() { return page_shapes; }
    pdf_resource<PAGE_IMAGES>& get_page_images() { return page_images; }
    pdf_resource<PAGE_DIMENSION>& get_page_dimension() { return page_dimension; }

    // Char, word and line cells (char_cells is alias for page_cells, word/line are computed)
    pdf_resource<PAGE_CELLS>& get_char_cells() { return page_cells; }
    pdf_resource<PAGE_CELLS>& get_word_cells() { return word_cells; }
    pdf_resource<PAGE_CELLS>& get_line_cells() { return line_cells; }

    bool has_word_cells() const { return word_cells_created; }
    bool has_line_cells() const { return line_cells_created; }

    // Create word/line cells from page_cells
    void create_word_cells(const decode_page_config& config);
    void create_line_cells(const decode_page_config& config);

    // JSON serialization
    nlohmann::json get(const decode_page_config& config);

    void decode_page(const decode_page_config& config);

    // Get timing information for this page
    pdf_timings& get_timings() { return timings; }
    const pdf_timings& get_timings() const { return timings; }

  private:

    void decode_dimensions();

    // Resources
    void decode_resources(const decode_page_config& config);
    void decode_resources_low_level(const decode_page_config& config);

    void decode_grphs();

    void decode_fonts();

    void decode_xobjects();

    // Contents
    void decode_contents(const decode_page_config& config);

    void decode_annots_from_qpdf();
    void extract_page_cells_from_annot(QPDFObjectHandle annots);
    
    void rotate_contents();

    void sanitise_contents(std::string page_boundary);

  private:

    QPDFObjectHandle qpdf_page;

    int page_number;

    QPDFObjectHandle qpdf_resources;
    QPDFObjectHandle qpdf_grphs;
    QPDFObjectHandle qpdf_fonts;
    QPDFObjectHandle qpdf_xobjects;

    // Debug-only: populated when config.populate_json_objects is true
    nlohmann::json json_page;
    nlohmann::json json_annots;
    
    pdf_resource<PAGE_DIMENSION> page_dimension;

    pdf_resource<PAGE_CELLS>  page_cells;
    pdf_resource<PAGE_SHAPES>  page_shapes;
    pdf_resource<PAGE_IMAGES> page_images;

    pdf_resource<PAGE_CELLS>  cells;
    pdf_resource<PAGE_SHAPES>  shapes;
    pdf_resource<PAGE_IMAGES> images;

    // Computed cell aggregations
    pdf_resource<PAGE_CELLS>  word_cells;
    pdf_resource<PAGE_CELLS>  line_cells;
    bool word_cells_created = false;
    bool line_cells_created = false;

    std::shared_ptr<pdf_resource<PAGE_GRPHS> > page_grphs;
    std::shared_ptr<pdf_resource<PAGE_FONTS> > page_fonts;
    std::shared_ptr<pdf_resource<PAGE_XOBJECTS> > page_xobjects;

    pdf_timings timings;
  };

  pdf_decoder<PAGE>::pdf_decoder(QPDFObjectHandle page, int page_num):
    qpdf_page(page),
    page_number(page_num),
    page_grphs(std::make_shared<pdf_resource<PAGE_GRPHS>>()),
    page_fonts(std::make_shared<pdf_resource<PAGE_FONTS>>()),
    page_xobjects(std::make_shared<pdf_resource<PAGE_XOBJECTS>>())
  {
  }

  pdf_decoder<PAGE>::~pdf_decoder()
  {
    LOG_S(INFO) << "releasing memory for pdf page decoder";    
  }

  int pdf_decoder<PAGE>::get_page_number()
  {
    return page_number;
  }

  nlohmann::json pdf_decoder<PAGE>::get(const decode_page_config& config)
  {
    bool keep_char_cells = config.keep_char_cells;
    bool keep_shapes = config.keep_shapes;
    bool keep_bitmaps = config.keep_bitmaps;
    bool do_sanitization = config.do_sanitization;

    LOG_S(INFO) << "pdf_decoder<PAGE>::get "
		<< "keep_char_cells: " << keep_char_cells << ", "
		<< "keep_shapes: " << keep_shapes << ", "
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

        if(keep_shapes)
          {
            original["shapes"] = page_shapes.get();
          }
	else
	  {
	    LOG_S(WARNING) << "skipping the serialization of `shapes` to json!";
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

          if(keep_shapes)
            {
              sanitized["shapes"] = shapes.get();
            }
        }
      	else
	  {
	    LOG_S(WARNING) << "skipping the serialization of `sanitzed` page to json!";
	  }	
    }

    return result;
  }

  void pdf_decoder<PAGE>::decode_page(const decode_page_config& config)
  {
    utils::timer global, local;

    if(config.populate_json_objects)
      {
	local.reset();
	json_page = to_json(qpdf_page);
	timings.add_timing(pdf_timings::KEY_TO_JSON_PAGE, local.get_time());
      }

    if(config.populate_json_objects)
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
      decode_resources(config);
      timings.add_timing(pdf_timings::KEY_DECODE_RESOURCES, local.get_time());
    }

    {
      local.reset();
      decode_contents(config);
      timings.add_timing(pdf_timings::KEY_DECODE_CONTENTS, local.get_time());
    }

    {
      local.reset();
      decode_annots_from_qpdf();
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

      sanitator.sanitize(config.page_boundary); // update the top-level bbox
      sanitator.sanitize(page_cells, config.page_boundary);
      sanitator.sanitize(page_shapes, config.page_boundary);
      sanitator.sanitize(page_images, config.page_boundary);
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

    if(config.do_sanitization)
      {
        local.reset();
        sanitise_contents(config.page_boundary);
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

    page_dimension.execute(qpdf_page);
  }

  void pdf_decoder<PAGE>::decode_resources(const decode_page_config& config)
  {
    LOG_S(INFO) << __FUNCTION__;

    bool has_resources = qpdf_page.hasKey("/Resources");
    bool has_parent = qpdf_page.hasKey("/Parent");

    if(has_resources and has_parent)
      {
        auto parent = qpdf_page.getKey("/Parent");
        if(parent.hasKey("/Resources"))
          {
            qpdf_resources = parent.getKey("/Resources");
            decode_resources_low_level(config);
          }
        else
          {
            LOG_S(INFO) << "parent of page has no resources!";
          }

        // This might overwrite resources from the parent ...
        qpdf_resources = qpdf_page.getKey("/Resources");
        decode_resources_low_level(config);
      }
    else if(has_resources)
      {
        qpdf_resources = qpdf_page.getKey("/Resources");
        decode_resources_low_level(config);
      }
    else if(has_parent)
      {
        auto parent = qpdf_page.getKey("/Parent");
        if(parent.hasKey("/Resources"))
          {
            qpdf_resources = parent.getKey("/Resources");

            LOG_S(INFO) << "parent of page has resources!";

            decode_resources_low_level(config);
          }
        else
          {
            LOG_S(ERROR) << "page has no /Resources nor a /Parent with /Resources.";
          }
      }
    else
      {
        LOG_S(WARNING) << "page does not have any resources!";
      }

    {
      auto font_keys = page_fonts->keys();

      LOG_S(INFO) << "fonts: " << font_keys.size();
      for(auto key:font_keys)
        {
          LOG_S(INFO) << " -> font-key: '" << key << "'";
        }
    }
  }

  void pdf_decoder<PAGE>::decode_resources_low_level(const decode_page_config& config)
  {
    LOG_S(INFO) << __FUNCTION__;

    if(qpdf_resources.hasKey("/ExtGState"))
      {
        qpdf_grphs = qpdf_resources.getKey("/ExtGState");
        decode_grphs();
      }
    else
      {
        LOG_S(WARNING) << "page does not have any graphics state!";
      }

    if(qpdf_resources.hasKey("/Font"))
      {
        qpdf_fonts = qpdf_resources.getKey("/Font");
        decode_fonts();
      }
    else
      {
        LOG_S(WARNING) << "page does not have any fonts!";
      }

    if(qpdf_resources.hasKey("/XObject"))
      {
        qpdf_xobjects = qpdf_resources.getKey("/XObject");
        decode_xobjects();
      }
    else
      {
        LOG_S(WARNING) << "page does not have any xobjects!";
      }
  }

  void pdf_decoder<PAGE>::decode_grphs()
  {
    LOG_S(INFO) << __FUNCTION__;

    page_grphs->set(qpdf_grphs, timings);
  }

  void pdf_decoder<PAGE>::decode_fonts()
  {
    LOG_S(INFO) << __FUNCTION__;

    page_fonts->set(qpdf_fonts, timings);
  }

  void pdf_decoder<PAGE>::decode_xobjects()
  {
    LOG_S(INFO) << __FUNCTION__;

    page_xobjects->set(qpdf_xobjects, timings);
  }

  void pdf_decoder<PAGE>::decode_contents(const decode_page_config& config)
  {
    LOG_S(INFO) << __FUNCTION__;

    QPDFPageObjectHelper          qpdf_page_object(qpdf_page);
    std::vector<QPDFObjectHandle> contents = qpdf_page_object.getPageContents();

    pdf_decoder<STREAM> stream_decoder(config,

				       page_dimension,
				       page_cells,
                                       page_shapes,
				       page_images,
                                       page_fonts,
				       page_grphs,
                                       page_xobjects,

				       timings);

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

  /* // legacy decode_annots - commented out, declaration removed
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
  */ // end legacy decode_annots

  void pdf_decoder<PAGE>::decode_annots_from_qpdf()
  {
    if(qpdf_page.isDictionary())
      {
	if(qpdf_page.hasKey("/Annot"))
	  {
	    LOG_S(INFO) << "found `/Annot`";
	    QPDFObjectHandle annot = qpdf_page.getKey("/Annot");
	    extract_page_cells_from_annot(annot);
	  }

	if(qpdf_page.hasKey("/Annots"))
	  {
	    LOG_S(INFO) << "found `/Annots`";
	    QPDFObjectHandle annots = qpdf_page.getKey("/Annots");
	    extract_page_cells_from_annot(annots);
	  }    
      }
  }

  // FIXME: we need to expand the capabilities of the annotation extraction!
  void pdf_decoder<PAGE>::extract_page_cells_from_annot(QPDFObjectHandle annots)
  {
    if(not annots.isArray())
      {
	LOG_S(WARNING) << "annotation is not an array";
	return;
      }

    for(int l=0; l<annots.getArrayNItems(); l++)
      {
	QPDFObjectHandle annot = annots.getArrayItem(l);

	// auto annot_json = to_json(annot);
	// LOG_S(INFO) << "annot " << l << ": " << annot_json.dump(2);
	
	auto [has_type, type] = to_string(annot, "/Type");
	if(not has_type)
	  {
	    continue;
	  }

	auto [has_subtype, subtype] = to_string(annot, "/Subtype");
	if(not has_subtype)
	  {
	    continue;
	  }
	
	LOG_S(INFO) << "type: " << type << ", subtype: " << subtype;
	
	if(type=="/Annot" and
	   subtype=="/Widget" and
	   annot.hasKey("/Rect") and
	   annot.getKey("/Rect").isArray() and
	   annot.hasKey("/V") and
	   annot.hasKey("/T")
	   )
	  {
	    auto rect = annot.getKey("/Rect");

	    std::array<double, 4> bbox = {0., 0., 0., 0.};
	    for(int l=0; l<rect.getArrayNItems() and l<bbox.size(); l++)
	      {
		QPDFObjectHandle num = rect.getArrayItem(l);
		if(num.isNumber())
		  {
		    bbox[l] = num.getNumericValue();
		  }
	      }
	    
	    auto [has_value, text] = to_string(annot, "/V");
	    if(not has_value)
	      {
		text = "<unknown>";
	      }
	    
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
	else
	  {
	    LOG_S(WARNING) << "annot is being skipped!";
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
    page_shapes.rotate(angle, delta);
    page_images.rotate(angle, delta);
  }

  void pdf_decoder<PAGE>::sanitise_contents(std::string page_boundary)
  {
    LOG_S(INFO) << __FUNCTION__;

    {
      shapes = page_shapes;
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

  void pdf_decoder<PAGE>::create_word_cells(const decode_page_config& config)
  {
    LOG_S(INFO) << __FUNCTION__;
    utils::timer timer;

    pdf_sanitator<PAGE_CELLS> sanitizer;

    word_cells = sanitizer.create_word_cells(page_cells, config);

    // Remove duplicates (quadratic but necessary)
    sanitizer.remove_duplicate_cells(word_cells, 0.5, true);

    word_cells_created = true;

    LOG_S(INFO) << "#-page-cells: " << page_cells.size() << " -> #-word-cells: " << word_cells.size();
    timings.add_timing(pdf_timings::KEY_CREATE_WORD_CELLS, timer.get_time());
  }

  void pdf_decoder<PAGE>::create_line_cells(const decode_page_config& config)
  {
    LOG_S(INFO) << __FUNCTION__;
    utils::timer timer;

    pdf_sanitator<PAGE_CELLS> sanitizer;

    line_cells = sanitizer.create_line_cells(page_cells, config);

    // Remove duplicates (quadratic but necessary)
    sanitizer.remove_duplicate_cells(line_cells, 0.5, true);

    line_cells_created = true;

    LOG_S(INFO) << "#-page-cells: " << page_cells.size() << " -> #-line-cells: " << line_cells.size();
    timings.add_timing(pdf_timings::KEY_CREATE_LINE_CELLS, timer.get_time());
  }

}

#endif
