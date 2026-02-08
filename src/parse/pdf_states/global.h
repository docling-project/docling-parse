//-*-C++-*-

#ifndef PDF_GLOBAL_STATE_H
#define PDF_GLOBAL_STATE_H

namespace pdflib
{

  template<>
  class pdf_state<GLOBAL>
  {
  public:

    pdf_state(const decode_page_config& config,
              pdf_resource<PAGE_CELLS>& page_cells_,
              pdf_resource<PAGE_SHAPES>& page_shapes_,
              pdf_resource<PAGE_IMAGES>& page_images_,

              pdf_resource<PAGE_FONTS>& page_fonts_,
              pdf_resource<PAGE_GRPHS>& page_grphs_);

    pdf_state(const pdf_state<GLOBAL>& other);

    ~pdf_state();

    pdf_state<GLOBAL>& operator=(const pdf_state<GLOBAL>& other);

    void cm(std::vector<qpdf_instruction>& instructions);
    void cm(std::array<double, 6> matrix);

    // void Do_image(pdf_resource<PAGE_XOBJECT>& xobj);

    // void Do_form(pdf_resource<PAGE_XOBJECT>& xobj);

  private:

    bool verify(std::vector<qpdf_instruction>& instructions,
                std::size_t num_instr, std::string name);

  public:

    const decode_page_config& config;

    pdf_resource<PAGE_CELLS>& page_cells;
    pdf_resource<PAGE_SHAPES>& page_shapes;
    pdf_resource<PAGE_IMAGES>& page_images;

    pdf_resource<PAGE_FONTS>& page_fonts;
    pdf_resource<PAGE_GRPHS>& page_grphs;

    std::array<double, 9> trafo_matrix;

    pdf_state<GRPH> grph_state; // this needs to be first

    pdf_state<TEXT> text_state;
    pdf_state<SHAPE> shape_state;
    pdf_state<BITMAP> bitmap_state;
  };

  pdf_state<GLOBAL>::pdf_state(const decode_page_config& config_,
                               pdf_resource<PAGE_CELLS>& page_cells_,
                               pdf_resource<PAGE_SHAPES>& page_shapes_,
                               pdf_resource<PAGE_IMAGES>& page_images_,

                               pdf_resource<PAGE_FONTS>& page_fonts_,
                               pdf_resource<PAGE_GRPHS>& page_grphs_):
    config(config_),

    page_cells(page_cells_),
    page_shapes(page_shapes_),
    page_images(page_images_),

    page_fonts(page_fonts_),
    page_grphs(page_grphs_),

    trafo_matrix({1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0}),

    grph_state(trafo_matrix, page_grphs),
    text_state(config, grph_state, trafo_matrix, page_cells, page_fonts),
    shape_state(config, grph_state, trafo_matrix, page_shapes),
    bitmap_state(config, grph_state, trafo_matrix, page_images)
  {
    //LOG_S(INFO) << "pdf_state<GLOBAL>";
  }

  pdf_state<GLOBAL>::pdf_state(const pdf_state<GLOBAL>& other):
    config(other.config),

    page_cells(other.page_cells),
    page_shapes(other.page_shapes),
    page_images(other.page_images),

    page_fonts(other.page_fonts),
    page_grphs(other.page_grphs),

    trafo_matrix(other.trafo_matrix),

    grph_state(trafo_matrix, page_grphs),
    text_state(config, grph_state, trafo_matrix, page_cells, page_fonts),
    shape_state(config, grph_state, trafo_matrix, page_shapes),
    bitmap_state(config, grph_state, trafo_matrix, page_images)
  {
    //LOG_S(INFO) << "pdf_state<GLOBAL>(const pdf_state<GLOBAL>& other)";
    shape_state = other.shape_state;
    grph_state = other.grph_state;
    text_state = other.text_state;
    bitmap_state = other.bitmap_state;
  }

  pdf_state<GLOBAL>::~pdf_state()
  {
    //LOG_S(INFO) << "~pdf_state<GLOBAL>";
  }

  pdf_state<GLOBAL>& pdf_state<GLOBAL>::operator=(const pdf_state<GLOBAL>& other)
  {
    this->trafo_matrix = other.trafo_matrix;

    this->grph_state = other.grph_state;
    this->text_state = other.text_state;
    this->shape_state = other.shape_state;
    this->bitmap_state = other.bitmap_state;

    return *this;
  }

  bool pdf_state<GLOBAL>::verify(std::vector<qpdf_instruction>& instructions,
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
                     << __FUNCTION__;
        LOG_S(ERROR) << " => we can continue but might have incorrect results!";

        return true;
      }

