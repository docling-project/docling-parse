//-*-C++-*-

#ifndef PDF_BLEND2D_RENDERER_H
#define PDF_BLEND2D_RENDERER_H

#include <render/template_renderer.h>

#include <blend2d/blend2d.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace pdflib
{
  template<>
  class renderer<BLEND2D>
  {
  public:

    renderer();

    void set_size(size_instruction& instr);
    void render_text(text_instruction& instr);
    void render_bitmap(bitmap_instruction& instr);
    void render_shape(shape_instruction& instr);

    // Returns the rendered canvas as RGBA bytes, row-major top-to-bottom.
    // The associated shape is {height, width, 4}.
    std::shared_ptr<std::vector<uint8_t>> get_canvas() const;
    const std::array<int, 3>& get_shape() const { return shape_; }

    // Save the canvas to a file.  The format is inferred from the extension
    // (e.g. ".png", ".bmp").  PNG is recommended; it is built into Blend2D.
    void save(const std::string& path) const;

    // Save the canvas to a temporary PNG and open it with the OS default
    // image viewer (like PIL's Image.show()).
    void show() const;

  private:

    mutable BLImage    image_;  // internal canvas (PRGB32 format)
    std::array<int, 3> shape_;  // {height, width, 4}

    // Convert a PDF y-coordinate (origin bottom-left) to a canvas
    // y-coordinate (origin top-left).
    double canvas_y(double pdf_y) const
    {
      return static_cast<double>(shape_[0]) - pdf_y;
    }
  };

  // ---------------------------------------------------------------------------
  // Constructor
  // ---------------------------------------------------------------------------

  inline renderer<BLEND2D>::renderer()
    : shape_({0, 0, 4})
  {}

  // ---------------------------------------------------------------------------
  // set_size
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::set_size(size_instruction& instr)
  {
    const auto& bbox = instr.crop_bbox;
    const int width  = bbox[2] - bbox[0];
    const int height = bbox[3] - bbox[1];

    if (width <= 0 or height <= 0) { return; }

    shape_ = {height, width, 4};
    image_.create(width, height, BL_FORMAT_PRGB32);

    // Initialise canvas to opaque white.
    BLContext ctx(image_);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
    ctx.fill_all();
    ctx.end();
  }

  // ---------------------------------------------------------------------------
  // render_text
  //
  // Draws the text bounding quad as a light-blue filled region with a thin
  // blue outline.  Actual glyph rendering would require resolving font_key to
  // a font file; that is left as a future extension.
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::render_text(text_instruction& instr)
  {
    LOG_S(INFO) << __FUNCTION__;

    if (shape_[0] == 0 or shape_[1] == 0) { return; }

    const double x0 = instr.get_r_x0(), y0 = canvas_y(instr.get_r_y0());
    const double x1 = instr.get_r_x1(), y1 = canvas_y(instr.get_r_y1());
    const double x2 = instr.get_r_x2(), y2 = canvas_y(instr.get_r_y2());
    const double x3 = instr.get_r_x3(), y3 = canvas_y(instr.get_r_y3());

    BLPath path;
    path.move_to(x0, y0);
    path.line_to(x1, y1);
    path.line_to(x2, y2);
    path.line_to(x3, y3);
    path.close();

    BLContext ctx(image_);
    // Semi-transparent fill (~20 % opacity light blue)
    ctx.set_fill_style(BLRgba32(0x33AEC6FFu));
    ctx.fill_path(path);
    // Solid thin blue outline
    ctx.set_stroke_style(BLRgba32(0xFF1070C0u));
    ctx.set_stroke_width(0.5);
    ctx.stroke_path(path);
    ctx.end();
  }

  // ---------------------------------------------------------------------------
  // render_bitmap
  //
  // Converts the raw pixel data into a BLImage and blits it into the
  // axis-aligned bounding box of the destination quad (y-flipped).
  // For non-axis-aligned quads a full affine transform would be needed.
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::render_bitmap(bitmap_instruction& instr)
  {
    LOG_S(INFO) << __FUNCTION__;

    if (shape_[0] == 0 or shape_[1] == 0)
      {
        LOG_S(WARNING) << __FUNCTION__ << ": canvas not initialised, skipping";
        return;
      }

    // Compute axis-aligned destination rectangle in canvas coordinates first,
    // so we can draw a placeholder even when pixel data is unavailable.
    const double x_min = std::min({instr.get_r_x0(), instr.get_r_x1(),
        instr.get_r_x2(), instr.get_r_x3()});
    const double x_max = std::max({instr.get_r_x0(), instr.get_r_x1(),
        instr.get_r_x2(), instr.get_r_x3()});
    const double y_min = std::min({instr.get_r_y0(), instr.get_r_y1(),
        instr.get_r_y2(), instr.get_r_y3()});
    const double y_max = std::max({instr.get_r_y0(), instr.get_r_y1(),
        instr.get_r_y2(), instr.get_r_y3()});

    const double dst_w = x_max - x_min;
    const double dst_h = y_max - y_min;
    if (dst_w <= 0.0 or dst_h <= 0.0)
      {
        LOG_S(WARNING) << __FUNCTION__ << ": degenerate destination rect, skipping";
        return;
      }

    // canvas_y(y_max) gives the top-left y of the destination in canvas space.
    const double dst_x = x_min;
    const double dst_y = canvas_y(y_max);
    const BLRect dst_rect(dst_x, dst_y, dst_w, dst_h);

    const auto& src_data  = instr.get_data();
    const auto& src_shape = instr.get_shape(); // {height, width, channels}
    const int sh = src_shape[0];
    const int sw = src_shape[1];
    const int sc = src_shape[2];

    BLContext ctx(image_);

    if ((not instr.has_data()) or sh <= 0 or sw <= 0 or sc < 1)
      {
        LOG_S(WARNING) << "No pixel data — draw a semi-transparent yellow placeholder.";
        // No pixel data — draw a semi-transparent yellow placeholder.
        ctx.set_fill_style(BLRgba32(0x66FFFF00u)); // A=40%, R=255, G=255, B=0
        ctx.fill_rect(dst_rect);
        ctx.end();
        return;
      }

    // Build a BLImage (PRGB32) from the raw channel data.
    BLImage src_img;
    src_img.create(sw, sh, BL_FORMAT_PRGB32);

    {
      BLImageData img_data;
      src_img.make_mutable(&img_data);
      auto* base = static_cast<uint8_t*>(img_data.pixel_data);
      const intptr_t stride = img_data.stride;

      for (int row = 0; row < sh; ++row)
        {
          auto* row_ptr = reinterpret_cast<uint32_t*>(base + row * stride);
          for (int col = 0; col < sw; ++col)
            {
              const int idx = (row * sw + col) * sc;
              const uint8_t r = src_data->at(idx);
              const uint8_t g = (sc >= 2) ? src_data->at(idx + 1) : r;
              const uint8_t b = (sc >= 3) ? src_data->at(idx + 2) : r;
              const uint8_t a = (sc >= 4) ? src_data->at(idx + 3) : 0xFFu;

              // Store as premultiplied ARGB (required by BL_FORMAT_PRGB32).
              const uint32_t pm_r = static_cast<uint32_t>(r) * a / 255u;
              const uint32_t pm_g = static_cast<uint32_t>(g) * a / 255u;
              const uint32_t pm_b = static_cast<uint32_t>(b) * a / 255u;
              row_ptr[col] = (static_cast<uint32_t>(a) << 24)
                | (pm_r                     << 16)
                | (pm_g                     <<  8)
                |  pm_b;
            }
        }
    }

    ctx.blit_image(dst_rect, src_img, BLRectI(0, 0, sw, sh));
    ctx.end();
  }

  // ---------------------------------------------------------------------------
  // render_shape
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::render_shape(shape_instruction& instr)
  {
    LOG_S(INFO) << __FUNCTION__;

    if (shape_[0] == 0 or shape_[1] == 0) { return; }
    if (instr.size() < 2) { return; }

    const auto& xs = instr.get_x();
    const auto& ys = instr.get_y();

    BLPath path;
    path.move_to(xs[0], canvas_y(ys[0]));
    LOG_S(INFO) << " -> add point: (" << xs[0] << ", " << ys[0] << ")";
    for (size_t i = 1; i < instr.size(); ++i)
      {
        LOG_S(INFO) << " -> add point: (" << xs[i] << ", " << ys[i] << ")";
        path.line_to(xs[i], canvas_y(ys[i]));
      }

    BLContext ctx(image_);
    ctx.set_stroke_style(BLRgba32(0xFF000000u));
    ctx.set_stroke_width(1.0);
    ctx.stroke_path(path);
    ctx.end();
  }

  // ---------------------------------------------------------------------------
  // get_canvas
  //
  // Extracts the internal PRGB32 canvas as straight RGBA bytes.
  // Since the canvas is always opaque (alpha == 255 everywhere), no
  // un-premultiplication is necessary.
  // ---------------------------------------------------------------------------

  inline std::shared_ptr<std::vector<uint8_t>> renderer<BLEND2D>::get_canvas() const
  {

    const int h = shape_[0];
    const int w = shape_[1];
    if (h == 0 or w == 0)
      {
        return std::make_shared<std::vector<uint8_t>>();
      }

    BLImageData img_data;
    image_.get_data(&img_data);

    auto result = std::make_shared<std::vector<uint8_t>>(
                                                         static_cast<std::size_t>(h) * w * 4);

    const auto* base = static_cast<const uint8_t*>(img_data.pixel_data);
    const intptr_t stride = img_data.stride;

    for (int row = 0; row < h; ++row)
      {
        const auto* src_row =
          reinterpret_cast<const uint32_t*>(base + row * stride);
        uint8_t* dst_row = result->data() + row * w * 4;

        for (int col = 0; col < w; ++col)
          {
            // BL_FORMAT_PRGB32 value = A<<24 | R<<16 | G<<8 | B  (little-endian)
            const uint32_t px = src_row[col];
            dst_row[col * 4 + 0] = static_cast<uint8_t>((px >> 16) & 0xFFu); // R
            dst_row[col * 4 + 1] = static_cast<uint8_t>((px >>  8) & 0xFFu); // G
            dst_row[col * 4 + 2] = static_cast<uint8_t>((px >>  0) & 0xFFu); // B
            dst_row[col * 4 + 3] = static_cast<uint8_t>((px >> 24) & 0xFFu); // A
          }
      }

    return result;
  }

  // ---------------------------------------------------------------------------
  // save
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::save(const std::string& path) const
  {

    if (shape_[0] == 0 or shape_[1] == 0)
      {
        throw std::runtime_error("renderer<BLEND2D>::save: canvas is empty");
      }

    const BLResult err = image_.write_to_file(path.c_str());
    if (err != BL_SUCCESS)
      {
        throw std::runtime_error(
                                 "renderer<BLEND2D>::save: failed to write '" + path + "' "
                                 "(BLResult=" + std::to_string(err) + ")");
      }
  }

  // ---------------------------------------------------------------------------
  // show
  //
  // Writes the canvas to a temporary PNG file and opens it with the platform's
  // default image viewer, mirroring the behaviour of PIL's Image.show().
  // ---------------------------------------------------------------------------

  inline void renderer<BLEND2D>::show() const
  {
    namespace fs = std::filesystem;

    const std::string tmp =
      (fs::temp_directory_path() / "blend2d_renderer_preview.png").string();

    save(tmp);

    const std::string cmd =
#if defined(_WIN32)
      "start \"\" \"" + tmp + "\"";
#elif defined(__APPLE__)
    "open " + tmp;
#else
    "xdg-open " + tmp + " &";
#endif

    std::system(cmd.c_str()); // NOLINT(cert-env33-c)
  }

} // namespace pdflib

#endif // PDF_BLEND2D_RENDERER_H
