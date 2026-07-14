//-*-C++-*-

#ifndef PDF_STREAM_DECODER_H
#define PDF_STREAM_DECODER_H

namespace pdflib
{

  template<>
  class pdf_decoder<STREAM>
  {

  public:

    pdf_decoder(const decode_config& config,

                page_item<PAGE_DIMENSION>& page_dimension_,
                page_item<PAGE_CELLS>&     page_cells_,
                page_item<PAGE_SHAPES>&     page_shapes_,
                page_item<PAGE_IMAGES>&    page_images_,

                std::shared_ptr<pdf_resource<PAGE_FONTS>>       page_fonts_,
                std::shared_ptr<pdf_resource<PAGE_GRPHS>>       page_grphs_,
                std::shared_ptr<pdf_resource<PAGE_COLORSPACES>> page_colorspaces_,
                std::shared_ptr<pdf_resource<PAGE_XOBJECTS>>    page_xobjects_,

                pdf_render_instructions& instructions,

                pdf_timings& timings);

    ~pdf_decoder();

    void print();

    std::unordered_set<std::string> get_unknown_operators();

    // decode the qpdf-stream
    void decode(QPDFObjectHandle& content);

    // methods used to interprete the stream
    void interprete(std::vector<qpdf_stream_instruction>& parameters);

  private:

    bool update_stack(std::vector<pdf_state<GLOBAL> >& stack_,
                      int                              stack_count_);

    void interprete(std::vector<qpdf_stream_instruction>& stream_,
                    std::vector<qpdf_stream_instruction>& parameters_);


    void interprete_stream(std::vector<qpdf_stream_instruction>& parameters);

    pdf_state<GLOBAL>&  current_global_state(); // get current global state
    pdf_state<TEXT>&    current_text_state(); // get current text state
    pdf_state<SHAPE>&   current_shape_state(); // get current shape state
    pdf_state<GRPH>&    current_graphic_state(); // get current graphics state
    pdf_state<BITMAP>&  current_bitmap_state(); // get current bitmap state

    void q();
    void Q();
    
    void execute_operator(qpdf_stream_instruction op,
                          std::vector<qpdf_stream_instruction>& parameters);
    
    void do_image(const std::string& xobj_name,
		  const xobject_subtype_name& xobj_subtype);
    
    void do_form(const std::string& xobj_name,
		 const xobject_subtype_name& xobj_subtype);

    void do_postscript(const std::string& xobj_name,
		       const xobject_subtype_name& xobj_subtype);

    // marked-content (BMC/BDC ... EMC) tracking, used to honor /ActualText
    // replacement text (PDF 32000-1, section 14.9.4)
    struct marked_content_entry
    {
      bool        has_actual_text = false;
      std::string actual_text     = ""; // UTF-8
      std::size_t cells_begin     = 0;  // page_cells.size() at BDC/BMC
    };

    void begin_marked_content(std::vector<qpdf_stream_instruction>& parameters);
    void end_marked_content();

    void apply_actual_text(const marked_content_entry& entry);

  private:

    const decode_config& config;

    page_item<PAGE_DIMENSION>& page_dimension;
    page_item<PAGE_CELLS>&     page_cells;
    page_item<PAGE_SHAPES>&     page_shapes;
    page_item<PAGE_IMAGES>&    page_images;

    std::shared_ptr<pdf_resource<PAGE_FONTS>>       page_fonts;
    std::shared_ptr<pdf_resource<PAGE_GRPHS>>       page_grphs;
    std::shared_ptr<pdf_resource<PAGE_COLORSPACES>> page_colorspaces;
    std::shared_ptr<pdf_resource<PAGE_XOBJECTS>>    page_xobjects;

    pdf_render_instructions& instructions;

    pdf_timings& timings;

    std::unordered_set<std::string> unknown_operators;

    std::vector<qpdf_stream_instruction> stream;
    std::vector<pdf_state<GLOBAL> > stack;

    int stack_count;

