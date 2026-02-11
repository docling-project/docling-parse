//-*-C++-*-

#ifndef PDF_STREAM_DECODER_H
#define PDF_STREAM_DECODER_H

namespace pdflib
{

  template<>
  class pdf_decoder<STREAM>
  {

  public:

    pdf_decoder(const decode_page_config& config,

                pdf_resource<PAGE_DIMENSION>& page_dimension_,
                pdf_resource<PAGE_CELLS>&     page_cells_,
                pdf_resource<PAGE_SHAPES>&     page_shapes_,
                pdf_resource<PAGE_IMAGES>&    page_images_,

                std::shared_ptr<pdf_resource<PAGE_FONTS>>     page_fonts_,
                std::shared_ptr<pdf_resource<PAGE_GRPHS>>     page_grphs_,
                std::shared_ptr<pdf_resource<PAGE_XOBJECTS>>  page_xobjects_,

                pdf_timings& timings);

    ~pdf_decoder();

    void print();

    std::unordered_set<std::string> get_unknown_operators();

    // decode the qpdf-stream
    void decode(QPDFObjectHandle& content);

    // methods used to interprete the stream
    void interprete(std::vector<qpdf_instruction>& parameters);

  private:

    bool update_stack(std::vector<pdf_state<GLOBAL> >& stack_,
                      int                              stack_count_);

    void interprete(std::vector<qpdf_instruction>& stream_,
                    std::vector<qpdf_instruction>& parameters_);


    void interprete_stream(std::vector<qpdf_instruction>& parameters);

    pdf_state<GLOBAL>&  current_global_state(); // get current global state
    pdf_state<TEXT>&    current_text_state(); // get current text state
    pdf_state<SHAPE>&   current_shape_state(); // get current shape state
    pdf_state<GRPH>&    current_graphic_state(); // get current graphics state
    pdf_state<BITMAP>&  current_bitmap_state(); // get current bitmap state

    void q();
    void Q();
    
    void execute_operator(qpdf_instruction op,
                          std::vector<qpdf_instruction> parameters);
    
    void do_image(const std::string& xobj_name,
		  const xobject_subtype_name& xobj_subtype);
    
    void do_form(const std::string& xobj_name,
		 const xobject_subtype_name& xobj_subtype);

    void do_postscript(const std::string& xobj_name,
		       const xobject_subtype_name& xobj_subtype);

  private:

    const decode_page_config& config;

    pdf_resource<PAGE_DIMENSION>& page_dimension;
    pdf_resource<PAGE_CELLS>&     page_cells;
    pdf_resource<PAGE_SHAPES>&     page_shapes;
    pdf_resource<PAGE_IMAGES>&    page_images;

    std::shared_ptr<pdf_resource<PAGE_FONTS>>     page_fonts;
    std::shared_ptr<pdf_resource<PAGE_GRPHS>>     page_grphs;
    std::shared_ptr<pdf_resource<PAGE_XOBJECTS>>  page_xobjects;

    pdf_timings& timings;

    std::unordered_set<std::string> unknown_operators;

    std::vector<qpdf_instruction> stream;
    std::vector<pdf_state<GLOBAL> > stack;

    int stack_count;
  };

  pdf_decoder<STREAM>::pdf_decoder(const decode_page_config& config_,

                                   pdf_resource<PAGE_DIMENSION>& page_dimension_,
                                   pdf_resource<PAGE_CELLS>&     page_cells_,
                                   pdf_resource<PAGE_SHAPES>&     page_shapes_,
                                   pdf_resource<PAGE_IMAGES>&    page_images_,

                                   std::shared_ptr<pdf_resource<PAGE_FONTS>>     page_fonts_,
                                   std::shared_ptr<pdf_resource<PAGE_GRPHS>>     page_grphs_,

                                   std::shared_ptr<pdf_resource<PAGE_XOBJECTS>>  page_xobjects_,

                                   pdf_timings& timings):
    config(config_),

    page_dimension(page_dimension_),
    page_cells(page_cells_),
    page_shapes(page_shapes_),
    page_images(page_images_),

    page_fonts(page_fonts_),
    page_grphs(page_grphs_),

    page_xobjects(page_xobjects_),

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