    LOG_S(ERROR) << "#-instructions " << instructions.size()
                 << " does not match expected value " << num_instr << " for "
                 << __FUNCTION__;

    return false;
  }

  void pdf_state<GLOBAL>::cm(std::vector<qpdf_instruction>& instructions)
  {
    if(not verify(instructions, 6, __FUNCTION__) ) { return; }

    std::array<double, 6> matrix;

    for(int d=0; d<6; d++)
      {
        matrix[d] = instructions[d].to_double();
      }

    this->cm(matrix);
  }

  // We follow the coordinate transformations as indicated in 8.3.4 [p 120], i.e.
  //
  //                       |a b 0|
  //   [x′ y′ 1] = [x y 1]*|c d 0|
  //                       |e f 1|
  //
  // with a = M[0], b = M[1], c = M[3], d = M[4], e = M[6], f = M[8]
  //
  void pdf_state<GLOBAL>::cm(std::array<double, 6> matrix)
  {
    std::array<double, 9> tmp = trafo_matrix;

    trafo_matrix[0] = matrix[0] * tmp[0] + matrix[1] * tmp[3];
    trafo_matrix[1] = matrix[0] * tmp[1] + matrix[1] * tmp[4];

    trafo_matrix[3] = matrix[2] * tmp[0] + matrix[3] * tmp[3];
    trafo_matrix[4] = matrix[2] * tmp[1] + matrix[3] * tmp[4];

    trafo_matrix[6] = matrix[4] * tmp[0] + matrix[5] * tmp[3] + tmp[6];
    trafo_matrix[7] = matrix[4] * tmp[1] + matrix[5] * tmp[4] + tmp[7];
  }

  /*
  void pdf_state<GLOBAL>::Do_image(pdf_resource<PAGE_XOBJECT>& xobj)
  {
    if(not config.keep_bitmaps) { return; }

    pdf_resource<PAGE_IMAGE> image;
    {
      // FIXME clean up this crap
      std::array<double, 9> ctm = trafo_matrix;

      std::array<double, 3> u_0 = {{0, 0, 1}};
      std::array<double, 3> u_1 = {{0, 1, 1}};
      std::array<double, 3> u_2 = {{1, 1, 1}};
      std::array<double, 3> u_3 = {{1, 0, 1}};

      std::array<double, 3> d_0 = {{0, 0, 0}};
      std::array<double, 3> d_1 = {{0, 0, 0}};
      std::array<double, 3> d_2 = {{0, 0, 0}};
      std::array<double, 3> d_3 = {{0, 0, 0}};

      // p 120
      for(int j=0; j<3; j++){
        for(int i=0; i<3; i++){
          d_0[j] += u_0[i]*ctm[i*3+j] ;
          d_1[j] += u_1[i]*ctm[i*3+j] ;
          d_2[j] += u_2[i]*ctm[i*3+j] ;
          d_3[j] += u_3[i]*ctm[i*3+j] ;
        }
      }

      std::array<double, 4> img_bbox;

      img_bbox[0] = std::min(std::min(d_0[0], d_1[0]),
                             std::min(d_2[0], d_3[0]));
      img_bbox[2] = std::max(std::max(d_0[0], d_1[0]),
                             std::max(d_2[0], d_3[0]));

      img_bbox[1] = std::min(std::min(d_0[1], d_1[1]),
                             std::min(d_2[1], d_3[1]));
      img_bbox[3] = std::max(std::max(d_0[1], d_1[1]),
                             std::max(d_2[1], d_3[1]));

      image.x0 = img_bbox[0];
      image.y0 = img_bbox[1];
      image.x1 = img_bbox[2];
      image.y1 = img_bbox[3];
    }

    // Populate image properties from the XObject
    {
      image.xobject_key       = xobj.get_key();
      image.image_width       = xobj.get_image_width();
      image.image_height      = xobj.get_image_height();
      image.bits_per_component = xobj.get_bits_per_component();
      image.color_space       = xobj.get_color_space();
      image.intent            = xobj.get_intent();
      image.filters              = xobj.get_filters();
      image.raw_stream_data      = xobj.get_raw_stream_data();
      image.decoded_stream_data  = xobj.get_decoded_stream_data();

      // propagate PDF semantics for JPEG correction
      image.decode_present = xobj.has_decode_array();
      image.decode_array   = xobj.get_decode_array();
      image.image_mask     = xobj.is_image_mask();
    }

    page_images.push_back(image);
  }
  */

}

#endif
