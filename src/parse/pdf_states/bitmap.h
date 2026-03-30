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
      image.decode_present  = xobj.has_decode_array();
      image.decode_array    = xobj.get_decode_array();
      image.image_mask      = xobj.is_image_mask();
      image.icc_components  = xobj.get_icc_components();

      // propagate /Indexed color space data
      image.indexed_hival   = xobj.get_indexed_hival();
      image.indexed_base_cs = xobj.get_indexed_base_cs();
      image.indexed_palette = xobj.get_indexed_palette();

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
    if(image.color_space == "/DeviceGray")
      {
        fmt = PIXEL_FORMAT_GRAY; channels = 1;
      }
    else if(image.color_space == "/DeviceRGB")
      {
        fmt = PIXEL_FORMAT_RGB; channels = 3;
      }
    else if(image.color_space == "/DeviceCMYK")
      {
        fmt = PIXEL_FORMAT_CMYK; channels = 4;
      }
    else if(image.color_space.find("/ICCBased") != std::string::npos
            and image.icc_components > 0)
      {
        LOG_S(INFO) << "bitmap: ICCBased color space with N=" << image.icc_components
                    << " for xobject_key=" << image.xobject_key;
        if(image.icc_components == 1)
          {
            LOG_S(INFO) << "bitmap: treating ICCBased N=1 as DeviceGray";
            fmt = PIXEL_FORMAT_GRAY; channels = 1;
          }
        else if(image.icc_components == 3)
          {
            LOG_S(INFO) << "bitmap: treating ICCBased N=3 as DeviceRGB";
            fmt = PIXEL_FORMAT_RGB; channels = 3;
          }
        else if(image.icc_components == 4)
          {
            LOG_S(INFO) << "bitmap: treating ICCBased N=4 as DeviceCMYK";
            fmt = PIXEL_FORMAT_CMYK; channels = 4;
          }
        else
          {
            LOG_S(WARNING) << "bitmap: ICCBased with unsupported N=" << image.icc_components
                           << " for xobject_key=" << image.xobject_key;
          }
      }
    else if(image.indexed_palette and not image.indexed_palette->empty())
      {
        // /Indexed: expand palette indices into base color space pixels.
        // Base color space channels determine the output format.
        int ncomps = 0;
        if(image.indexed_base_cs == "/DeviceGray")
          {
            fmt = PIXEL_FORMAT_GRAY; ncomps = 1;
          }
        else if(image.indexed_base_cs == "/DeviceRGB")
          {
            fmt = PIXEL_FORMAT_RGB; ncomps = 3;
          }
        else if(image.indexed_base_cs == "/DeviceCMYK")
          {
            fmt = PIXEL_FORMAT_CMYK; ncomps = 4;
          }
        else
          {
            LOG_S(WARNING) << "bitmap: Indexed with unsupported base color space '"
                           << image.indexed_base_cs
                           << "' for xobject_key=" << image.xobject_key;
          }

        if(ncomps > 0 and image.decoded_stream_data and image.decoded_stream_data->getSize() > 0)
          {
            const int w = image.image_width;
            const int h = image.image_height;
            const auto& palette = *image.indexed_palette;
            const auto* indices = reinterpret_cast<const uint8_t*>(
              image.decoded_stream_data->getBuffer());
            const size_t n_indices = image.decoded_stream_data->getSize();

            auto expanded = std::make_shared<std::vector<uint8_t>>();
            expanded->reserve(static_cast<size_t>(w) * h * ncomps);

            for(size_t i = 0; i < n_indices; ++i)
              {
                int idx = static_cast<int>(indices[i]);
                // clamp to hival
                if(image.indexed_hival >= 0 and idx > image.indexed_hival)
                  {
                    idx = image.indexed_hival;
                  }
                const size_t palette_offset = static_cast<size_t>(idx) * ncomps;
                if(palette_offset + ncomps <= palette.size())
                  {
                    for(int c = 0; c < ncomps; ++c)
                      {
                        expanded->push_back(palette[palette_offset + c]);
                      }
                  }
                else
                  {
                    // out-of-range index: fill with zeros
                    for(int c = 0; c < ncomps; ++c)
                      {
                        expanded->push_back(0);
                      }
                  }
              }

            pixel_data  = std::move(expanded);
            pixel_shape = {h, w, ncomps};
            channels    = ncomps; // mark as handled
            LOG_S(INFO) << "bitmap: expanded Indexed palette for xobject_key="
                        << image.xobject_key
                        << " (" << n_indices << " indices → "
                        << pixel_data->size() << " bytes, ncomps=" << ncomps << ")";
          }
        else if(ncomps > 0)
          {
            LOG_S(WARNING) << "bitmap: Indexed color space but no decoded_stream_data "
                           << "for xobject_key=" << image.xobject_key;
          }
      }
    else
      {
        LOG_S(WARNING) << "bitmap: unsupported color space '" << image.color_space
                       << "' for xobject_key=" << image.xobject_key;
      }

    if (channels > 0 and not pixel_data)
      {
        // Pick the best source of raw pixel bytes.
        //
        // Priority:
        //   1. decoded_stream_data — QPDF has already fully decoded the stream
        //      (available for /FlateDecode and some /DCTDecode cases).
        //   2. /DCTDecode via libjpeg — when QPDF could not decode the stream
        //      in thread-safe mode, raw_stream_data IS the original JPEG bytes;
        //      we decompress with libjpeg to get raw pixels directly.
        //   3. raw_stream_data with no filter — the stream is already raw pixels.

        const bool has_dct = std::find(image.filters.begin(), image.filters.end(),
                                       "/DCTDecode") != image.filters.end();

        if (image.decoded_stream_data and image.decoded_stream_data->getSize() > 0)
          {
            const int w           = image.image_width;
            const int h           = image.image_height;
            const size_t expected = static_cast<size_t>(w) * h * channels;
            const auto src        = image.decoded_stream_data;

            if (src->getSize() >= expected)
              {
                const auto* raw = reinterpret_cast<const uint8_t*>(src->getBuffer());
                pixel_data  = std::make_shared<std::vector<uint8_t>>(raw, raw + expected);
                pixel_shape = {h, w, channels};
              }
            else
              {
                LOG_S(WARNING) << "bitmap: decoded_stream_data too small ("
                               << src->getSize() << " < " << expected
                               << ") for xobject_key=" << image.xobject_key;
              }
          }
        else if (has_dct and image.raw_stream_data and image.raw_stream_data->getSize() > 0)
          {
            LOG_S(INFO) << "bitmap: decoded_stream_data unavailable for /DCTDecode image, "
                        << "decoding JPEG via libjpeg "
                        << "for xobject_key=" << image.xobject_key;

            jpeg::ColorSpace cs = jpeg::icc_n_to_color_space(channels);

            jpeg::jpeg_parameters params;
            params.color_space = cs;
            params.width       = image.image_width;
            params.height      = image.image_height;
            params.decode      = image.decode_array;
            params.has_decode  = image.decode_present and not image.decode_array.empty();

            auto decoded = jpeg::decode_jpeg_to_raw_pixels(
                reinterpret_cast<unsigned char const*>(image.raw_stream_data->getBuffer()),
                static_cast<std::size_t>(image.raw_stream_data->getSize()),
                params);

            if (not decoded.empty())
              {
                const int w = image.image_width;
                const int h = image.image_height;
                pixel_data  = std::make_shared<std::vector<uint8_t>>(std::move(decoded));
                pixel_shape = {h, w, channels};
              }
            else
              {
                LOG_S(WARNING) << "bitmap: libjpeg decode failed "
                               << "for xobject_key=" << image.xobject_key;
              }
          }
        else if (image.filters.empty() and image.raw_stream_data and image.raw_stream_data->getSize() > 0)
          {
            LOG_S(WARNING) << "bitmap: decoded_stream_data unavailable, "
                           << "falling back to raw_stream_data (no filter) "
                           << "for xobject_key=" << image.xobject_key;

            const int w           = image.image_width;
            const int h           = image.image_height;
            const size_t expected = static_cast<size_t>(w) * h * channels;
            const auto src        = image.raw_stream_data;

            if (src->getSize() >= expected)
              {
                const auto* raw = reinterpret_cast<const uint8_t*>(src->getBuffer());
                pixel_data  = std::make_shared<std::vector<uint8_t>>(raw, raw + expected);
                pixel_shape = {h, w, channels};
              }
            else
              {
                LOG_S(WARNING) << "bitmap: raw_stream_data too small ("
                               << src->getSize() << " < " << expected
                               << ") for xobject_key=" << image.xobject_key;
              }
          }
        else
          {
            LOG_S(WARNING) << "bitmap: no usable pixel data "
                           << "for xobject_key=" << image.xobject_key;
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
