//-*-C++-*-

#ifndef PDF_BITMAP_STATE_H
#define PDF_BITMAP_STATE_H

#include <algorithm>
#include <cmath>

#include <parse/utils/ccitt/ccitt_utils.h>
#include <parse/utils/jpx/jpx_utils.h>
#include <third_party/pdfium_jbig2.h>

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

    void Do_image(pdf_resource<PAGE_XOBJECT_IMAGE>& xobj,
                  clip_state_instruction clip_state = clip_state_instruction());

  private:

    enum visible_bbox_state
    {
      VISIBLE_BBOX_NONE,
      VISIBLE_BBOX_CLIPPED,
      VISIBLE_BBOX_EMPTY,
    };

    void add_bitmap_instruction(const page_item<PAGE_IMAGE>& image,
                                clip_state_instruction clip_state);

    bool get_clip_path_bbox(const clip_path_instruction& clip_path,
                            std::array<double, 4>& bbox) const;

    bool intersect_bbox(std::array<double, 4>& bbox,
                        const std::array<double, 4>& clip_bbox) const;

    visible_bbox_state compute_visible_bbox(
      const page_item<PAGE_IMAGE>& image,
      const clip_state_instruction& clip_state,
      std::array<double, 4>& visible_bbox) const;

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

  bool pdf_state<BITMAP>::get_clip_path_bbox(const clip_path_instruction& clip_path,
                                             std::array<double, 4>& bbox) const
  {
    if(clip_path.empty())
      {
        return false;
      }

    const auto& xs = clip_path.get_x();
    const auto& ys = clip_path.get_y();
    const size_t n = clip_path.size();

    double x_min = xs[0];
    double x_max = xs[0];
    double y_min = ys[0];
    double y_max = ys[0];

    for(size_t i = 1; i < n; i++)
      {
        x_min = std::min(x_min, xs[i]);
        x_max = std::max(x_max, xs[i]);
        y_min = std::min(y_min, ys[i]);
        y_max = std::max(y_max, ys[i]);
      }

    if(x_max - x_min <= config.min_visible_clip_extent or
       y_max - y_min <= config.min_visible_clip_extent)
      {
        return false;
      }

    bbox = {x_min, y_min, x_max, y_max};
    return true;
  }

  bool pdf_state<BITMAP>::intersect_bbox(std::array<double, 4>& bbox,
                                         const std::array<double, 4>& clip_bbox) const
  {
    const double x0 = std::max(bbox[0], clip_bbox[0]);
    const double y0 = std::max(bbox[1], clip_bbox[1]);
    const double x1 = std::min(bbox[2], clip_bbox[2]);
    const double y1 = std::min(bbox[3], clip_bbox[3]);

    if(x1 <= x0 or y1 <= y0)
      {
        return false;
      }

    bbox = {x0, y0, x1, y1};
    return true;
  }

  pdf_state<BITMAP>::visible_bbox_state pdf_state<BITMAP>::compute_visible_bbox(
    const page_item<PAGE_IMAGE>& image,
    const clip_state_instruction& clip_state,
    std::array<double, 4>& visible_bbox) const
  {
    if(not clip_state.has_clip())
      {
        return VISIBLE_BBOX_NONE;
      }

    visible_bbox = {image.x0, image.y0, image.x1, image.y1};

    bool applied_clip = false;
    for(const auto& clip_path : clip_state.get_paths())
      {
        std::array<double, 4> clip_bbox = {0.0, 0.0, 0.0, 0.0};
        if(not get_clip_path_bbox(clip_path, clip_bbox))
          {
            continue;
          }

        std::array<double, 4> candidate = visible_bbox;
        if(not intersect_bbox(candidate, clip_bbox))
          {
            LOG_S(INFO) << "bitmap: empty visible bbox after clip"
                        << " for xobject_key=" << image.xobject_key
                           << " clip=(" << clip_bbox[0] << ", " << clip_bbox[1]
                           << ", " << clip_bbox[2] << ", " << clip_bbox[3] << ")"
                           << " image=(" << image.x0 << ", " << image.y0
                           << ", " << image.x1 << ", " << image.y1 << ")";
            return VISIBLE_BBOX_EMPTY;
          }

        visible_bbox = candidate;
        applied_clip = true;
      }

    return applied_clip ? VISIBLE_BBOX_CLIPPED : VISIBLE_BBOX_NONE;
  }

  void pdf_state<BITMAP>::Do_image(pdf_resource<PAGE_XOBJECT_IMAGE>& xobj,
                                   clip_state_instruction clip_state)
  {
    if(not config.keep_bitmaps) { LOG_S(WARNING) << "skipping " << __FUNCTION__; return; }

    LOG_S(INFO) << "starting to do " << __FUNCTION__ << " for xobject_key=" << xobj.get_key();
    
    page_item<PAGE_IMAGE> image;
    image.xobject_key = xobj.get_key();

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

      std::array<double, 4> visible_bbox = {0.0, 0.0, 0.0, 0.0};
      const visible_bbox_state bbox_state =
        compute_visible_bbox(image, clip_state, visible_bbox);
      if(bbox_state == VISIBLE_BBOX_CLIPPED)
        {
          image.has_visible_bbox = true;
          image.visible_x0 = visible_bbox[0];
          image.visible_y0 = visible_bbox[1];
          image.visible_x1 = visible_bbox[2];
          image.visible_y1 = visible_bbox[3];
        }
      else if(bbox_state == VISIBLE_BBOX_EMPTY)
        {
          image.is_visible = false;
        }
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
      image.soft_mask_data     = xobj.get_soft_mask_data();

      LOG_S(INFO) << "image with ("
		  << image.x0 << ", " << image.y0 << ") x ("
		  << image.x1 << ", " << image.y1 << "): "
		  << image.raw_stream_data
		  << " xobject_key=" << image.xobject_key;

      // propagate PDF semantics for JPEG correction
      image.decode_present  = xobj.has_decode_array();
      image.decode_array    = xobj.get_decode_array();
      image.image_mask      = xobj.is_image_mask();

      // propagate /CCITTFaxDecode parameters
      image.ccitt_k          = xobj.get_ccitt_k();
      image.ccitt_black_is_1 = xobj.get_ccitt_black_is_1();
      image.icc_components  = xobj.get_icc_components();
      image.device_n_components = xobj.get_device_n_components();
      image.device_n_names = xobj.get_device_n_names();
      image.jbig2_globals_data = xobj.get_jbig2_globals_data();

      // propagate /Indexed color space data
      image.indexed_hival   = xobj.get_indexed_hival();
      image.indexed_base_cs = xobj.get_indexed_base_cs();
      image.indexed_palette = xobj.get_indexed_palette();
      image.indexed_base_device_n_names = xobj.get_indexed_base_device_n_names();
      image.indexed_base_device_n_single_black =
        xobj.get_indexed_base_device_n_single_black();

      // propagate graphics state
      image.has_graphics_state = true;
      image.rgb_stroking_ops   = grph_state.get_rgb_stroking_ops();
      image.rgb_filling_ops    = grph_state.get_rgb_filling_ops();
    }

    page_images.push_back(image);

    add_bitmap_instruction(image, std::move(clip_state));
  }

  void pdf_state<BITMAP>::add_bitmap_instruction(const page_item<PAGE_IMAGE>& image,
                                                 clip_state_instruction clip_state)
  {
    std::shared_ptr<std::vector<uint8_t>> pixel_data;
    std::array<int, 3> pixel_shape = {0, 0, 0};
    pixel_format fmt = PIXEL_FORMAT_UNKNOWN;
    cmyk_convention cmyk_conv = CMYK_CONVENTION_UNKNOWN;

    int channels = 0;
    auto has_default_adobe_cmyk_decode = [&](std::vector<double> const& decode_array) -> bool
      {
        static constexpr double expected_decode[8] = {
          1.0, 0.0, 1.0, 0.0,
          1.0, 0.0, 1.0, 0.0
        };
        if(decode_array.size() < 8)
          {
            return false;
          }
        for(int i = 0; i < 8; ++i)
          {
            if(std::abs(decode_array[static_cast<std::size_t>(i)] - expected_decode[i]) > 1e-12)
              {
                return false;
              }
          }
        return true;
      };
    auto apply_decode_to_u8_samples = [&](std::shared_ptr<std::vector<uint8_t>>& dst,
                                          int ncomps) -> void
      {
        if(not dst or ncomps <= 0 or not image.decode_present or image.decode_array.size() < 2)
          {
            return;
          }

        const int pair_count = static_cast<int>(image.decode_array.size() / 2);
        for(size_t i = 0; i < dst->size(); ++i)
          {
            const int comp = static_cast<int>(i % static_cast<size_t>(ncomps));
            if(comp < pair_count)
              {
                (*dst)[i] = jpeg::apply_decode_component(
                  (*dst)[i],
                  image.decode_array[2 * comp + 0],
                  image.decode_array[2 * comp + 1]);
              }
          }
      };

    auto expand_indexed_samples = [&](int ncomps,
                                      const uint8_t* indices,
                                      size_t n_indices,
                                      int w,
                                      int h) -> bool
      {
        if(ncomps <= 0 or not image.indexed_palette or image.indexed_palette->empty() or not indices)
          {
            return false;
          }

        const auto& palette = *image.indexed_palette;
        auto expanded = std::make_shared<std::vector<uint8_t>>();
        expanded->reserve(static_cast<size_t>(w) * h * ncomps);

        for(size_t i = 0; i < n_indices; ++i)
          {
            int idx = static_cast<int>(indices[i]);
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
                for(int c = 0; c < ncomps; ++c)
                  {
                    expanded->push_back(0);
                  }
              }
          }

        pixel_data = std::move(expanded);
        pixel_shape = {h, w, ncomps};
        channels = ncomps;
        if(fmt == PIXEL_FORMAT_CMYK
           and detail::device_n_names_are_process_cmyk_subset(image.indexed_base_device_n_names))
          {
            cmyk_conv = CMYK_CONVENTION_PROCESS;
          }

        if(image.indexed_base_device_n_single_black and ncomps == 1)
          {
            for(auto& sample : *pixel_data)
              {
                sample = static_cast<uint8_t>(255 - sample);
              }
            LOG_S(INFO) << "bitmap: inverted Indexed single-Black DeviceN palette "
                        << "for xobject_key=" << image.xobject_key;
          }
        return true;
      };

    auto unpack_subbyte_samples_to_u8 =
      [&](std::shared_ptr<Buffer> const& src,
          int w,
          int h,
          int ncomps,
          int bits_per_component,
          std::vector<double> const& decode_array) -> bool
      {
        // QPDF's getStreamData() decodes the filter chain, but it does not
        // expand sub-8-bit image samples into one byte per component. For a
        // `/FlateDecode` image with `/BitsPerComponent 1`, the decoded stream
        // therefore still contains packed bits (with producer-dependent row
        // padding), while the renderer expects a dense 8-bit-per-component
        // buffer. This helper performs that expansion and applies the image's
        // `/Decode` mapping while unpacking.
        if(not src or src->getSize() == 0 or w <= 0 or h <= 0 or ncomps <= 0)
          {
            return false;
          }
        if(bits_per_component <= 0 or bits_per_component >= 8)
          {
            return false;
          }

        const std::size_t row_bits =
          static_cast<std::size_t>(w) * static_cast<std::size_t>(ncomps)
          * static_cast<std::size_t>(bits_per_component);
        const std::size_t min_row_bytes = (row_bits + 7u) / 8u;
        if(min_row_bytes == 0)
          {
            return false;
          }

        const std::size_t src_size = src->getSize();
        const std::size_t min_total = min_row_bytes * static_cast<std::size_t>(h);
        if(src_size < min_total)
          {
            LOG_S(WARNING) << "bitmap: packed decoded_stream_data too small ("
                           << src_size << " < " << min_total
                           << ") for sub-byte image xobject_key=" << image.xobject_key
                           << " width=" << w
                           << " height=" << h
                           << " channels=" << ncomps
                           << " bpc=" << bits_per_component;
            return false;
          }

        // Per the PDF spec, rows are exactly min_row_bytes wide. QPDF's
        // getStreamData() may return more bytes than width*height*bpc/8 (e.g.
        // trailing data), but those extra bytes are not per-row padding and must
        // not be used to inflate the row stride.
        const std::size_t row_stride = min_row_bytes;

        const std::uint32_t sample_max = (1u << bits_per_component) - 1u;
        auto decode_sample =
          [&](int component_index, std::uint32_t raw_sample) -> std::uint8_t
          {
            const int pair_count = static_cast<int>(decode_array.size() / 2);
            if(component_index < pair_count)
              {
                const double dmin = decode_array[2 * component_index + 0];
                const double dmax = decode_array[2 * component_index + 1];
                const double norm =
                  static_cast<double>(raw_sample) / static_cast<double>(sample_max);
                const double decoded = dmin + norm * (dmax - dmin);
                const double clamped = std::clamp(decoded, 0.0, 1.0);
                return static_cast<std::uint8_t>(std::lround(clamped * 255.0));
              }

            // Absent /Decode entry: fall back to PDF's identity mapping.
            const double norm =
              static_cast<double>(raw_sample) / static_cast<double>(sample_max);
            return static_cast<std::uint8_t>(std::lround(norm * 255.0));
          };

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(src->getBuffer());
        auto expanded = std::make_shared<std::vector<uint8_t>>();
        expanded->reserve(static_cast<std::size_t>(w) * h * ncomps);

        for(int row = 0; row < h; ++row)
          {
            const auto* row_ptr = bytes + static_cast<std::size_t>(row) * row_stride;
            std::size_t bit_offset = 0;
            for(int col = 0; col < w; ++col)
              {
                for(int comp = 0; comp < ncomps; ++comp)
                  {
                    std::uint32_t raw_sample = 0u;
                    for(int bit = 0; bit < bits_per_component; ++bit)
                      {
                        const std::size_t absolute_bit = bit_offset + static_cast<std::size_t>(bit);
                        const std::size_t byte_index = absolute_bit / 8u;
                        const int bit_in_byte = 7 - static_cast<int>(absolute_bit % 8u);
                        const std::uint8_t byte = row_ptr[byte_index];
                        raw_sample = (raw_sample << 1u)
                          | static_cast<std::uint32_t>((byte >> bit_in_byte) & 1u);
                      }
                    expanded->push_back(decode_sample(comp, raw_sample));
                    bit_offset += static_cast<std::size_t>(bits_per_component);
                  }
              }
          }

        pixel_data = std::move(expanded);
        pixel_shape = {h, w, ncomps};
        channels = ncomps;

        LOG_S(INFO) << "bitmap: unpacked sub-byte decoded_stream_data"
                    << " for xobject_key=" << image.xobject_key
                    << " width=" << w
                    << " height=" << h
                    << " channels=" << ncomps
                    << " bpc=" << bits_per_component
                    << " row_stride=" << row_stride
                    << " output_size=" << pixel_data->size();
        return true;
      };

    if(image.image_mask)
      {
        fmt = PIXEL_FORMAT_GRAY; channels = 1;
      }
    else if(image.color_space == "/DeviceGray")
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
    else if(image.color_space.find("/DeviceN") != std::string::npos
            and image.device_n_components > 0)
      {
        LOG_S(INFO) << "bitmap: DeviceN color space with N=" << image.device_n_components
                    << " for xobject_key=" << image.xobject_key;
        if(image.device_n_components == 1)
          {
            fmt = PIXEL_FORMAT_GRAY; channels = 1;
          }
        else if(image.device_n_components == 3)
          {
            fmt = PIXEL_FORMAT_RGB; channels = 3;
          }
        else if(image.device_n_components == 4)
          {
            fmt = PIXEL_FORMAT_CMYK; channels = 4;
          }
        else
          {
            LOG_S(WARNING) << "bitmap: DeviceN with unsupported N="
                           << image.device_n_components
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

        channels = ncomps;

        if(ncomps > 0 and image.decoded_stream_data and image.decoded_stream_data->getSize() > 0)
          {
            const int w = image.image_width;
            const int h = image.image_height;
            const auto* indices = reinterpret_cast<const uint8_t*>(
              image.decoded_stream_data->getBuffer());
            const size_t n_indices = image.decoded_stream_data->getSize();
            if(expand_indexed_samples(ncomps, indices, n_indices, w, h))
              {
                LOG_S(INFO) << "bitmap: expanded Indexed palette for xobject_key="
                            << image.xobject_key
                            << " (" << n_indices << " indices -> "
                            << pixel_data->size() << " bytes, ncomps=" << ncomps << ")";
              }
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
        const bool has_flate = std::find(image.filters.begin(), image.filters.end(),
                                         "/FlateDecode") != image.filters.end();
        const bool has_jpx = std::find(image.filters.begin(), image.filters.end(),
                                       "/JPXDecode") != image.filters.end();

        if (image.decoded_stream_data and image.decoded_stream_data->getSize() > 0)
          {
            const int w           = image.image_width;
            const int h           = image.image_height;
            const auto src        = image.decoded_stream_data;

            if(image.bits_per_component > 0 and image.bits_per_component < 8)
              {
                if(not unpack_subbyte_samples_to_u8(src,
                                                    w,
                                                    h,
                                                    channels,
                                                    image.bits_per_component,
                                                    image.decode_array))
                  {
                    LOG_S(WARNING) << "bitmap: failed to unpack sub-byte decoded_stream_data "
                                   << "for xobject_key=" << image.xobject_key;
                  }
              }
            else
              {
                const size_t expected = static_cast<size_t>(w) * h * channels;
                if (src->getSize() >= expected)
                  {
                    const auto* raw = reinterpret_cast<const uint8_t*>(src->getBuffer());
                    pixel_data  = std::make_shared<std::vector<uint8_t>>(raw, raw + expected);
                    apply_decode_to_u8_samples(pixel_data, channels);
                    pixel_shape = {h, w, channels};
                  }
                else
                  {
                    LOG_S(WARNING) << "bitmap: decoded_stream_data too small ("
                                   << src->getSize() << " < " << expected
                                   << ") for xobject_key=" << image.xobject_key;
                  }
              }
          }
        else if (has_dct and image.raw_stream_data and image.raw_stream_data->getSize() > 0)
          {
            LOG_S(INFO) << "bitmap: decoded_stream_data unavailable for /DCTDecode image, "
                        << "decoding JPEG via libjpeg "
                        << "for xobject_key=" << image.xobject_key;

            jpeg::ColorSpace cs = jpeg::to_color_space(image.color_space);
            if (cs == jpeg::ColorSpace::Unknown and image.icc_components > 0)
              {
                cs = jpeg::icc_n_to_color_space(image.icc_components);
              }

            jpeg::jpeg_parameters params;
            params.color_space = cs;
            params.width       = image.image_width;
            params.height      = image.image_height;
            params.decode      = image.decode_array;
            params.has_decode  = image.decode_present and not image.decode_array.empty();

            LOG_S(INFO) << "bitmap: JPEG fallback parameters"
                        << " xobject_key=" << image.xobject_key
                        << " declared_cs=" << image.color_space
                        << " icc_components=" << image.icc_components
                        << " requested_cs=" << jpeg::color_space_name(cs)
                        << " size=" << params.width << "x" << params.height
                        << " decode_len=" << params.decode.size();

            auto decoded = jpeg::decode_pdf_jpeg_stream_to_raw_pixels(
                reinterpret_cast<unsigned char const*>(image.raw_stream_data->getBuffer()),
                static_cast<std::size_t>(image.raw_stream_data->getSize()),
                has_flate,
                params);

            if (not decoded.empty())
              {
                const int w = decoded.width  > 0 ? decoded.width  : image.image_width;
                const int h = decoded.height > 0 ? decoded.height : image.image_height;
                const int decoded_channels = decoded.components;
                pixel_data  = std::make_shared<std::vector<uint8_t>>(std::move(decoded.pixels));
                pixel_shape = {h, w, decoded_channels};
                channels    = decoded_channels;

                if(decoded_channels == 1)
                  {
                    fmt = PIXEL_FORMAT_GRAY;
                  }
                else if(decoded_channels == 3)
                  {
                    fmt = PIXEL_FORMAT_RGB;
                  }
                else if(decoded_channels == 4)
                  {
                    fmt = PIXEL_FORMAT_CMYK;
                    if(image.decode_present and has_default_adobe_cmyk_decode(image.decode_array))
                      {
                        cmyk_conv = CMYK_CONVENTION_ADOBE_INVERTED;
                      }
                  }
                else
                  {
                    fmt = PIXEL_FORMAT_UNKNOWN;
                  }

                LOG_S(INFO) << "bitmap: libjpeg decode succeeded "
                            << "for xobject_key=" << image.xobject_key
                            << " actual_cs=" << jpeg::color_space_name(decoded.color_space)
                            << " actual_shape=" << h << "x" << w << "x" << decoded_channels;
              }
            else
              {
                LOG_S(WARNING) << "bitmap: libjpeg decode failed "
                               << "for xobject_key=" << image.xobject_key;
              }
          }
        else if (has_jpx and image.raw_stream_data and image.raw_stream_data->getSize() > 0)
          {
            LOG_S(INFO) << "bitmap: decoded_stream_data unavailable for /JPXDecode image, "
                        << "decoding JPEG2000 via OpenJPEG "
                        << "for xobject_key=" << image.xobject_key;

            auto decoded = jpx::decode_jpx_to_raw_pixels(
                reinterpret_cast<uint8_t const*>(image.raw_stream_data->getBuffer()),
                static_cast<std::size_t>(image.raw_stream_data->getSize()));

            if(not decoded.empty())
              {
                if(image.indexed_palette and not image.indexed_palette->empty())
                  {
                    LOG_S(INFO) << "bitmap: Indexed JPX fallback metadata "
                                << "xobject_key=" << image.xobject_key
                                << " base_cs=" << image.indexed_base_cs
                                << " expected_components=" << channels
                                << " palette_bytes=" << image.indexed_palette->size()
                                << " decoded_components=" << decoded.components;

                    if(decoded.components == 1
                       and expand_indexed_samples(channels,
                                                  decoded.pixels.data(),
                                                  decoded.pixels.size(),
                                                  decoded.width,
                                                  decoded.height))
                      {
                        LOG_S(INFO) << "bitmap: OpenJPEG decode succeeded for Indexed image "
                                    << "xobject_key=" << image.xobject_key
                                    << " actual_shape=" << decoded.height << "x"
                                    << decoded.width << "x" << decoded.components
                                    << " expanded_shape=" << pixel_shape[0] << "x"
                                    << pixel_shape[1] << "x" << pixel_shape[2];
                      }
                    else if(decoded.components == channels)
                      {
                        pixel_data = std::make_shared<std::vector<uint8_t>>(std::move(decoded.pixels));
                        pixel_shape = {decoded.height, decoded.width, decoded.components};

                        LOG_S(INFO) << "bitmap: OpenJPEG returned already-expanded pixels "
                                    << "for Indexed image xobject_key=" << image.xobject_key
                                    << " actual_cs=" << jpeg::color_space_name(decoded.color_space)
                                    << " actual_shape=" << decoded.height << "x"
                                    << decoded.width << "x" << decoded.components;
                      }
                    else
                      {
                        LOG_S(WARNING) << "bitmap: OpenJPEG decode for Indexed image returned "
                                       << decoded.components
                                       << " components, expected 1, for xobject_key="
                                       << image.xobject_key;
                      }
                  }
                else
                  {
                    pixel_data = std::make_shared<std::vector<uint8_t>>(std::move(decoded.pixels));
                    apply_decode_to_u8_samples(pixel_data, decoded.components);
                    pixel_shape = {decoded.height, decoded.width, decoded.components};
                    channels = decoded.components;

                    if(decoded.components == 1)
                      {
                        fmt = PIXEL_FORMAT_GRAY;
                      }
                    else if(decoded.components == 3)
                      {
                        fmt = PIXEL_FORMAT_RGB;
                      }
                    else if(decoded.components == 4)
                      {
                        fmt = PIXEL_FORMAT_CMYK;
                        cmyk_conv = CMYK_CONVENTION_PROCESS;
                      }
                    else
                      {
                        fmt = PIXEL_FORMAT_UNKNOWN;
                      }

                    LOG_S(INFO) << "bitmap: OpenJPEG decode succeeded "
                                << "for xobject_key=" << image.xobject_key
                                << " actual_cs=" << jpeg::color_space_name(decoded.color_space)
                                << " actual_shape=" << decoded.height << "x"
                                << decoded.width << "x" << decoded.components;
                  }
              }
            else
              {
                LOG_S(WARNING) << "bitmap: OpenJPEG decode failed "
                               << "for xobject_key=" << image.xobject_key;
              }
          }
        else if (std::find(image.filters.begin(), image.filters.end(),
                           "/JBIG2Decode") != image.filters.end()
                 and ((image.raw_stream_data and image.raw_stream_data->getSize() > 0)
                      or (image.decoded_stream_data and image.decoded_stream_data->getSize() > 0)))
          {
            const int w = image.image_width;
            const int h = image.image_height;

            std::shared_ptr<Buffer> page_stream_data;
            const char*             page_stream_source = "none";
            if (image.decoded_stream_data and image.decoded_stream_data->getSize() > 0)
              {
                page_stream_data   = image.decoded_stream_data;
                page_stream_source = "decoded";
              }
            else if (image.raw_stream_data and image.raw_stream_data->getSize() > 0)
              {
                page_stream_data   = image.raw_stream_data;
                page_stream_source = "raw";
              }

            const auto* page_buf =
                reinterpret_cast<const uint8_t*>(page_stream_data->getBuffer());
            const std::size_t page_size = page_stream_data->getSize();

            const uint8_t*  globals_buf  = nullptr;
            std::size_t     globals_size = 0;
            if (image.jbig2_globals_data and image.jbig2_globals_data->getSize() > 0)
              {
                globals_buf  = reinterpret_cast<const uint8_t*>(
                    image.jbig2_globals_data->getBuffer());
                globals_size = image.jbig2_globals_data->getSize();
              }

            LOG_S(INFO) << "bitmap: /JBIG2Decode image is getting decoded"
                        << " for xobject_key=" << image.xobject_key
                        << " page_stream_source=" << page_stream_source
                        << " page_size=" << page_size
                        << " has_globals="
                        << ((image.jbig2_globals_data and image.jbig2_globals_data->getSize() > 0) ? "true" : "false")
                        << " width=" << image.image_width
                        << " height=" << image.image_height
                        << " bpc=" << image.bits_per_component
                        << " image_mask=" << (image.image_mask ? "true" : "false");

            auto bits = jbig2_decode(
                {page_buf,    page_size},
                {globals_buf, globals_size},
                static_cast<uint32_t>(w),
                static_cast<uint32_t>(h));

            if (not bits.empty())
              {
                // Expand 1bpp packed bitmap → 8bpp grayscale.
                // For ordinary JBIG2 images, bit 1 means black and bit 0 means white.
                // For image masks, honor PDF /Decode semantics:
                //   [0 1] => 0 paints, 1 leaves unchanged
                //   [1 0] => reversed
                const uint32_t pitch = (static_cast<uint32_t>(w) + 7u) / 8u;
                auto expanded = std::make_shared<std::vector<uint8_t>>();
                expanded->reserve(static_cast<std::size_t>(w) * h);
                bool mask_zero_paints = true;
                if (image.image_mask
                    and image.decode_present
                    and image.decode_array.size() >= 2)
                  {
                    mask_zero_paints =
                      std::abs(image.decode_array[0] - 0.0) < 1e-12
                      and std::abs(image.decode_array[1] - 1.0) < 1e-12;
                  }
                for (int row = 0; row < h; ++row)
                  {
                    for (int col = 0; col < w; ++col)
                      {
                        const uint8_t byte = bits[row * pitch + col / 8];
                        const bool    bit = ((byte >> (7 - (col % 8))) & 1u) != 0;
                        const bool    black = image.image_mask
                          ? (mask_zero_paints ? !bit : bit)
                          : bit;
                        expanded->push_back(black ? 0x00u : 0xFFu);
                      }
                  }

                fmt         = PIXEL_FORMAT_GRAY;
                pixel_data  = std::move(expanded);
                pixel_shape = {h, w, 1};

                LOG_S(INFO) << "bitmap: /JBIG2Decode decode succeeded"
                            << " for xobject_key=" << image.xobject_key
                            << " shape=" << h << "x" << w
                            << " pixel_data_size=" << pixel_data->size();
              }
            else
              {
                LOG_S(WARNING) << "bitmap: /JBIG2Decode decode failed"
                               << " for xobject_key=" << image.xobject_key;
              }
          }
        else if (std::find(image.filters.begin(), image.filters.end(),
                           "/CCITTFaxDecode") != image.filters.end()
                 and image.raw_stream_data
                 and image.raw_stream_data->getSize() > 0)
          {
            LOG_S(INFO) << "bitmap: decoded_stream_data unavailable for /CCITTFaxDecode image, "
                        << "decoding via built-in CCITT decoder "
                        << "for xobject_key=" << image.xobject_key;

            const int w = image.image_width;
            const int h = image.image_height;

            auto decoded = ccitt::decode(
                reinterpret_cast<const uint8_t*>(image.raw_stream_data->getBuffer()),
                static_cast<size_t>(image.raw_stream_data->getSize()),
                w, h,
                image.ccitt_k,
                image.ccitt_black_is_1);

            if(not decoded.empty())
              {
                // --- TEMPORARY DEBUG: save the raw decoded pixels as a PNG ---
		bool export_to_png_for_debug = false; // should always be false in production!!
		if(export_to_png_for_debug)
		  {
		    // Strip the leading '/' that PDF keys always carry (e.g. "/Im0" → "Im0").
		    std::string tmp_name = image.xobject_key;
		    if(not tmp_name.empty() and tmp_name[0] == '/')
		      {
			tmp_name = tmp_name.substr(1);
		      }
		    std::string dbg_path = "./tmp/ccitt_debug_" + tmp_name + ".png";
		    
		    LOG_S(WARNING) << "saving PNG image at: " << dbg_path;
		    ccitt::save_debug_png(decoded, w, h, dbg_path);
		  }
		
                pixel_data  = std::make_shared<std::vector<uint8_t>>(std::move(decoded));
                pixel_shape = {h, w, channels};
              }
            else
              {
                LOG_S(WARNING) << "bitmap: CCITT decode failed "
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
                              image.soft_mask_data,
                              cmyk_conv,
                              pixel_shape,
                              fmt,
                              image.image_mask,
                              image.rgb_filling_ops,
                              image.r_x0, image.r_y0,
                              image.r_x1, image.r_y1,
                              image.r_x2, image.r_y2,
                              image.r_x3, image.r_y3,
                              std::move(clip_state));
    instructions.add_bitmap_instruction(std::move(binstr));
  }

}

#endif
