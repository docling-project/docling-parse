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
                std::shared_ptr<pdf_resource<PAGE_SHADINGS>>    page_shadings_,
                std::shared_ptr<pdf_resource<PAGE_PATTERNS>>   page_patterns_,
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

    // Establishes the matrix this stream is interpreted under, so that a
    // stream living in its own space -- an annotation appearance -- emits
    // instructions already in page coordinates. Must be called before
    // interprete().
    void set_base_matrix(const std::array<double, 6>& matrix);

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

    // Records the CTM this content stream started in, which is what the
    // pattern /Matrix maps into (8.7.3.1).
    void capture_base_ctm();
    void Q();

    void execute_operator(qpdf_stream_instruction op,
                          std::vector<qpdf_stream_instruction>& parameters);

    void do_image(const std::string& xobj_name,
		  const xobject_subtype_name& xobj_subtype);

    void begin_inline_image();
    void read_inline_image_header(std::vector<qpdf_stream_instruction>& parameters);
    void read_inline_image_data(const qpdf_stream_instruction& instruction);

    // Pushes the initial graphics state when nothing has yet.
    void ensure_stack();

    // Resolves a /CS name that is not a device space against the page's
    // /ColorSpace resources (ISO 32000-1, 8.9.7, Table 93).
    void resolve_inline_color_space(const std::string& name);
    void end_inline_image();

    void do_form(const std::string& xobj_name,
		 const xobject_subtype_name& xobj_subtype);

    void do_postscript(const std::string& xobj_name,
       const xobject_subtype_name& xobj_subtype);

    // `sh`: resolve the named /Shading resource and emit the paint
    // instruction that covers the current clip region.
    void do_shading(const std::string& sh_name);

    // `scn` naming a tiling pattern: replay the pattern cell across the page
    // box, clipped to the path being filled. `even_odd` is that path's fill
    // rule. Returns false when the pattern cannot be resolved or painted.
    bool do_pattern_fill(const std::string& pattern_name, bool even_odd);
    bool do_coons_patch_pattern_fill(const std::string& pattern_name,
                                     pdf_resource<PAGE_PATTERN>& pattern,
                                     bool even_odd);

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

    struct inline_image_entry
    {
      bool active = false;
      bool has_header = false;
      bool has_data = false;
      int width = 0;
      int height = 0;
      int bits_per_component = 1;
      std::string color_space = "";
      std::vector<std::string> filters;
      bool decode_present = false;
      std::vector<double> decode_array;
      bool image_mask = false;
      ccitt::decode_parameters ccitt_params;
      std::string data;

      // Set when /CS named an /Indexed resource: the palette flattened to RGB,
      // so the decoder can treat it as a /DeviceRGB lookup.
      std::shared_ptr<std::vector<uint8_t>> indexed_rgb_palette;
      int indexed_hival = -1;
    };

  private:

    const decode_config& config;

    page_item<PAGE_DIMENSION>& page_dimension;
    page_item<PAGE_CELLS>&     page_cells;
    page_item<PAGE_SHAPES>&     page_shapes;
    page_item<PAGE_IMAGES>&    page_images;

    std::shared_ptr<pdf_resource<PAGE_FONTS>>       page_fonts;
    std::shared_ptr<pdf_resource<PAGE_GRPHS>>       page_grphs;
    std::shared_ptr<pdf_resource<PAGE_COLORSPACES>> page_colorspaces;
    std::shared_ptr<pdf_resource<PAGE_SHADINGS>>    page_shadings;
    std::shared_ptr<pdf_resource<PAGE_PATTERNS>>   page_patterns;

    // CTM in force when this stream began. Pattern space is defined against
    // the default space of the page, not against the CTM at the fill, so a
    // pattern needs this rather than the current matrix.
    std::array<double, 9> base_ctm_ = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool base_ctm_valid_ = false;

    std::shared_ptr<pdf_resource<PAGE_XOBJECTS>>    page_xobjects;

    pdf_render_instructions& instructions;

    pdf_timings& timings;

    std::unordered_set<std::string> unknown_operators;

    std::vector<qpdf_stream_instruction> stream;
    std::vector<pdf_state<GLOBAL> > stack;

    int stack_count;
    int inline_image_count = 0;
    inline_image_entry inline_image;

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
                                   std::shared_ptr<pdf_resource<PAGE_SHADINGS>>    page_shadings_,
                                   std::shared_ptr<pdf_resource<PAGE_PATTERNS>>   page_patterns_,

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
    page_shadings(page_shadings_),
    page_patterns(page_patterns_),

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

  void pdf_decoder<STREAM>::ensure_stack()
  {
    if(stack.size()!=0) { return; }

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

  void pdf_decoder<STREAM>::set_base_matrix(const std::array<double, 6>& matrix)
  {
    ensure_stack();

    current_global_state().cm(matrix);

    // Pattern space is anchored to the space this stream starts in, which is
    // now the one the matrix establishes.
    base_ctm_ = current_global_state().trafo_matrix;
    base_ctm_valid_ = true;
  }

  void pdf_decoder<STREAM>::interprete(std::vector<qpdf_stream_instruction>& parameters)
  {
    LOG_S(INFO) << __FUNCTION__;

    ensure_stack();

    interprete_stream(parameters);
  }

  // A form XObject establishes its own resource scope (PDF 32000-1, 8.10.2):
  // `Tf`, `gs`, `cs`/`CS` and `Do` inside it resolve against the form's
  // /Resources first and only then against the inherited ones. The child
  // resources are parent-linked, so a form that merely inherits resolves
  // identically to its caller.
  //
  // The inherited stack still points at the *caller's* resource objects, so a
  // state built on this decoder's resources is pushed and then value-assigned
  // from the caller's top state: operator= of pdf_state<GLOBAL/GRPH/TEXT/
  // SHAPE/BITMAP> copies parameters only and never the resource pointers,
  // which is what makes this rebase work.
  //
  // The rebase is unconditional. Keying it on the /Font resources (as was done
  // before) missed forms that only carry an /ExtGState or a /ColorSpace: their
  // `gs` and `cs` lookups then went to the page-level maps and failed.
  bool pdf_decoder<STREAM>::update_stack(std::vector<pdf_state<GLOBAL> >& stack_,
                                         int                              stack_count_)
  {
    stack       = stack_;
    stack_count = stack_count_;

    if(stack.size()==0)
      {
        return false;
      }

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

    // Pattern space is anchored to the space in effect at the START of this
    // content stream, not to whatever the CTM happens to be when a pattern is
    // painted (ISO 32000-1, 8.7.3.1): for a page that is the default user
    // space, for a form XObject the space the form was invoked in. Every entry
    // point lands here, so this is where it gets recorded -- capturing it in
    // one of the interprete() overloads only left the other one, the one pages
    // go through, with nothing to anchor to.
    capture_base_ctm();

    for(int l=0; l<stream.size(); l++)
      {
        qpdf_stream_instruction& inst = stream[l];

        if(inst.obj.isInlineImage())
          {
            read_inline_image_data(inst);
          }
        else if(inst.key=="operator")
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

  void pdf_decoder<STREAM>::capture_base_ctm()
  {
    // Only the first content stream handed to this decoder establishes the
    // space; a page split over several streams is one stream as far as the
    // graphics state is concerned (7.8.2).
    if(base_ctm_valid_ or stack.size() == 0)
      {
        return;
      }

    base_ctm_ = current_graphic_state().get_trafo_matrix();
    base_ctm_valid_ = true;

    LOG_S(INFO) << "base CTM for pattern space: ["
                << base_ctm_[0] << ", " << base_ctm_[1] << ", "
                << base_ctm_[3] << ", " << base_ctm_[4] << ", "
                << base_ctm_[6] << ", " << base_ctm_[7] << "]";
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

  void pdf_decoder<STREAM>::begin_inline_image()
  {
    inline_image = inline_image_entry();
    inline_image.active = true;
  }

  void pdf_decoder<STREAM>::read_inline_image_header(
      std::vector<qpdf_stream_instruction>& parameters)
  {
    if(not inline_image.active)
      {
        LOG_S(WARNING) << "ID operator without active inline image";
        return;
      }

    auto canonical_name = [](const std::string& name) -> std::string
      {
        if(name == "/W") { return "/Width"; }
        if(name == "/H") { return "/Height"; }
        if(name == "/BPC") { return "/BitsPerComponent"; }
        if(name == "/CS") { return "/ColorSpace"; }
        if(name == "/F") { return "/Filter"; }
        if(name == "/D") { return "/Decode"; }
        if(name == "/DP") { return "/DecodeParms"; }
        if(name == "/IM") { return "/ImageMask"; }
        if(name == "/I") { return "/Intent"; }
        return name;
      };

    auto canonical_color_space = [](const std::string& name) -> std::string
      {
        if(name == "/G") { return "/DeviceGray"; }
        if(name == "/RGB") { return "/DeviceRGB"; }
        if(name == "/CMYK") { return "/DeviceCMYK"; }
        if(name == "/I") { return "/Indexed"; }
        return name;
      };

    auto canonical_filter = [](const std::string& name) -> std::string
      {
        if(name == "/AHx") { return "/ASCIIHexDecode"; }
        if(name == "/A85") { return "/ASCII85Decode"; }
        if(name == "/LZW") { return "/LZWDecode"; }
        if(name == "/Fl") { return "/FlateDecode"; }
        if(name == "/RL") { return "/RunLengthDecode"; }
        if(name == "/CCF") { return "/CCITTFaxDecode"; }
        if(name == "/DCT") { return "/DCTDecode"; }
        return name;
      };

    for(size_t i = 0; i + 1 < parameters.size(); i += 2)
      {
        qpdf_stream_instruction key_instruction = parameters[i];
        qpdf_stream_instruction value_instruction = parameters[i + 1];

        if(not key_instruction.obj.isName())
          {
            LOG_S(WARNING) << "inline image dictionary key is not a name: "
                           << key_instruction.val;
            continue;
          }

        const std::string key = canonical_name(key_instruction.obj.getName());
        QPDFObjectHandle value = value_instruction.obj;

        if(key == "/Width" and value.isInteger())
          {
            inline_image.width = value.getIntValue();
          }
        else if(key == "/Height" and value.isInteger())
          {
            inline_image.height = value.getIntValue();
          }
        else if(key == "/BitsPerComponent" and value.isInteger())
          {
            inline_image.bits_per_component = value.getIntValue();
          }
        else if(key == "/ColorSpace" and value.isName())
          {
            inline_image.color_space = canonical_color_space(value.getName());
            resolve_inline_color_space(value.getName());
          }
        else if(key == "/Filter")
          {
            inline_image.filters.clear();
            if(value.isName())
              {
                inline_image.filters.push_back(canonical_filter(value.getName()));
              }
            else if(value.isArray())
              {
                for(int j = 0; j < value.getArrayNItems(); ++j)
                  {
                    QPDFObjectHandle item = value.getArrayItem(j);
                    if(item.isName())
                      {
                        inline_image.filters.push_back(canonical_filter(item.getName()));
                      }
                  }
              }
          }
        else if(key == "/Decode" and value.isArray())
          {
            inline_image.decode_array.clear();
            for(int j = 0; j < value.getArrayNItems(); ++j)
              {
                QPDFObjectHandle item = value.getArrayItem(j);
                if(item.isNumber())
                  {
                    inline_image.decode_array.push_back(
                      utils::numeric::locale_safe_numeric_value(item));
                  }
              }
            inline_image.decode_present = not inline_image.decode_array.empty();
          }
        else if(key == "/ImageMask" and value.isBool())
          {
            inline_image.image_mask = value.getBoolValue();
          }
        else if(key == "/DecodeParms")
          {
            // Only /CCITTFaxDecode carries parameters we act on here. Written
            // as an array they line up with the filters; the first dictionary
            // is the one that can hold them, since every filter that may
            // precede the codec takes none.
            QPDFObjectHandle parms = value;
            if(parms.isArray())
              {
                for(int j = 0; j < parms.getArrayNItems(); ++j)
                  {
                    if(parms.getArrayItem(j).isDictionary())
                      {
                        parms = parms.getArrayItem(j);
                        break;
                      }
                  }
              }

            if(parms.isDictionary())
              {
                if(parms.hasKey("/K") and parms.getKey("/K").isInteger())
                  {
                    inline_image.ccitt_params.k =
                      static_cast<int>(parms.getKey("/K").getIntValue());
                  }
                if(parms.hasKey("/Columns") and parms.getKey("/Columns").isInteger())
                  {
                    inline_image.ccitt_params.columns =
                      static_cast<int>(parms.getKey("/Columns").getIntValue());
                  }
                if(parms.hasKey("/BlackIs1") and parms.getKey("/BlackIs1").isBool())
                  {
                    inline_image.ccitt_params.black_is_1 =
                      parms.getKey("/BlackIs1").getBoolValue();
                  }
                if(parms.hasKey("/EncodedByteAlign")
                   and parms.getKey("/EncodedByteAlign").isBool())
                  {
                    inline_image.ccitt_params.encoded_byte_align =
                      parms.getKey("/EncodedByteAlign").getBoolValue();
                  }
              }
          }
      }

    inline_image.has_header = true;
  }

  void pdf_decoder<STREAM>::read_inline_image_data(
      const qpdf_stream_instruction& instruction)
  {
    if(not inline_image.active)
      {
        LOG_S(WARNING) << "inline image data without active BI/ID";
        return;
      }

    QPDFObjectHandle obj = instruction.obj;
    inline_image.data = obj.getInlineImageValue();
    inline_image.has_data = true;
  }

  void pdf_decoder<STREAM>::resolve_inline_color_space(const std::string& name)
  {
    // An inline image may name a colour space from the page's /ColorSpace
    // resources instead of spelling out a device space (ISO 32000-1, 8.9.7,
    // Table 93). Leaving the bare name in place made the decoder report an
    // unsupported colour space and drop the image: a figure drawn as hundreds
    // of small /Indexed inline images came out as the flat background it was
    // painted on.
    if(name == "/G" or name == "/RGB" or name == "/CMYK" or name == "/I" or
       name == "/DeviceGray" or name == "/DeviceRGB" or name == "/DeviceCMYK")
      {
        return;
      }

    if(not page_colorspaces or page_colorspaces->count(name) != 1)
      {
        LOG_S(WARNING) << "inline image names colour space " << name
                       << ", which is not in the /ColorSpace resources";
        return;
      }

    auto& colorspace = (*page_colorspaces)[name];

    std::vector<uint8_t> palette;
    if(colorspace.build_indexed_rgb_palette(palette))
      {
        // The palette arrives already converted, whatever the base space was,
        // so the image is an /Indexed image over /DeviceRGB from here on.
        inline_image.color_space = "/Indexed";
        inline_image.indexed_hival =
          static_cast<int>(palette.size() / 3) - 1;
        inline_image.indexed_rgb_palette =
          std::make_shared<std::vector<uint8_t>>(std::move(palette));

        LOG_S(INFO) << "inline image colour space " << name
                    << ": /Indexed with " << (inline_image.indexed_hival + 1)
                    << " entries";
        return;
      }

    // Not indexed: the component count is enough to pick a device space, which
    // is what every non-indexed family reduces to for sample decoding.
    switch(colorspace.get_num_components())
      {
      case 1: { inline_image.color_space = "/DeviceGray"; break; }
      case 3: { inline_image.color_space = "/DeviceRGB";  break; }
      case 4: { inline_image.color_space = "/DeviceCMYK"; break; }
      default:
        {
          LOG_S(WARNING) << "inline image colour space " << name
                         << " has " << colorspace.get_num_components()
                         << " components; leaving it unresolved";
          return;
        }
      }

    LOG_S(INFO) << "inline image colour space " << name << " -> "
                << inline_image.color_space;
  }

  void pdf_decoder<STREAM>::end_inline_image()
  {
    if(not inline_image.active)
      {
        LOG_S(WARNING) << "EI operator without active inline image";
        return;
      }
    if(not inline_image.has_header or not inline_image.has_data)
      {
        LOG_S(WARNING) << "incomplete inline image: header="
                       << (inline_image.has_header ? "true" : "false")
                       << " data=" << (inline_image.has_data ? "true" : "false");
        inline_image = inline_image_entry();
        return;
      }

    inline_image_count += 1;
    std::string xobject_key = "__inline_image_" + std::to_string(inline_image_count);
    std::shared_ptr<Buffer> stream_data =
      std::make_shared<Buffer>(std::move(inline_image.data));

    current_bitmap_state().Do_inline_image(
      xobject_key,
      inline_image.width,
      inline_image.height,
      inline_image.bits_per_component,
      inline_image.color_space,
      inline_image.filters,
      inline_image.decode_present,
      inline_image.decode_array,
      inline_image.image_mask,
      inline_image.ccitt_params,
      stream_data,
      inline_image.indexed_hival,
      inline_image.indexed_rgb_palette,
      current_shape_state().get_clip_state());

    inline_image = inline_image_entry();
  }

  namespace
  {
    struct coons_mesh_point
    {
      double x = 0.0;
      double y = 0.0;
    };

    struct coons_mesh_patch
    {
      std::array<coons_mesh_point, 12> points;
      std::array<std::array<int, 3>, 4> colors;
    };

    class coons_mesh_bit_reader
    {
    public:
      coons_mesh_bit_reader(const unsigned char* data, std::size_t size):
        data_(data), bit_size_(size * 8), bit_pos_(0) {}

      bool read(std::size_t nbits, std::uint32_t& value)
      {
        if(nbits > 31 or bit_pos_ + nbits > bit_size_) { return false; }

        value = 0;
        for(std::size_t i = 0; i < nbits; i++)
          {
            const std::size_t byte_pos = (bit_pos_ + i) / 8;
            const std::size_t bit_in_byte = 7 - ((bit_pos_ + i) % 8);
            value = (value << 1) |
                    ((data_[byte_pos] >> bit_in_byte) & 0x01u);
          }
        bit_pos_ += nbits;
        return true;
      }

      std::size_t remaining_bits() const { return bit_size_ - bit_pos_; }

    private:
      const unsigned char* data_;
      std::size_t bit_size_;
      std::size_t bit_pos_;
    };

    double coons_decode_sample(std::uint32_t raw,
                               std::size_t nbits,
                               double d0,
                               double d1)
    {
      const double max_raw =
        static_cast<double>((std::uint64_t{1} << nbits) - 1u);
      if(max_raw <= 0.0) { return d0; }
      return d0 + (static_cast<double>(raw) / max_raw) * (d1 - d0);
    }

    coons_mesh_point coons_lerp(const coons_mesh_point& p0,
                                const coons_mesh_point& p1,
                                double t)
    {
      return {p0.x + t * (p1.x - p0.x),
              p0.y + t * (p1.y - p0.y)};
    }

    coons_mesh_point coons_bezier(const coons_mesh_point& p0,
                                  const coons_mesh_point& p1,
                                  const coons_mesh_point& p2,
                                  const coons_mesh_point& p3,
                                  double t)
    {
      const coons_mesh_point a = coons_lerp(p0, p1, t);
      const coons_mesh_point b = coons_lerp(p1, p2, t);
      const coons_mesh_point c = coons_lerp(p2, p3, t);
      const coons_mesh_point d = coons_lerp(a, b, t);
      const coons_mesh_point e = coons_lerp(b, c, t);
      return coons_lerp(d, e, t);
    }

    coons_mesh_point coons_eval(const coons_mesh_patch& patch,
                                double u,
                                double v)
    {
      const auto& p = patch.points;

      const coons_mesh_point bottom = coons_bezier(p[0], p[1], p[2], p[3], u);
      const coons_mesh_point right  = coons_bezier(p[3], p[4], p[5], p[6], v);
      const coons_mesh_point top    = coons_bezier(p[9], p[8], p[7], p[6], u);
      const coons_mesh_point left   = coons_bezier(p[0], p[11], p[10], p[9], v);

      const double x_bilinear =
        (1.0 - u) * (1.0 - v) * p[0].x +
        u * (1.0 - v) * p[3].x +
        u * v * p[6].x +
        (1.0 - u) * v * p[9].x;
      const double y_bilinear =
        (1.0 - u) * (1.0 - v) * p[0].y +
        u * (1.0 - v) * p[3].y +
        u * v * p[6].y +
        (1.0 - u) * v * p[9].y;

      return {
        (1.0 - v) * bottom.x + v * top.x +
          (1.0 - u) * left.x + u * right.x - x_bilinear,
        (1.0 - v) * bottom.y + v * top.y +
          (1.0 - u) * left.y + u * right.y - y_bilinear
      };
    }

    std::array<int, 3> coons_color(const coons_mesh_patch& patch,
                                   double u,
                                   double v)
    {
      std::array<int, 3> rgb = {0, 0, 0};
      for(std::size_t c = 0; c < 3; c++)
        {
          const double val =
            (1.0 - u) * (1.0 - v) * patch.colors[0][c] +
            u * (1.0 - v) * patch.colors[1][c] +
            u * v * patch.colors[2][c] +
            (1.0 - u) * v * patch.colors[3][c];
          rgb[c] = static_cast<int>(std::round(std::min(255.0,
                                                        std::max(0.0, val))));
        }
      return rgb;
    }

    coons_mesh_point coons_transform(const std::array<double, 9>& matrix,
                                     const coons_mesh_point& p)
    {
      return {matrix[0] * p.x + matrix[3] * p.y + matrix[6],
              matrix[1] * p.x + matrix[4] * p.y + matrix[7]};
    }

    shape_instruction coons_triangle_instruction(
      const coons_mesh_point& a,
      const coons_mesh_point& b,
      const coons_mesh_point& c,
      const std::array<int, 3>& rgb,
      const pdf_state<GRPH>& grph_state,
      const clip_state_instruction& clip_state)
    {
      std::vector<shape_segment_op> ops = {SEGMENT_LINE_TO, SEGMENT_LINE_TO};
      std::vector<double> px = {b.x, c.x};
      std::vector<double> py = {b.y, c.y};
      std::vector<shape_subpath> subpaths;
      subpaths.emplace_back(a.x, a.y, std::move(ops), std::move(px),
                            std::move(py), CLOSED, LINE);

      shape_instruction instr(std::move(subpaths),
                              SHAPE_PAINT_FILL,
                              SHAPE_FILL_NONZERO,
                              0.0,
                              grph_state.get_line_cap(),
                              grph_state.get_line_join(),
                              grph_state.get_miter_limit(),
                              std::vector<double>(),
                              0.0,
                              grph_state.get_rgb_stroking_ops(),
                              rgb,
                              grph_state.get_stroke_alpha(),
                              grph_state.get_fill_alpha(),
                              clip_state);
      instr.set_blend_mode(grph_state.get_blend_mode());
      return instr;
    }
  }

  bool pdf_decoder<STREAM>::do_coons_patch_pattern_fill(
    const std::string& pattern_name,
    pdf_resource<PAGE_PATTERN>& pattern,
    bool even_odd)
  {
    QPDFObjectHandle shading = pattern.get_shading();
    if(not shading.isStream()) { return false; }

    QPDFObjectHandle dict = shading.getDict();
    if(not dict.isDictionary() or
       not dict.hasKey("/ShadingType") or
       not dict.getKey("/ShadingType").isInteger() or
       dict.getKey("/ShadingType").getIntValue() != 6)
      {
        return false;
      }

    if(dict.hasKey("/Function"))
      {
        LOG_S(WARNING) << "pattern " << pattern_name
                       << ": Type 6 shading with /Function is not supported";
        return false;
      }

    if(not dict.hasKey("/ColorSpace"))
      {
        LOG_S(WARNING) << "pattern " << pattern_name
                       << ": Type 6 shading has no /ColorSpace";
        return false;
      }

    pdf_resource<PAGE_COLORSPACE> colorspace;
    colorspace.set(pattern_name + "/ColorSpace", dict.getKey("/ColorSpace"));
    const int ncomps = colorspace.get_num_components();
    if(ncomps <= 0)
      {
        LOG_S(WARNING) << "pattern " << pattern_name
                       << ": Type 6 shading has unsupported /ColorSpace";
        return false;
      }

    if(not dict.hasKey("/BitsPerCoordinate") or
       not dict.hasKey("/BitsPerComponent") or
       not dict.hasKey("/BitsPerFlag") or
       not dict.hasKey("/Decode") or
       not dict.getKey("/BitsPerCoordinate").isInteger() or
       not dict.getKey("/BitsPerComponent").isInteger() or
       not dict.getKey("/BitsPerFlag").isInteger())
      {
        LOG_S(WARNING) << "pattern " << pattern_name
                       << ": Type 6 shading is missing required sampling keys";
        return false;
      }

    const std::size_t bits_per_coord =
      static_cast<std::size_t>(dict.getKey("/BitsPerCoordinate").getIntValue());
    const std::size_t bits_per_component =
      static_cast<std::size_t>(dict.getKey("/BitsPerComponent").getIntValue());
    const std::size_t bits_per_flag =
      static_cast<std::size_t>(dict.getKey("/BitsPerFlag").getIntValue());

    if(bits_per_coord == 0 or bits_per_coord > 31 or
       bits_per_component == 0 or bits_per_component > 31 or
       bits_per_flag == 0 or bits_per_flag > 31)
      {
        LOG_S(WARNING) << "pattern " << pattern_name
                       << ": Type 6 shading has unsupported sample bit widths";
        return false;
      }

    QPDFObjectHandle decode_obj = dict.getKey("/Decode");
    if(not decode_obj.isArray() or
       decode_obj.getArrayNItems() < 4 + 2 * ncomps)
      {
        LOG_S(WARNING) << "pattern " << pattern_name
                       << ": Type 6 shading has invalid /Decode";
        return false;
      }

    std::vector<double> decode;
    for(int i = 0; i < decode_obj.getArrayNItems(); i++)
      {
        decode.push_back(decode_obj.getArrayItem(i).getNumericValue());
      }

    std::shared_ptr<Buffer> buffer;
    try
      {
        buffer = to_shared_ptr(shading.getStreamData(qpdf_dl_all));
      }
    catch(const std::exception& e)
      {
        LOG_S(WARNING) << "pattern " << pattern_name
                       << ": failed to decode Type 6 stream: " << e.what();
        return false;
      }

    if(buffer == nullptr or buffer->getSize() == 0)
      {
        LOG_S(WARNING) << "pattern " << pattern_name
                       << ": Type 6 stream is empty";
        return false;
      }

    coons_mesh_bit_reader reader(buffer->getBuffer(), buffer->getSize());
    std::vector<coons_mesh_patch> patches;

    const std::size_t bits_per_patch =
      bits_per_flag + 24 * bits_per_coord +
      static_cast<std::size_t>(4 * ncomps) * bits_per_component;

    while(reader.remaining_bits() >= bits_per_patch)
      {
        std::uint32_t flag = 0;
        if(not reader.read(bits_per_flag, flag)) { break; }

        if(flag != 0)
          {
            LOG_S(WARNING) << "pattern " << pattern_name
                           << ": Type 6 continuation patch flag " << flag
                           << " is not supported yet";
            return false;
          }

        coons_mesh_patch patch;
        bool patch_ok = true;
        for(std::size_t i = 0; i < patch.points.size(); i++)
          {
            std::uint32_t raw_x = 0;
            std::uint32_t raw_y = 0;
            if(not reader.read(bits_per_coord, raw_x) or
               not reader.read(bits_per_coord, raw_y))
              {
                patch_ok = false;
                break;
              }

            patch.points[i].x =
              coons_decode_sample(raw_x, bits_per_coord, decode[0], decode[1]);
            patch.points[i].y =
              coons_decode_sample(raw_y, bits_per_coord, decode[2], decode[3]);
          }
        if(not patch_ok)
          {
            break;
          }

        for(std::size_t corner = 0; corner < patch.colors.size(); corner++)
          {
            std::vector<double> comps;
            comps.reserve(static_cast<std::size_t>(ncomps));
            for(int c = 0; c < ncomps; c++)
              {
                std::uint32_t raw = 0;
                if(not reader.read(bits_per_component, raw))
                  {
                    patch_ok = false;
                    break;
                  }
                const std::size_t d = static_cast<std::size_t>(4 + 2 * c);
                comps.push_back(coons_decode_sample(raw,
                                                    bits_per_component,
                                                    decode[d],
                                                    decode[d + 1]));
              }
            if(comps.size() != static_cast<std::size_t>(ncomps))
              {
                patch_ok = false;
                break;
              }
            if(not colorspace.map_to_rgb(comps, patch.colors[corner]))
              {
                LOG_S(WARNING) << "pattern " << pattern_name
                               << ": Type 6 corner color could not be mapped";
                return false;
              }
          }
        if(not patch_ok)
          {
            break;
          }

        patches.push_back(std::move(patch));
      }

    if(patches.empty())
      {
        LOG_S(WARNING) << "pattern " << pattern_name
                       << ": no supported Type 6 patches decoded";
        return false;
      }

    this->q();
    {
      std::vector<qpdf_stream_instruction> no_parameters;
      if(even_odd) { current_shape_state().WStar(no_parameters); }
      else         { current_shape_state().W(no_parameters);     }
      current_shape_state().n(no_parameters);
    }

    const clip_state_instruction clip_state =
      current_shape_state().get_clip_state();
    const std::array<double, 9>& matrix = pattern.get_matrix();
    const auto& grph_state = current_graphic_state();
    constexpr int mesh_steps = 48;
    std::size_t triangle_count = 0;

    for(const auto& patch : patches)
      {
        for(int iy = 0; iy < mesh_steps; iy++)
          {
            const double v0 = static_cast<double>(iy) / mesh_steps;
            const double v1 = static_cast<double>(iy + 1) / mesh_steps;
            for(int ix = 0; ix < mesh_steps; ix++)
              {
                const double u0 = static_cast<double>(ix) / mesh_steps;
                const double u1 = static_cast<double>(ix + 1) / mesh_steps;
                const coons_mesh_point p00 =
                  coons_transform(matrix, coons_eval(patch, u0, v0));
                const coons_mesh_point p10 =
                  coons_transform(matrix, coons_eval(patch, u1, v0));
                const coons_mesh_point p11 =
                  coons_transform(matrix, coons_eval(patch, u1, v1));
                const coons_mesh_point p01 =
                  coons_transform(matrix, coons_eval(patch, u0, v1));

                const std::array<int, 3> rgb_a =
                  coons_color(patch, (u0 + u1 + u1) / 3.0,
                              (v0 + v0 + v1) / 3.0);
                const std::array<int, 3> rgb_b =
                  coons_color(patch, (u0 + u1 + u0) / 3.0,
                              (v0 + v1 + v1) / 3.0);

                instructions.add_shape_instruction(
                  coons_triangle_instruction(p00, p10, p11, rgb_a,
                                             grph_state, clip_state));
                instructions.add_shape_instruction(
                  coons_triangle_instruction(p00, p11, p01, rgb_b,
                                             grph_state, clip_state));
                triangle_count += 2;
              }
          }
      }

    this->Q();

    LOG_S(INFO) << "pattern " << pattern_name
                << ": rendered Type 6 Coons mesh as " << triangle_count
                << " filled triangles";
    return true;
  }

  // 8.7.4.5: `sh` paints the named shading over the whole current clip
  // region, taking its colours from the shading's own colour space and
  // function rather than from the current fill colour.
  bool pdf_decoder<STREAM>::do_pattern_fill(const std::string& pattern_name,
                                            bool even_odd)
  {
    if(not page_patterns or page_patterns->count(pattern_name) == 0)
      {
        return false;
      }

    pdf_resource<PAGE_PATTERN>& pattern = (*page_patterns)[pattern_name];
    if(not pattern.is_valid())
      {
        return false;
      }

    if(pattern.get_pattern_type() == 2)
      {
        if(not config.keep_shapes)
          {
            return false;
          }

        if(do_coons_patch_pattern_fill(pattern_name, pattern, even_odd))
          {
            return true;
          }

        pdf_resource<PAGE_SHADING> shading;
        shading.set(pattern_name, pattern.get_shading());
        if(not shading.is_paintable())
          {
            LOG_S(WARNING) << "pattern " << pattern_name
                           << ": shading is not paintable: "
                           << shading.get_reason();
            return false;
          }

        this->q();
        {
          std::vector<qpdf_stream_instruction> no_parameters;
          if(even_odd) { current_shape_state().WStar(no_parameters); }
          else         { current_shape_state().W(no_parameters);     }
          current_shape_state().n(no_parameters);
        }

        const std::array<double, 9>& m = pattern.get_matrix();
        std::array<double, 6> matrix = {m[0], m[1], m[3], m[4], m[6], m[7]};
        clip_state_instruction clip_state = current_shape_state().get_clip_state();

        shading_instruction shinstr(
          pattern_name,
          shading.get_shading_type() == SHADING_AXIAL
            ? SHADING_GEOMETRY_AXIAL
            : SHADING_GEOMETRY_RADIAL,
          shading.get_coords(),
          matrix,
          shading.get_stops(),
          shading.get_extend_start(),
          shading.get_extend_end(),
          current_graphic_state().get_fill_alpha(),
          std::move(clip_state));
        shinstr.set_blend_mode(current_graphic_state().get_blend_mode());
        instructions.add_shading_instruction(std::move(shinstr));

        this->Q();
        return true;
      }

    if(pattern.get_pattern_type() != 1)
      {
        return false;
      }

    std::vector<qpdf_stream_instruction> cell = pattern.parse_stream();
    if(cell.empty())
      {
        return false;
      }

    const std::array<double, 9>& want = pattern.get_matrix();
    const std::array<double, 9>& cur  = current_graphic_state().get_trafo_matrix();

    // The base CTM is recorded when the content stream starts interpreting.
    // Standing in with the current matrix instead -- which is what happened
    // while pages never recorded one -- anchors the lattice to the painting
    // operation rather than to the page, so every cell lands displaced by the
    // fill's own translation and the tiling is out of phase with the artwork
    // it fills.
    if(not base_ctm_valid_)
      {
        LOG_S(WARNING) << "pattern " << pattern_name
                       << ": no base CTM recorded for this content stream;"
                       << " anchoring pattern space to the default user space";
      }

    // inverse of the current (affine) CTM
    const double a = cur[0], b = cur[1], c = cur[3], d = cur[4];
    const double e = cur[6], f = cur[7];
    const double det = a * d - b * c;
    if(std::abs(det) < 1e-12) { return false; }

    const std::array<double, 9> inv = {
       d / det, -b / det, 0.0,
      -c / det,  a / det, 0.0,
      (c * f - d * e) / det, (b * e - a * f) / det, 1.0
    };

    auto mul = [](const std::array<double, 9>& m, const std::array<double, 9>& n)
    {
      std::array<double, 9> r = {0, 0, 0, 0, 0, 0, 0, 0, 1};
      r[0] = m[0]*n[0] + m[1]*n[3];
      r[1] = m[0]*n[1] + m[1]*n[4];
      r[3] = m[3]*n[0] + m[4]*n[3];
      r[4] = m[3]*n[1] + m[4]*n[4];
      r[6] = m[6]*n[0] + m[7]*n[3] + n[6];
      r[7] = m[6]*n[1] + m[7]*n[4] + n[7];
      return r;
    };

    // Pattern space -> device is Matrix x base. cm() post-multiplies by the
    // CURRENT matrix, so hand it Matrix x base x inverse(current), which
    // composes back to exactly that. Dropping the base placed the cell in raw
    // pattern coordinates and put it off the top of the page.
    const std::array<double, 9> to_pattern = mul(mul(want, base_ctm_), inv);

    // How many cells to lay down: cover the filled path in pattern space.
    // Falling back to the page box keeps patterns usable when the path cannot
    // produce a finite bbox, but the path bbox is the normal case and avoids
    // picking a clamped off-page tile range for small figures.
    std::array<double, 4> paint_box = page_dimension.get_crop_bbox();
    current_shape_state().get_current_path_bbox(paint_box);
    double xs = pattern.get_x_step();
    double ys = pattern.get_y_step();
    if(xs <= 0.0 or ys <= 0.0) { xs = ys = 0.0; }

    // Which cells of the lattice actually touch the page? Take the page box
    // back through the pattern transform and read off the index range. The
    // lattice extends in BOTH directions: this pattern's origin sits a full
    // page above the page box, so the cell that covers it is at index -1 and
    // a 0..n loop painted nothing but off-page copies.
    int ix0 = 0, ix1 = 0, iy0 = 0, iy1 = 0;
    if(xs > 0.0 and ys > 0.0)
      {
        const std::array<double, 9> pat = mul(want, base_ctm_);
        const double pa = pat[0], pb = pat[1], pc = pat[3], pd = pat[4];
        const double pe = pat[6], pf = pat[7];
        const double pdet = pa * pd - pb * pc;
        if(std::abs(pdet) > 1e-12)
          {
            auto to_pattern_space = [&](double X, double Y, double& u, double& v)
            {
              const double x = X - pe, y = Y - pf;
              u = ( x * pd - y * pc) / pdet;
              v = (-x * pb + y * pa) / pdet;
            };

            const double cx[4] = {paint_box[0], paint_box[2], paint_box[2], paint_box[0]};
            const double cy[4] = {paint_box[1], paint_box[1], paint_box[3], paint_box[3]};
            double umin = 0, umax = 0, vmin = 0, vmax = 0;
            for(int i = 0; i < 4; i++)
              {
                double u, v;
                to_pattern_space(cx[i], cy[i], u, v);
                if(i == 0) { umin = umax = u; vmin = vmax = v; }
                umin = std::min(umin, u); umax = std::max(umax, u);
                vmin = std::min(vmin, v); vmax = std::max(vmax, v);
              }

            const double bx = pattern.has_bbox() ? pattern.get_bbox()[0] : 0.0;
            const double by = pattern.has_bbox() ? pattern.get_bbox()[1] : 0.0;
            ix0 = static_cast<int>(std::floor((umin - bx) / xs));
            ix1 = static_cast<int>(std::ceil ((umax - bx) / xs));
            iy0 = static_cast<int>(std::floor((vmin - by) / ys));
            iy1 = static_cast<int>(std::ceil ((vmax - by) / ys));
          }
      }

    // Bounded: a degenerate step must not spawn a lattice of thousands.
    constexpr int max_span = 16;
    if(ix1 - ix0 > max_span) { ix1 = ix0 + max_span; }
    if(iy1 - iy0 > max_span) { iy1 = iy0 + max_span; }

    LOG_S(INFO) << "painting tiling pattern `" << pattern_name << "` cells x["
                << ix0 << ", " << ix1 << "] y[" << iy0 << ", " << iy1 << "]";

    auto page_fonts_       = std::make_shared<pdf_resource<PAGE_FONTS>>(page_fonts);
    auto page_grphs_       = std::make_shared<pdf_resource<PAGE_GRPHS>>(page_grphs);
    auto page_colorspaces_ = std::make_shared<pdf_resource<PAGE_COLORSPACES>>(page_colorspaces);
    auto page_shadings_    = std::make_shared<pdf_resource<PAGE_SHADINGS>>(page_shadings);
    auto page_patterns_    = std::make_shared<pdf_resource<PAGE_PATTERNS>>(page_patterns);
    auto page_xobjects_    = std::make_shared<pdf_resource<PAGE_XOBJECTS>>(page_xobjects);

    if(pattern.has_resources())
      {
        QPDFObjectHandle res = pattern.get_resources();
        if(res.hasKey("/Font"))      { QPDFObjectHandle o = res.getKey("/Font");      page_fonts_->set(o, timings); }
        if(res.hasKey("/ExtGState")) { QPDFObjectHandle o = res.getKey("/ExtGState"); page_grphs_->set(o, timings); }
        if(res.hasKey("/ColorSpace")){ QPDFObjectHandle o = res.getKey("/ColorSpace");page_colorspaces_->set(o); }
        if(res.hasKey("/Shading"))   { QPDFObjectHandle o = res.getKey("/Shading");   page_shadings_->set(o); }
        if(res.hasKey("/Pattern"))   { QPDFObjectHandle o = res.getKey("/Pattern");   page_patterns_->set(o); }
        if(res.hasKey("/XObject"))   { QPDFObjectHandle o = res.getKey("/XObject");   page_xobjects_->set(o, timings); }
      }

    // The path being filled bounds the pattern (PDF 32000-1, 8.7.3.1): the
    // cells paint only inside it. Without this the lattice covered the whole
    // page box, so a page whose pattern fill was one small shape came out with
    // a block of cells in the corner that no other renderer draws.
    //
    // The capture has to happen before the cell placement matrix goes on:
    // clips are transformed by the matrix current when they are captured, and
    // after the `cm` below that is pattern space, not the space the path was
    // built in.
    this->q();
    {
      std::vector<qpdf_stream_instruction> no_parameters;
      if(even_odd) { current_shape_state().WStar(no_parameters); }
      else         { current_shape_state().W(no_parameters);     }
      current_shape_state().n(no_parameters);
    }

    bool painted = false;

    for(int iy = iy0; iy <= iy1; iy++)
      {
        for(int ix = ix0; ix <= ix1; ix++)
          {
            std::array<double, 9> step = {1, 0, 0, 0, 1, 0,
                                          ix * xs, iy * ys, 1};
            std::array<double, 9> place = mul(step, to_pattern);

            this->q();
            const std::array<double, 6> place6 = {place[0], place[1],
                                                  place[3], place[4],
                                                  place[6], place[7]};
            current_global_state().cm(place6);

            // The cell replays on a clean path. The state q() pushed still
            // carries the path currently being filled with this pattern, and
            // the cell's own painting operator would paint that path too --
            // the whole fill region came out flooded in the cell's colour,
            // with the tiles only visible outside it. Q() restores it.
            std::vector<qpdf_stream_instruction> no_parameters;
            current_shape_state().n(no_parameters);

            pdf_decoder<STREAM> cell_stream(config,
                                            page_dimension,
                                            page_cells,
                                            page_shapes,
                                            page_images,
                                            page_fonts_,
                                            page_grphs_,
                                            page_colorspaces_,
                                            page_shadings_,
                                            page_patterns_,
                                            page_xobjects_,
                                            instructions,
                                            timings);

            bool updated_stack = cell_stream.update_stack(stack, stack_count);
            cell_stream.current_graphic_state().materialize_pattern_fill_color();

            std::vector<qpdf_stream_instruction> parameters;
            std::vector<qpdf_stream_instruction> insts = cell;
            cell_stream.interprete(insts, parameters);

            if(updated_stack) { cell_stream.Q(); }

            this->Q();
            painted = true;
          }
      }

    this->Q();

    return painted;
  }

  void pdf_decoder<STREAM>::do_shading(const std::string& sh_name)
  {
    // Without shape tracking there is no clip path to bound the shading, and
    // an unbounded `sh` would flood the page.
    if(not config.keep_shapes)
      {
        LOG_S(INFO) << "sh " << sh_name << ": skipped, shapes are not kept";
        return;
      }

    const pdf_resource<PAGE_SHADING>* shading = page_shadings->get(sh_name);

    if(shading == nullptr)
      {
        LOG_S(WARNING) << "sh: could not resolve shading resource " << sh_name
                       << " (known: " << page_shadings->size() << " in this scope)";
        return;
      }

    if(not shading->is_paintable())
      {
        LOG_S(WARNING) << "sh: not painting shading " << sh_name << ": "
                       << shading->get_reason();
        return;
      }

    const std::array<double, 9>& trafo = current_global_state().trafo_matrix;

    // shading space -> page space, in PDF operand order [a b c d e f]
    std::array<double, 6> matrix = {trafo[0], trafo[1],
                                    trafo[3], trafo[4],
                                    trafo[6], trafo[7]};

    clip_state_instruction clip_state = current_shape_state().get_clip_state();

    // A shading /BBox is expressed in shading space and bounds the paint just
    // like a clip path does, so it is transformed and appended to the clip
    // (which the renderer intersects).
    if(shading->has_bbox())
      {
        const std::array<double, 4>& bbox = shading->get_bbox();

        const double xs[4] = {bbox[0], bbox[2], bbox[2], bbox[0]};
        const double ys[4] = {bbox[1], bbox[1], bbox[3], bbox[3]};

        std::vector<double> px, py;
        for(int c = 0; c < 4; c++)
          {
            px.push_back(matrix[0]*xs[c] + matrix[2]*ys[c] + matrix[4]);
            py.push_back(matrix[1]*xs[c] + matrix[3]*ys[c] + matrix[5]);
          }

        std::vector<clip_path_instruction> paths = clip_state.get_paths();
        paths.emplace_back(std::move(px), std::move(py), CLOSED, RECTANGLE);

        // the /BBox always bounds, so an unclipped shading becomes clipped
        clip_rule rule = clip_state.has_clip() ? clip_state.get_rule()
                                               : CLIP_RULE_NONZERO;
        clip_state = clip_state_instruction(rule, std::move(paths));
      }

    LOG_S(INFO) << "sh " << sh_name << ": " << to_string(shading->get_shading_type())
                << ", #-stops: " << shading->get_stops().size()
                << ", #-clip-paths: " << clip_state.get_paths().size()
                << ", alpha: " << current_graphic_state().get_fill_alpha()
                << ", ctm: [" << matrix[0] << ", " << matrix[1] << ", "
                << matrix[2] << ", " << matrix[3] << ", "
                << matrix[4] << ", " << matrix[5] << "]";

    shading_instruction shinstr(sh_name,
                                shading->get_shading_type() == SHADING_AXIAL
                                  ? SHADING_GEOMETRY_AXIAL
                                  : SHADING_GEOMETRY_RADIAL,
                                shading->get_coords(),
                                matrix,
                                shading->get_stops(),
                                shading->get_extend_start(),
                                shading->get_extend_end(),
                                current_graphic_state().get_fill_alpha(),
                                std::move(clip_state));

    shinstr.set_blend_mode(current_graphic_state().get_blend_mode());

    instructions.add_shading_instruction(std::move(shinstr));
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
    auto page_shadings_    = std::make_shared<pdf_resource<PAGE_SHADINGS>>(page_shadings);
    auto page_patterns_    = std::make_shared<pdf_resource<PAGE_PATTERNS>>(page_patterns);
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

      if(xobj.has_shadings())
        {
          QPDFObjectHandle xobj_shadings = xobj.get_shadings();
          page_shadings_->set(xobj_shadings);
        }

      if(xobj.has_patterns())
        {
          QPDFObjectHandle xobj_patterns = xobj.get_patterns();
          page_patterns_->set(xobj_patterns);
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

      // A transparency group is composited as a unit, so what is in force at
      // the `Do` applies to the group's result rather than to each operator
      // inside it (11.6.6).
      if(xobj.has_transparency_group())
        {
          current_graphic_state().enter_transparency_group();
        }

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
                                       page_shadings_,
                                       page_patterns_,
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

          // tripwire: update_stack() rebases the state onto this decoder's
          // resource scope, so the two must agree
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
          begin_inline_image();
        }
        break;

      case pdf_operator::ID:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          read_inline_image_header(parameters);
        }
        break;

      case pdf_operator::EI:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          end_inline_image();
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
          if(current_graphic_state().get_fill_is_unresolved_pattern() and
             not current_graphic_state().get_fill_pattern_name().empty())
            {
              do_pattern_fill(current_graphic_state().get_fill_pattern_name(), false);
            }
          current_shape_state().f(parameters);
        }
        break;

      case pdf_operator::F:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          if(current_graphic_state().get_fill_is_unresolved_pattern() and
             not current_graphic_state().get_fill_pattern_name().empty())
            {
              do_pattern_fill(current_graphic_state().get_fill_pattern_name(), false);
            }
          current_shape_state().F(parameters);
        }
        break;

      case pdf_operator::fStar:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          if(current_graphic_state().get_fill_is_unresolved_pattern() and
             not current_graphic_state().get_fill_pattern_name().empty())
            {
              do_pattern_fill(current_graphic_state().get_fill_pattern_name(), true);
            }
          current_shape_state().fStar(parameters);
        }
        break;

      case pdf_operator::B:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          if(current_graphic_state().get_fill_is_unresolved_pattern() and
             not current_graphic_state().get_fill_pattern_name().empty())
            {
              do_pattern_fill(current_graphic_state().get_fill_pattern_name(), false);
            }
          current_shape_state().B(parameters);
        }
        break;

      case pdf_operator::BStar:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          if(current_graphic_state().get_fill_is_unresolved_pattern() and
             not current_graphic_state().get_fill_pattern_name().empty())
            {
              do_pattern_fill(current_graphic_state().get_fill_pattern_name(), true);
            }
          current_shape_state().BStar(parameters);
        }
        break;

      case pdf_operator::b:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          if(current_graphic_state().get_fill_is_unresolved_pattern() and
             not current_graphic_state().get_fill_pattern_name().empty())
            {
              current_shape_state().h(parameters);
              do_pattern_fill(current_graphic_state().get_fill_pattern_name(), false);
              current_shape_state().B(parameters);
            }
          else
            {
              current_shape_state().b(parameters);
            }
        }
        break;

      case pdf_operator::bStar:
        {
          LOG_S(INFO) << "executing " << to_string(name);
          if(current_graphic_state().get_fill_is_unresolved_pattern() and
             not current_graphic_state().get_fill_pattern_name().empty())
            {
              current_shape_state().h(parameters);
              do_pattern_fill(current_graphic_state().get_fill_pattern_name(), true);
              current_shape_state().BStar(parameters);
            }
          else
            {
              current_shape_state().bStar(parameters);
            }
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

          if(parameters.size() != 1)
            {
              LOG_S(WARNING) << "sh expects exactly one shading name, got "
                             << parameters.size() << " operand(s)";
              break;
            }

          this->do_shading(parameters[0].to_utf8_string());
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