    // BDC/EMC pairs must balance within a single content stream (PDF 32000-1,
    // 14.6), so this stack is per-decoder; cells created by nested form
    // XObjects still land in the shared page_cells and are covered by the
    // recorded cells_begin index.
    std::vector<marked_content_entry> marked_content_stack;
  };

  pdf_decoder<STREAM>::pdf_decoder(const decode_config& config_,

                                   page_item<PAGE_DIMENSION>& page_dimension_,
                                   page_item<PAGE_CELLS>&     page_cells_,
                                   page_item<PAGE_SHAPES>&     page_shapes_,
                                   page_item<PAGE_IMAGES>&    page_images_,

                                   std::shared_ptr<pdf_resource<PAGE_FONTS>>       page_fonts_,
                                   std::shared_ptr<pdf_resource<PAGE_GRPHS>>       page_grphs_,
                                   std::shared_ptr<pdf_resource<PAGE_COLORSPACES>> page_colorspaces_,

                                   std::shared_ptr<pdf_resource<PAGE_XOBJECTS>>    page_xobjects_,

                                   pdf_render_instructions& instructions_,

				   pdf_timings& timings):
    config(config_),

    page_dimension(page_dimension_),
    page_cells(page_cells_),
    page_shapes(page_shapes_),
    page_images(page_images_),

    page_fonts(page_fonts_),
    page_grphs(page_grphs_),
    page_colorspaces(page_colorspaces_),

    page_xobjects(page_xobjects_),

    instructions(instructions_),

    timings(timings),

    unknown_operators({}),
    stream({}),
    stack({}),

    stack_count(0)
  {
    LOG_S(INFO) << __FUNCTION__;
  }

  pdf_decoder<STREAM>::~pdf_decoder()
  {
    if(unknown_operators.size()>0)
      {
        LOG_S(WARNING) << "============= ~pdf_decoder ===================";
        for(auto item:unknown_operators)
          {
            LOG_S(WARNING) << "unknown operator: " << item;
          }
        LOG_S(WARNING) << "==============================================";
      }
  }

  std::unordered_set<std::string> pdf_decoder<STREAM>::get_unknown_operators()
  {
    LOG_S(INFO) << __FUNCTION__;
    return unknown_operators;
  }

  void pdf_decoder<STREAM>::print()
  {
    LOG_S(INFO) << __FUNCTION__;
    for(auto row:stream)
      {
        LOG_S(INFO) << std::setw(12) << row.key << " | " << row.val;
      }
  }

  void pdf_decoder<STREAM>::decode(QPDFObjectHandle& qpdf_content)
  {
    LOG_S(INFO) << __FUNCTION__;

    qpdf_stream_decoder decoder(stream);
    decoder.decode(qpdf_content);
  }

  void pdf_decoder<STREAM>::interprete(std::vector<qpdf_stream_instruction>& parameters)
  {
    LOG_S(INFO) << __FUNCTION__;

    // initialise the stack
    if(stack.size()==0)
      {
        //stack.clear();

        pdf_state<GLOBAL> state(config,
				page_cells,
				page_shapes,
				page_images,
				page_fonts,
				page_grphs,
				page_colorspaces,
				instructions);

        stack.push_back(state);
      }

    interprete_stream(parameters);
  }

  bool pdf_decoder<STREAM>::update_stack(std::vector<pdf_state<GLOBAL> >& stack_,
                                         int                              stack_count_)
  {
    stack       = stack_;
    stack_count = stack_count_;

    if(stack.size()>0 and page_fonts->keys()!=current_global_state().page_fonts->keys())
      {
        pdf_state<GLOBAL> state(config,
				page_cells,
				page_shapes,
				page_images,
				page_fonts,
				page_grphs,
				page_colorspaces,
				instructions);

        state = stack.back();

        stack.push_back(state);

        return true;
      }

    return false;
  }

