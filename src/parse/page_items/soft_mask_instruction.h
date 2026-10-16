//-*-C++-*-

#ifndef PDF_SOFT_MASK_INSTRUCTION_H
#define PDF_SOFT_MASK_INSTRUCTION_H

namespace pdflib
{
  // An ExtGState soft mask (ISO 32000-1, 11.6.5), decoded for the renderer.
  //
  // The mask is a transparency group whose luminosity (or alpha) becomes the
  // coverage of everything painted while the mask is in force. The group's
  // content has been sub-decoded into `group`: ordinary render instructions
  // in page space, placed by the CTM in force at the `gs` that installed the
  // mask -- which is what fixes the mask's coordinate system (11.6.5.1), not
  // the CTM at the paint -- composed with the group's /Matrix.
  //
  // One instance is shared by every instruction painted under the same mask,
  // so a renderer rasterises the group once and reuses the coverage.
  class soft_mask_instruction
  {
  public:
    soft_mask_instruction(std::string key,
                          bool luminosity,
                          std::array<int, 3> backdrop_rgb,
                          std::vector<uint8_t> transfer,
                          std::array<double, 8> bbox_quad):
      key_(std::move(key)),
      luminosity_(luminosity),
      backdrop_rgb_(backdrop_rgb),
      transfer_(std::move(transfer)),
      bbox_quad_(bbox_quad)
    {}

    // The ExtGState resource name the mask came from, for diagnostics.
    const std::string& get_key() const { return key_; }

    // /S /Luminosity reads the coverage off the group's colour, /S /Alpha
    // off its alpha.
    bool is_luminosity() const { return luminosity_; }

    // /BC, the colour the group is composited against, in device RGB. It is
    // also what shows outside the group's /BBox. Black when absent.
    const std::array<int, 3>& get_backdrop_rgb() const { return backdrop_rgb_; }

    // /TR sampled at 256 points, mapping luminosity (or alpha) to coverage;
    // empty for the identity.
    const std::vector<uint8_t>& get_transfer() const { return transfer_; }

    // The group's /BBox as four page-space corners (x0, y0, ..., x3, y3).
    const std::array<double, 8>& get_bbox_quad() const { return bbox_quad_; }

    // The group's content, as instructions in page space.
    pdf_render_instructions&       get_group()       { return group_; }
    const pdf_render_instructions& get_group() const { return group_; }

  private:

    std::string           key_;
    bool                  luminosity_;
    std::array<int, 3>    backdrop_rgb_;
    std::vector<uint8_t>  transfer_;
    std::array<double, 8> bbox_quad_;

    pdf_render_instructions group_;
  };

}

#endif
