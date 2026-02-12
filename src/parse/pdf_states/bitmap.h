//-*-C++-*-

#ifndef PDF_BITMAP_STATE_H
#define PDF_BITMAP_STATE_H

namespace pdflib
{

  template<>
  class pdf_state<BITMAP>
  {
  public:

    pdf_state(const decode_page_config& config_,
              const pdf_state<GRPH>& grph_state_,
              std::array<double, 9>&    trafo_matrix_,
              page_item<PAGE_IMAGES>& page_images_);

    pdf_state(const pdf_state<BITMAP>& other);

    ~pdf_state();

    pdf_state<BITMAP>& operator=(const pdf_state<BITMAP>& other);

    void Do_image(pdf_resource<PAGE_XOBJECT_IMAGE>& xobj);

  private:

    const decode_page_config& config;
    const pdf_state<GRPH>& grph_state;

    std::array<double, 9>& trafo_matrix;

    page_item<PAGE_IMAGES>& page_images;
  };

  pdf_state<BITMAP>::pdf_state(const decode_page_config& config_,
                               const pdf_state<GRPH>& grph_state_,
                               std::array<double, 9>&    trafo_matrix_,
                               page_item<PAGE_IMAGES>& page_images_):
    config(config_),
    grph_state(grph_state_),
    trafo_matrix(trafo_matrix_),
    page_images(page_images_)
  {}

  pdf_state<BITMAP>::pdf_state(const pdf_state<BITMAP>& other):
    config(other.config),
    grph_state(other.grph_state),
    trafo_matrix(other.trafo_matrix),
    page_images(other.page_images)
  {}

  pdf_state<BITMAP>::~pdf_state()
  {}

  pdf_state<BITMAP>& pdf_state<BITMAP>::operator=(const pdf_state<BITMAP>& other)
  {
    return *this;
  }

  void pdf_state<BITMAP>::Do_image(pdf_resource<PAGE_XOBJECT_IMAGE>& xobj)
  {
    if(not config.keep_bitmaps) { LOG_S(WARNING) << "skipping " << __FUNCTION__; return; }

    LOG_S(INFO) << "starting to do " << __FUNCTION__;
    
    page_item<PAGE_IMAGE> image;
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

      LOG_S(INFO) << "image with ("
		  << image.x0 << ", " << image.y0 << ") x ("
		  << image.x1 << ", " << image.y1 << "): "
		  << image.raw_stream_data;
      
      // propagate PDF semantics for JPEG correction
      image.decode_present = xobj.has_decode_array();
      image.decode_array   = xobj.get_decode_array();
      image.image_mask     = xobj.is_image_mask();

      // propagate graphics state
      image.has_graphics_state = true;
      image.rgb_stroking_ops   = grph_state.get_rgb_stroking_ops();
      image.rgb_filling_ops    = grph_state.get_rgb_filling_ops();
    }

    page_images.push_back(image);
  }

}

#endif