  void pdf_decoder<STREAM>::interprete(std::vector<qpdf_stream_instruction>& stream_,
                                       std::vector<qpdf_stream_instruction>& parameters_)
  {
    LOG_S(INFO) << __FUNCTION__;

    stream = stream_;

    interprete_stream(parameters_);

    if(parameters_.size()!=0)
      {
        LOG_S(ERROR) << "Finishing a `Do` with nonzero number of parameters!";
      }
  }

  void pdf_decoder<STREAM>::interprete_stream(std::vector<qpdf_stream_instruction>& parameters)
  {
    LOG_S(INFO) << __FUNCTION__;

    for(int l=0; l<stream.size(); l++)
      {
        qpdf_stream_instruction& inst = stream[l];

        if(inst.key=="operator")
          {
            for(auto itr=parameters.begin(); itr!=parameters.end(); )
              {
                if(itr->key=="null" and itr->val=="null") // this can happen if you have an empty array/dict
                  {
                    LOG_S(ERROR) << "\t" << std::setw(12) << itr->key << " | " << itr->val << " => erasing ...";
                    itr = parameters.erase(itr);
                  }
                else
                  {
                    LOG_S(INFO) << "\t" << std::setw(12) << itr->key << " | " << itr->val;
                    itr++;
                  }
              }
            LOG_S(INFO) << " --> " << std::setw(12) << inst.key << " | " << inst.val;

            execute_operator(inst, parameters);

            parameters.clear();
          }
        else
          {
            parameters.push_back(inst);
          }
      }

    if(marked_content_stack.size()>0)
      {
        LOG_S(WARNING) << "content stream ended with " << marked_content_stack.size()
                       << " unbalanced BMC/BDC operator(s): pending /ActualText is not applied";
        marked_content_stack.clear();
      }
  }

  // get current global state
  pdf_state<GLOBAL>& pdf_decoder<STREAM>::current_global_state()
  {
    if(stack.size()==0)
      {
        std::stringstream message;
        message << "stack-size is zero in " << __FILE__ << ":" << __LINE__;

        LOG_S(ERROR) << message.str();
        throw std::logic_error(message.str());
      }

    pdf_state<GLOBAL>& state = stack.back();
    return state;
  }

  // get current text state
  pdf_state<TEXT>& pdf_decoder<STREAM>::current_text_state()
  {
    return current_global_state().text_state;
  }

  // get current shape state
  pdf_state<SHAPE>& pdf_decoder<STREAM>::current_shape_state()
  {
    return current_global_state().shape_state;
  }

  // get current graphics state
  pdf_state<GRPH>& pdf_decoder<STREAM>::current_graphic_state()
  {
    return current_global_state().grph_state;
  }

  // get current bitmap state
  pdf_state<BITMAP>& pdf_decoder<STREAM>::current_bitmap_state()
  {
    return current_global_state().bitmap_state;
  }

  void pdf_decoder<STREAM>::q()
  {
    if(stack.size()==0)
      {
        pdf_state<GLOBAL> state(config,
				page_cells,
				page_shapes,
				page_images,
				page_fonts,
				page_grphs,
				page_colorspaces,
				instructions);

        stack.push_back(state);
      }
    else
      {
        pdf_state<GLOBAL> state(stack.back());
        stack.push_back(state);
      }

    stack_count += 1;
  }

  void pdf_decoder<STREAM>::Q()
  {
    if(stack.size()>0)
      {
        stack.pop_back();
      }
    else
      {
        LOG_S(ERROR) << "invoking 'Q' on empty stack!";
        //throw std::logic_error(__FILE__);
      }
  }

  void pdf_decoder<STREAM>::do_image(const std::string& xobj_name,
                                     const xobject_subtype_name& xobj_subtype)
  {
    LOG_S(INFO) << "Do_Image: image with `" << xobj_name << "`";

    const pdf_resource<PAGE_XOBJECT_IMAGE>& xobj = page_xobjects->get_image(xobj_name);

    utils::timer do_image_timer;
    current_bitmap_state().Do_image(xobj_name,
                                    xobj,
                                    current_shape_state().get_clip_state());
    double do_image_seconds = do_image_timer.get_time();
    timings.add_timing(pdf_timings::KEY_DO_IMAGE_TOTAL, do_image_seconds);
    timings.note_attributed(do_image_seconds);
  }

