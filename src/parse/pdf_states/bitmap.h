//-*-C++-*-

#ifndef PDF_BITMAP_STATE_H
#define PDF_BITMAP_STATE_H

namespace pdflib
{

  template<>
  class pdf_state<BITMAP>
  {
  public:

    pdf_state(const decode_config& config_,
              const pdf_state<GRPH>& grph_state_,
              std::array<double, 9>&    trafo_matrix_,
              page_item<PAGE_IMAGES>& page_images_,
              pdf_render_instructions&  instructions_);

    pdf_state(const pdf_state<BITMAP>& other);

    ~pdf_state();

    pdf_state<BITMAP>& operator=(const pdf_state<BITMAP>& other);

    void Do_image(pdf_resource<PAGE_XOBJECT_IMAGE>& xobj);

  private:

    void add_bitmap_instruction(const page_item<PAGE_IMAGE>& image);

    const decode_config& config;
    const pdf_state<GRPH>& grph_state;

    std::array<double, 9>& trafo_matrix;

    page_item<PAGE_IMAGES>& page_images;

    pdf_render_instructions&  instructions;
  };

  pdf_state<BITMAP>::pdf_state(const decode_config& config_,
                               const pdf_state<GRPH>& grph_state_,
                               std::array<double, 9>& trafo_matrix_,
                               page_item<PAGE_IMAGES>& page_images_,
                               pdf_render_instructions& instructions_):
    config(config_),
    grph_state(grph_state_),
    trafo_matrix(trafo_matrix_),
    page_images(page_images_),
    instructions(instructions_)
  {}

  pdf_state<BITMAP>::pdf_state(const pdf_state<BITMAP>& other):
    config(other.config),
    grph_state(other.grph_state),
    trafo_matrix(other.trafo_matrix),
    page_images(other.page_images),
    instructions(other.instructions)
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

    // --- Compute quad corners and bounding box via the CTM ---
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
          d_0[j] += u_0[i]*ctm[i*3+j];
          d_1[j] += u_1[i]*ctm[i*3+j];
          d_2[j] += u_2[i]*ctm[i*3+j];
          d_3[j] += u_3[i]*ctm[i*3+j];
        }
      }

      std::array<double, 4> img_bbox;
      img_bbox[0] = std::min(std::min(d_0[0], d_1[0]), std::min(d_2[0], d_3[0]));
      img_bbox[2] = std::max(std::max(d_0[0], d_1[0]), std::max(d_2[0], d_3[0]));
      img_bbox[1] = std::min(std::min(d_0[1], d_1[1]), std::min(d_2[1], d_3[1]));
      img_bbox[3] = std::max(std::max(d_0[1], d_1[1]), std::max(d_2[1], d_3[1]));

      image.x0 = img_bbox[0];
      image.y0 = img_bbox[1];
      image.x1 = img_bbox[2];
      image.y1 = img_bbox[3];

      image.r_x0 = d_0[0]; image.r_y0 = d_0[1];
      image.r_x1 = d_1[0]; image.r_y1 = d_1[1];
      image.r_x2 = d_2[0]; image.r_y2 = d_2[1];
      image.r_x3 = d_3[0]; image.r_y3 = d_3[1];
    }

    // --- Populate image properties from the XObject ---
    {
      image.xobject_key        = xobj.get_key();
      image.image_width        = xobj.get_image_width();
      image.image_height       = xobj.get_image_height();
      image.bits_per_component = xobj.get_bits_per_component();
      image.color_space        = xobj.get_color_space();
      image.intent             = xobj.get_intent();
      image.filters            = xobj.get_filters();
      image.raw_stream_data    = xobj.get_raw_stream_data();
      image.decoded_stream_data = xobj.get_decoded_stream_data();

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

    add_bitmap_instruction(image);
  }

  void pdf_state<BITMAP>::add_bitmap_instruction(const page_item<PAGE_IMAGE>& image)
  {
    std::shared_ptr<std::vector<uint8_t>> pixel_data;
    std::array<int, 3> pixel_shape = {0, 0, 0};
    pixel_format fmt = PIXEL_FORMAT_UNKNOWN;

    int channels = 0;
    if      (image.color_space == "/DeviceGray") { fmt = PIXEL_FORMAT_GRAY; channels = 1; }
    else if (image.color_space == "/DeviceRGB")  { fmt = PIXEL_FORMAT_RGB;  channels = 3; }
    else if (image.color_space == "/DeviceCMYK") { fmt = PIXEL_FORMAT_CMYK; channels = 4; }
    else
      {
        LOG_S(WARNING) << "bitmap: unsupported color space '" << image.color_space
                       << "' for xobject_key=" << image.xobject_key;
      }

    if (channels > 0)
      {
        // Pick the best source of raw pixel bytes.
        // decoded_stream_data: QPDF has already decompressed/decoded the stream
        //   (handles /FlateDecode, /DCTDecode, /JPXDecode, …) — preferred.
        // raw_stream_data fallback: only valid when there is no compression filter,
        //   meaning the raw stream IS already the raw pixel bytes.
        std::shared_ptr<Buffer> src;
        if (image.decoded_stream_data and image.decoded_stream_data->getSize() > 0)
          {
            src = image.decoded_stream_data;
          }
        else if (image.filters.empty() and image.raw_stream_data and image.raw_stream_data->getSize() > 0)
          {
            LOG_S(WARNING) << "bitmap: decoded_stream_data unavailable, "
                           << "falling back to raw_stream_data (no filter) "
                           << "for xobject_key=" << image.xobject_key;
            src = image.raw_stream_data;
          }
        else
          {
            LOG_S(WARNING) << "bitmap: no usable pixel data "
                           << "for xobject_key=" << image.xobject_key;
          }

        if (src)
          {
            const int w           = image.image_width;
            const int h           = image.image_height;
            const size_t expected = static_cast<size_t>(w) * h * channels;

            if (src->getSize() >= expected)
              {
                const auto* raw = reinterpret_cast<const uint8_t*>(src->getBuffer());
                pixel_data  = std::make_shared<std::vector<uint8_t>>(raw, raw + expected);
                pixel_shape = {h, w, channels};
              }
            else
              {
                LOG_S(WARNING) << "bitmap: pixel buffer too small ("
                               << src->getSize() << " < " << expected
                               << ") for xobject_key=" << image.xobject_key;
              }
          }
      }

    bitmap_instruction binstr(image.xobject_key,
                              std::move(pixel_data),
                              pixel_shape,
                              fmt,
                              image.r_x0, image.r_y0,
                              image.r_x1, image.r_y1,
                              image.r_x2, image.r_y2,
                              image.r_x3, image.r_y3);
    instructions.add_bitmap_instruction(std::move(binstr));
  }

}

#endif