  void pdf_decoder<STREAM>::interprete(std::vector<qpdf_instruction>& parameters)
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
                                page_grphs);

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
                                page_grphs);
        state = stack.back();

        stack.push_back(state);

        return true;
      }

    return false;
  }

  void pdf_decoder<STREAM>::interprete(std::vector<qpdf_instruction>& stream_,
                                       std::vector<qpdf_instruction>& parameters_)
  {
    LOG_S(INFO) << __FUNCTION__;

    stream = stream_;

    interprete_stream(parameters_);

    if(parameters_.size()!=0)
      {
        LOG_S(ERROR) << "Finishing a `Do` with nonzero number of parameters!";
      }
  }

  void pdf_decoder<STREAM>::interprete_stream(std::vector<qpdf_instruction>& parameters)
  {
    LOG_S(INFO) << __FUNCTION__;

    //assert(page_fonts.keys()==current_global_state().page_fonts.keys());

    for(int l=0; l<stream.size(); l++)
      {
        qpdf_instruction& inst = stream[l];

        if(inst.key=="operator")
          {
            // pdf_operator::operator_name  name = pdf_operator::to_name(inst.val);
            // pdf_operator::operator_class clss = pdf_operator::to_class(name);

            /*
              if(//clss==pdf_operator::PATH_CONSTRUCTION      or
              //clss==pdf_operator::PATH_PAINTING          or
              clss==pdf_operator::GENERAL_GRAPHICS_STATE or
              clss==pdf_operator::COLOR_SCHEME            )
              {
              parameters.clear();
              continue;
              }
            */

            /*
              for(auto p:parameters)
              {
              LOG_S(INFO) << "\t" << std::setw(12) << p.key << " | " << p.val;
              }
              LOG_S(INFO) << " --> " << std::setw(12) << inst.key << " | " << inst.val;
            */

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
                                page_grphs);
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

    pdf_resource<PAGE_XOBJECT_IMAGE>& xobj = page_xobjects->get_image(xobj_name);
    current_bitmap_state().Do_image(xobj);
  }

  void pdf_decoder<STREAM>::do_form(const std::string& xobj_name,
                                    const xobject_subtype_name& xobj_subtype)
  {
    LOG_S(INFO) << "Do_Form: XObject with name `" << xobj_name << "`";

    pdf_resource<PAGE_XOBJECT_FORM>& xobj = page_xobjects->get_form(xobj_name);

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
    auto page_fonts_    = std::make_shared<pdf_resource<PAGE_FONTS>>(page_fonts);
    auto page_grphs_    = std::make_shared<pdf_resource<PAGE_GRPHS>>(page_grphs);
    auto page_xobjects_ = std::make_shared<pdf_resource<PAGE_XOBJECTS>>(page_xobjects);

    // parse the resources of the xobject into the child resources
    {
      if(xobj.has_fonts())
        {
          std::pair<nlohmann::json, QPDFObjectHandle> xobj_fonts = xobj.get_fonts();
          page_fonts_->set(xobj_fonts.first, xobj_fonts.second, timings);
        }

      if(xobj.has_grphs())
        {
          std::pair<nlohmann::json, QPDFObjectHandle> xobj_grphs = xobj.get_grphs();
          page_grphs_->set(xobj_grphs.first, xobj_grphs.second, timings);
        }

      if(xobj.has_xobjects())
        {
          std::pair<nlohmann::json, QPDFObjectHandle> xobj_xobjects = xobj.get_xobjects();
          page_xobjects_->set(xobj_xobjects.first, xobj_xobjects.second, timings);
        }
    }

    {
      // push-back the stack
      this->q();

      // transform coordinate system
      current_global_state().cm(xobj.get_matrix());

      {
        std::vector<qpdf_instruction> insts = xobj.parse_stream();

        pdf_decoder<STREAM> new_stream(config,

                                       page_dimension,
                                       page_cells,
                                       page_shapes,
                                       page_images,

                                       page_fonts_,
                                       page_grphs_,
                                       page_xobjects_,

                                       timings);
	
        bool updated_stack = new_stream.update_stack(stack, stack_count);

        // copy the stack
        std::vector<qpdf_instruction> parameters;
        new_stream.interprete(insts, parameters);

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

    LOG_S(INFO) << "ending the execution of FORM XObject with name `" << xobj_name << "`";

  }

  void pdf_decoder<STREAM>::do_postscript(const std::string& xobj_name,
                                          const xobject_subtype_name& xobj_subtype)
  {
    LOG_S(WARNING) << "unsupported xobject subtype (PostScript) with name " << xobj_name;
  }

  void pdf_decoder<STREAM>::execute_operator(qpdf_instruction              op,
                                             std::vector<qpdf_instruction> parameters)
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
         ***  text-objects
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

          std::vector<qpdf_instruction> TStar_params = {};
          current_text_state().TStar(TStar_params);

          std::vector<qpdf_instruction> Tj_params = {parameters[0]};
          current_text_state().Tj(Tj_params, stack_count);
        }
        break;

      case pdf_operator::double_accent:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          assert(parameters.size()==3);

          std::vector<qpdf_instruction> Tw_params = {parameters[0]};
          current_text_state().Tw(Tw_params);

          std::vector<qpdf_instruction> Tc_params = {parameters[1]};
          current_text_state().Tc(Tc_params);

          std::vector<qpdf_instruction> TStar_params = {};
          current_text_state().TStar(TStar_params);

          std::vector<qpdf_instruction> Tj_params = {parameters[2]};
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