  void pdf_decoder<STREAM>::do_form(const std::string& xobj_name,
                                    const xobject_subtype_name& xobj_subtype)
  {
    LOG_S(INFO) << "Do_Form: XObject with name `" << xobj_name << "`";

    // Time the whole call; the "machinery" cost (child-resource allocation,
    // graphics-state copies via q()/Q(), stack copy in update_stack(), ...) is
    // derived as: total - set() - parse_stream() - nested interprete(). The
    // subtracted parts are bucketed under their own keys, so this residual is
    // disjoint from them.
    utils::timer do_form_timer;
    double set_seconds          = 0.0;
    double parse_stream_seconds = 0.0;
    double interprete_seconds   = 0.0;

    const pdf_resource<PAGE_XOBJECT_FORM>& xobj = page_xobjects->get_form(xobj_name);

    std::array<double, 4> bbox = xobj.get_bbox();
    LOG_S(INFO) << "form bbox: ["
		<< bbox.at(0) << ", "
      		<< bbox.at(1) << ", "
      		<< bbox.at(2) << ", "
      		<< bbox.at(3) << "]";

    // check if (1) we keep data outside the page_boundary and
    // (2) if bbox is outside of page_boundary
    // please implement

    // create child resources with parent link (no deep copy)
    auto page_fonts_       = std::make_shared<pdf_resource<PAGE_FONTS>>(page_fonts);
    auto page_grphs_       = std::make_shared<pdf_resource<PAGE_GRPHS>>(page_grphs);
    auto page_colorspaces_ = std::make_shared<pdf_resource<PAGE_COLORSPACES>>(page_colorspaces);
    auto page_xobjects_    = std::make_shared<pdf_resource<PAGE_XOBJECTS>>(page_xobjects);

    // parse the resources of the xobject into the child resources
    {
      utils::timer set_timer;

      if(xobj.has_fonts())
        {
          QPDFObjectHandle xobj_fonts = xobj.get_fonts();
          page_fonts_->set(xobj_fonts, timings);
        }

      if(xobj.has_grphs())
        {
          QPDFObjectHandle xobj_grphs = xobj.get_grphs();
          page_grphs_->set(xobj_grphs, timings);
        }

      if(xobj.has_colorspaces())
        {
          QPDFObjectHandle xobj_colorspaces = xobj.get_colorspaces();
          page_colorspaces_->set(xobj_colorspaces);
        }

      if(xobj.has_xobjects())
        {
          QPDFObjectHandle xobj_xobjects = xobj.get_xobjects();
          page_xobjects_->set(xobj_xobjects, timings);
        }

      set_seconds = set_timer.get_time();
    }

    {
      // push-back the stack
      this->q();

      // transform coordinate system
      current_global_state().cm(xobj.get_matrix());

      {
        utils::timer parse_stream_timer;
        std::vector<qpdf_stream_instruction> insts = xobj.parse_stream();
        parse_stream_seconds = parse_stream_timer.get_time();
        timings.add_timing(pdf_timings::KEY_PARSE_STREAM_TOTAL, parse_stream_seconds);
        timings.note_attributed(parse_stream_seconds);

        pdf_decoder<STREAM> new_stream(config,

                                       page_dimension,
                                       page_cells,
                                       page_shapes,
                                       page_images,

                                       page_fonts_,
                                       page_grphs_,
                                       page_colorspaces_,
                                       page_xobjects_,

                                       instructions,

                                       timings);

        bool updated_stack = new_stream.update_stack(stack, stack_count);

        // copy the stack
        std::vector<qpdf_stream_instruction> parameters;
        {
          utils::timer interprete_timer;
          new_stream.interprete(insts, parameters);
          interprete_seconds = interprete_timer.get_time();
        }

        if(updated_stack)
          {
            new_stream.Q();
          }

        auto unkown_ops = new_stream.get_unknown_operators();
        for(auto item:unkown_ops)
          {
            unknown_operators.insert(item);
          }
      }

      // pop-back the stack
      this->Q();
    }

    // residual = state copies, child-resource allocation, stack handling, ...
    double machinery_seconds = do_form_timer.get_time()
                             - set_seconds - parse_stream_seconds - interprete_seconds;
    timings.add_timing(pdf_timings::KEY_DO_FORM_MACHINERY, machinery_seconds);
    timings.note_attributed(machinery_seconds);

    LOG_S(INFO) << "ending the execution of FORM XObject with name `" << xobj_name << "`";

  }

  void pdf_decoder<STREAM>::do_postscript(const std::string& xobj_name,
                                          const xobject_subtype_name& xobj_subtype)
  {
    LOG_S(WARNING) << "unsupported xobject subtype (PostScript) with name " << xobj_name;
  }

  // BMC has one operand (tag), BDC has two (tag + properties). The properties
  // operand of BDC is either an inline dictionary or a name referring to the
  // /Properties resource dictionary; only inline dictionaries are inspected
  // for /ActualText here (the named form is rare for /ActualText, which is
  // content-specific by nature).
  void pdf_decoder<STREAM>::begin_marked_content(std::vector<qpdf_stream_instruction>& parameters)
  {
    marked_content_entry entry;
    entry.cells_begin = page_cells.size();

    if(config.apply_actual_text and parameters.size()>=2)
      {
        qpdf_stream_instruction& props = parameters.back();

        if(props.is_dict())
          {
            QPDFObjectHandle dict = props.obj;

            if(dict.hasKey("/ActualText") and dict.getKey("/ActualText").isString())
              {
                entry.has_actual_text = true;
                // getUTF8Value decodes UTF-16BE (BOM) and PDFDoc strings
                entry.actual_text = dict.getKey("/ActualText").getUTF8Value();

                LOG_S(INFO) << "BDC with /ActualText: '" << entry.actual_text << "'";
              }
          }
        else if(props.obj.isName())
          {
            LOG_S(INFO) << "BDC with named properties " << props.obj.getName()
                        << ": not resolved against /Properties, /ActualText (if any) is ignored";
          }
      }

    marked_content_stack.push_back(entry);
  }

  void pdf_decoder<STREAM>::end_marked_content()
  {
    if(marked_content_stack.empty())
      {
        LOG_S(WARNING) << "EMC without matching BMC/BDC: ignoring";
        return;
      }

    marked_content_entry entry = marked_content_stack.back();
    marked_content_stack.pop_back();

    if(entry.has_actual_text)
      {
        apply_actual_text(entry);
      }
  }

  // Substitute the /ActualText replacement string into the text cells created
  // inside the marked-content span. Substitution requires the span to have
  // drawn text cells: /ActualText over pure graphics (the alternate-text-like
  // usage) has no anchor cell and is skipped, and a replacement string that is
  // implausibly long for the glyph run (descriptive misuse, /Alt semantics in
  // the wrong key) is rejected.
  void pdf_decoder<STREAM>::apply_actual_text(const marked_content_entry& entry)
  {
    std::size_t begin = entry.cells_begin;
    std::size_t end   = page_cells.size();

    if(begin>end)
      {
        LOG_S(ERROR) << "invalid marked-content cell range [" << begin << ", " << end << ")";
        return;
      }

    std::size_t num_cells = end-begin;

    if(num_cells==0)
      {
        LOG_S(WARNING) << "skipping /ActualText ('" << entry.actual_text
                       << "'): no text cells in marked-content span (non-text content)";
        return;
      }

    std::vector<std::string> chars = utils::string::split_unicode_characters(entry.actual_text);

    if(chars.size() > std::max<std::size_t>(8, 4*num_cells))
      {
        LOG_S(WARNING) << "ignoring /ActualText ('" << entry.actual_text
                       << "'): " << chars.size() << " character(s) is implausible for a span of "
                       << num_cells << " cell(s)";
        return;
      }

    if(chars.size()==0)
      {
        // an empty /ActualText declares the span to be non-textual content
        // (eg a purely visual line-break hyphen)
        LOG_S(INFO) << "empty /ActualText: removing " << num_cells << " cell(s)";
        page_cells.erase(page_cells.begin()+begin, page_cells.begin()+end);
        return;
      }

    if(chars.size()==num_cells)
      {
        // preserve the per-glyph geometry
        for(std::size_t i=0; i<num_cells; i++)
          {
            page_item<PAGE_CELL>& cell = page_cells[begin+i];

            if(cell.text!=chars.at(i))
              {
                LOG_S(INFO) << "/ActualText substitution: '" << cell.text
                            << "' -> '" << chars.at(i) << "'";

                cell.text = chars.at(i);
                cell.left_to_right = (not utils::string::is_right_to_left(cell.text));
              }
          }
        return;
      }

    // glyph count and character count differ (composed accents, ligatures,
    // hyphenation): the first cell carries the full replacement string and the
    // union of the span geometry, the remaining cells are removed
    LOG_S(INFO) << "/ActualText substitution: " << num_cells << " cell(s) -> '"
                << entry.actual_text << "'";

    page_item<PAGE_CELL>& first = page_cells[begin];

    // baseline direction of the span, taken from the first cell (r_0 -> r_1)
    const double dir_x = first.r_x1-first.r_x0;
    const double dir_y = first.r_y1-first.r_y0;

    double max_proj = first.r_x1*dir_x + first.r_y1*dir_y;

    for(std::size_t i=begin+1; i<end; i++)
      {
        page_item<PAGE_CELL>& cell = page_cells[i];

        first.x0 = std::min(first.x0, cell.x0);
        first.y0 = std::min(first.y0, cell.y0);
        first.x1 = std::max(first.x1, cell.x1);
        first.y1 = std::max(first.y1, cell.y1);

        // extend the baseline rect along the text flow: keep the leading edge
        // (r_0/r_3) of the first cell, take the trailing edge (r_1/r_2) of the
        // cell that reaches furthest along the baseline. Not simply the last
        // cell: zero-advance overlay glyphs (composed accents) trail *behind*
        // the base letter. Projection onto the baseline keeps rotated text
        // oriented correctly.
        double proj = cell.r_x1*dir_x + cell.r_y1*dir_y;
        if(proj>max_proj)
          {
            max_proj = proj;

            first.r_x1 = cell.r_x1;
            first.r_y1 = cell.r_y1;
            first.r_x2 = cell.r_x2;
            first.r_y2 = cell.r_y2;
          }
      }

    first.text = entry.actual_text;
    first.left_to_right = (not utils::string::is_right_to_left(first.text));

    page_cells.erase(page_cells.begin()+(begin+1), page_cells.begin()+end);
  }

  void pdf_decoder<STREAM>::execute_operator(qpdf_stream_instruction              op,
                                             std::vector<qpdf_stream_instruction>& parameters)
  {
    pdf_operator::operator_name name = pdf_operator::to_name(op.val);

    switch(name)
      {

        /**************************************************
         ***  General graphics state
         **************************************************/

      case pdf_operator::w:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().w(parameters);
        }
        break;

      case pdf_operator::J:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().J(parameters);
        }
        break;

      case pdf_operator::j:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().j(parameters);
        }
        break;

      case pdf_operator::M:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().M(parameters);
        }
        break;

      case pdf_operator::d:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().d(parameters);
        }
        break;

      case pdf_operator::ri:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().ri(parameters);
        }
        break;

      case pdf_operator::i:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().i(parameters);
        }
        break;

      case pdf_operator::gs:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().gs(parameters);
        }
        break;

        /**************************************************
         ***  Special graphics state
         **************************************************/

      case pdf_operator::q:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          this->q();
        }
        break;

      case pdf_operator::Q:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          this->Q();
        }
        break;

      case pdf_operator::cm:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_global_state().cm(parameters);
        }
        break;

        /**************************************************
         ***  XObjects
         **************************************************/

      case pdf_operator::Do:
        {
          LOG_S(INFO) << "executing " << to_string(name);

          std::string xobj_name = parameters[0].to_utf8_string();

          if(not page_xobjects->has(xobj_name))
            {
              LOG_S(ERROR) << "unknown xobject with name `" << xobj_name << "`";
              return;
            }

          xobject_subtype_name xobj_subtype = page_xobjects->get_subtype(xobj_name);

          switch(xobj_subtype)
            {
            case XOBJECT_IMAGE: { this->do_image(xobj_name, xobj_subtype); } break;

            case XOBJECT_FORM: { this->do_form(xobj_name, xobj_subtype); } break;
	      
            case XOBJECT_POSTSCRIPT: { this->do_postscript(xobj_name, xobj_subtype); } break;

            default:
              {
                LOG_S(ERROR) << "unknown xobject subtype with name " << xobj_name;
              }
            }
        }
        break;

        /**************************************************
         ***  color-schemes
         **************************************************/

      case pdf_operator::CS:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().CS(parameters);
        }
        break;

      case pdf_operator::cs:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().cs(parameters);
        }
        break;

      case pdf_operator::SC:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().SC(parameters);
        }
        break;

      case pdf_operator::SCN:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().SCN(parameters);
        }
        break;

      case pdf_operator::sc:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().sc(parameters);
        }
        break;

      case pdf_operator::scn:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().scn(parameters);
        }
        break;

      case pdf_operator::G:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().G(parameters);
        }
        break;

      case pdf_operator::g:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().g(parameters);
        }
        break;

      case pdf_operator::RG:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().RG(parameters);
        }
        break;

      case pdf_operator::rg:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().rg(parameters);
        }
        break;

      case pdf_operator::K:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().K(parameters);
        }
        break;

      case pdf_operator::k:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_graphic_state().k(parameters);
        }
        break;

        /**************************************************
         ***  group-objects
         **************************************************/

      case pdf_operator::BT:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          if(page_fonts->keys()!=current_global_state().page_fonts->keys())
            {
              LOG_S(ERROR) << "page_fonts keys mismatch with current global state";
            }

          current_text_state().BT();
        }
        break;

      case pdf_operator::ET:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().ET();
        }
        break;

      case pdf_operator::BX:
        {
          LOG_S(INFO) << "executing " << to_string(name);
        }
        break;

      case pdf_operator::EX:
        {
          LOG_S(INFO) << "executing " << to_string(name);
        }
        break;

      case pdf_operator::BMC:
      case pdf_operator::BDC:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          begin_marked_content(parameters);
        }
        break;

      case pdf_operator::EMC:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          end_marked_content();
        }
        break;

      case pdf_operator::BI:
        {
          LOG_S(INFO) << "executing " << to_string(name);
        }
        break;

      case pdf_operator::ID:
        {
          LOG_S(INFO) << "executing " << to_string(name);
        }
        break;

      case pdf_operator::EI:
        {
          LOG_S(INFO) << "executing " << to_string(name);
        }
        break;
	
        /**************************************************
         ***  text-state
         **************************************************/

      case pdf_operator::Tc:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().Tc(parameters);
        }
        break;

      case pdf_operator::Tw:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().Tw(parameters);
        }
        break;

      case pdf_operator::Tz:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().Tz(parameters);
        }
        break;

      case pdf_operator::TL:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().TL(parameters);
        }
        break;

      case pdf_operator::Tf:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().Tf(parameters);
        }
        break;

      case pdf_operator::Tr:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().Tr(parameters);
        }
        break;

      case pdf_operator::Ts:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().Ts(parameters);
        }
        break;

        /**************************************************
         ***  text-positioning
         **************************************************/

      case pdf_operator::Td:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().Td(parameters);
        }
        break;

      case pdf_operator::TD:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().TD(parameters);
        }
        break;

      case pdf_operator::Tm:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().Tm(parameters);
        }
        break;

      case pdf_operator::TStar:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().TStar(parameters);
        }
        break;

        /**************************************************
         ***  text-showing
         **************************************************/

      case pdf_operator::Tj:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().Tj(parameters, stack_count);
        }
        break;

      case pdf_operator::TJ:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_text_state().TJ(parameters, stack_count);
        }
        break;

      case pdf_operator::accent:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          assert(parameters.size()==1);

          std::vector<qpdf_stream_instruction> TStar_params = {};
          current_text_state().TStar(TStar_params);

          std::vector<qpdf_stream_instruction> Tj_params = {parameters[0]};
          current_text_state().Tj(Tj_params, stack_count);
        }
        break;

      case pdf_operator::double_accent:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          assert(parameters.size()==3);

          std::vector<qpdf_stream_instruction> Tw_params = {parameters[0]};
          current_text_state().Tw(Tw_params);

          std::vector<qpdf_stream_instruction> Tc_params = {parameters[1]};
          current_text_state().Tc(Tc_params);

          std::vector<qpdf_stream_instruction> TStar_params = {};
          current_text_state().TStar(TStar_params);

          std::vector<qpdf_stream_instruction> Tj_params = {parameters[2]};
          current_text_state().Tj(Tj_params, stack_count);
        }
        break;

        /**************************************************
         ***  paths construction [page 132-133]
         **************************************************/

      case pdf_operator::m:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().m(parameters);
        }
        break;

      case pdf_operator::l:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().l(parameters);
        }
        break;

      case pdf_operator::c:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().c(parameters);
        }
        break;

      case pdf_operator::v:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().v(parameters);
        }
        break;

      case pdf_operator::y:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().y(parameters);
        }
        break;

      case pdf_operator::h:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().h(parameters);
        }
        break;

      case pdf_operator::re:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().re(parameters);
        }
        break;

        /**************************************************
         ***  path painting [page 132-133]
         **************************************************/

      case pdf_operator::s:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().s(parameters);
        }
        break;

      case pdf_operator::S:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().S(parameters);
        }
        break;

      case pdf_operator::f:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().f(parameters);
        }
        break;

      case pdf_operator::F:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().F(parameters);
        }
        break;

      case pdf_operator::fStar:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().fStar(parameters);
        }
        break;

      case pdf_operator::B:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().B(parameters);
        }
        break;

      case pdf_operator::BStar:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().BStar(parameters);
        }
        break;

      case pdf_operator::b:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().b(parameters);
        }
        break;

      case pdf_operator::bStar:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().bStar(parameters);
        }
        break;

      case pdf_operator::n:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().n(parameters);
        }
        break;

        /**************************************************
         ***  path clipping [page ...]
         **************************************************/

      case pdf_operator::W:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().W(parameters);
        }
        break;

      case pdf_operator::WStar:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          current_shape_state().WStar(parameters);
        }
        break;

      case pdf_operator::MP:
        {
          LOG_S(INFO) << "executing " << to_string(name);
        }
        break;

      case pdf_operator::DP:
        {
          LOG_S(INFO) << "executing " << to_string(name);
        }
        break;

      case pdf_operator::sh:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          //current_graphic_state().sh(parameters);
        }
        break;

        /**************************************************
         ***  Type 3 font metrics
         **************************************************/

      case pdf_operator::d0:
        {
          LOG_S(INFO) << "executing " << to_string(name);
        }
        break;

      case pdf_operator::d1:
        {
          LOG_S(INFO) << "executing " << to_string(name);
        }
        break;
	
        /**************************************************
         ***  other
         **************************************************/

      case pdf_operator::null:
        {
          LOG_S(WARNING) << "unknown operator with name: " << op.val;
          unknown_operators.insert(op.val);
        }
        break;

      default:
        {
          LOG_S(WARNING) << "ignored operator with name: " << op.val;
          unknown_operators.insert(op.val);
        }
      }
  }

}

#endif
