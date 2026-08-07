//-*-C++-*-

#ifndef PDF_ENUMS_H
#define PDF_ENUMS_H

namespace pdflib
{
  enum font_subtype_name {
    TYPE_0,
    TYPE_1,
    TYPE_2,
    TYPE_3,

    MM_TYPE_1,
    TRUE_TYPE,

    CID_FONT_TYPE_0,
    CID_FONT_TYPE_1,
    CID_FONT_TYPE_2,

    NULL_TYPE
  };

  font_subtype_name to_subtype_name(std::string name)
  {
    if      (name=="TYPE_0" or name=="/Type0")    { return TYPE_0; }
    else if (name=="TYPE_1" or name=="/Type1")    { return TYPE_1; }
    else if (name=="TYPE_2" or name=="/Type2")    { return TYPE_2; }
    else if (name=="TYPE_3" or name=="/Type3")    { return TYPE_3; }

    else if (name=="MM_TYPE_1" or name=="/MMType1") { return MM_TYPE_1; }
    else if (name=="TRUE_TYPE" or name=="/TrueType") { return TRUE_TYPE; }

    else if (name=="CID_FONT_TYPE_0" or name=="/CIDFontType0") { return CID_FONT_TYPE_0; }
    else if (name=="CID_FONT_TYPE_1" or name=="/CIDFontType1") { return CID_FONT_TYPE_1; }
    else if (name=="CID_FONT_TYPE_2" or name=="/CIDFontType2") { return CID_FONT_TYPE_2; }
    else
      {
        LOG_S(ERROR) << "unknown subtype " << name;
        return NULL_TYPE; 
      }
  }

  std::string to_string(font_subtype_name name)
  {
    switch(name)
      {
      case TYPE_0: { return "TYPE_0"; }
      case TYPE_1: { return "TYPE_1"; }
      case TYPE_2: { return "TYPE_2"; }
      case TYPE_3: { return "TYPE_3"; }

      case MM_TYPE_1: { return "MM_TYPE_1"; }
      case TRUE_TYPE: { return "TRUE_TYPE"; }

      case CID_FONT_TYPE_0: { return "CID_FONT_TYPE_0"; }
      case CID_FONT_TYPE_1: { return "CID_FONT_TYPE_1"; }
      case CID_FONT_TYPE_2: { return "CID_FONT_TYPE_2"; }

      default:
        {
          LOG_S(ERROR) << "encountered a NULL_ENCODING";
          return "NULL_ENCODING";
        }
      }
  }

  enum font_encoding_name {
    NULL_ENCODING,

    STANDARD,
    MACROMAN,
    MACEXPERT,
    WINANSI,

    IDENTITY_H,
    IDENTITY_V,

    CMAP_RESOURCES
  };

  font_encoding_name to_encoding_name(std::string name)
  {
    if     (name=="STANDARD"   or name=="/StandardEncoding" ) { return STANDARD; }
    else if(name=="MACROMAN"   or name=="/MacRomanEncoding" ) { return MACROMAN; }
    else if(name=="MACEXPERT"  or name=="/MacExpertEncoding") { return MACEXPERT; }
    else if(name=="WINANSI"    or name=="/WinAnsiEncoding"  ) { return WINANSI; }
    else if(name=="IDENTITY_H" or name=="/Identity-H"       ) { return IDENTITY_H; }
    else if(name=="IDENTITY_V" or name=="/Identity-V"       ) { return IDENTITY_V; }
    else if(name=="CMAP_RESOURCES"                          ) { return CMAP_RESOURCES; }
    else 
      {
        LOG_S(ERROR) << __FILE__ << ":" << __LINE__ << " --> unknown encoding " << name;
        return NULL_ENCODING; 
      }
  }

  std::string to_string(font_encoding_name name)
  {
    switch(name)
      {
      case STANDARD:   { return "STANDARD"; } break;
      case MACROMAN:   { return "MACROMAN"; } break;
      case MACEXPERT:  { return "MACEXPERT"; } break;
      case WINANSI:    { return "WINANSI"; } break;
      case IDENTITY_H: { return "IDENTITY_H"; } break;
      case IDENTITY_V: { return "IDENTITY_V"; } break;
      case CMAP_RESOURCES: { return "CMAP_RESOURCES"; } break;

      default:
        {
          LOG_S(ERROR) << "encountered a NULL_ENCODING";
          return "NULL_ENCODING";
        }
      }
  }

  enum embedded_font_file_kind
  {
    FONT_FILE_NONE,
    FONT_FILE_TYPE1,
    FONT_FILE_TRUETYPE,
    FONT_FILE_CFF
  };

  inline std::string to_string(embedded_font_file_kind kind)
  {
    switch(kind)
      {
      case FONT_FILE_NONE:     return "FONT_FILE_NONE";
      case FONT_FILE_TYPE1:    return "FONT_FILE_TYPE1";
      case FONT_FILE_TRUETYPE: return "FONT_FILE_TRUETYPE";
      case FONT_FILE_CFF:      return "FONT_FILE_CFF";
      }

    return "FONT_FILE_UNKNOWN";
  }

  // Distinguishes the /FontFile3 subtypes that embedded_font_file_kind folds
  // into FONT_FILE_CFF. The renderer needs this: Blend2D loads only SFNT
  // containers (TRUETYPE/OPENTYPE), while TYPE1/TYPE1C/CID_TYPE0C need a
  // different backend.
  enum class embedded_font_format
  {
    UNKNOWN,
    TYPE1,      // /FontFile  (PFA/PFB)
    TRUETYPE,   // /FontFile2
    TYPE1C,     // /FontFile3 /Subtype /Type1C (bare CFF)
    CID_TYPE0C, // /FontFile3 /Subtype /CIDFontType0C (CID-keyed bare CFF)
    OPENTYPE    // /FontFile3 /Subtype /OpenType
  };

  inline std::string to_string(embedded_font_format format)
  {
    switch(format)
      {
      case embedded_font_format::UNKNOWN:    return "UNKNOWN";
      case embedded_font_format::TYPE1:      return "TYPE1";
      case embedded_font_format::TRUETYPE:   return "TRUETYPE";
      case embedded_font_format::TYPE1C:     return "TYPE1C";
      case embedded_font_format::CID_TYPE0C: return "CID_TYPE0C";
      case embedded_font_format::OPENTYPE:   return "OPENTYPE";
      }

    return "UNKNOWN";
  }
  
  enum xobject_subtype_name {
    XOBJECT_UNKNOWN,

    XOBJECT_FORM,
    XOBJECT_IMAGE,
    XOBJECT_POSTSCRIPT
  };

  enum page_shape_closing_type {
    CLOSING_UNDEFINED,
    OPEN,
    CLOSED,
  };

  enum page_shape_type {
    SHAPE_UNDEFINED,
    LINE,        // straight line between two points
    RECTANGLE,   // straight lines between four points (closed rectangle)
    BEZIER,      // cubic Bézier curve (interpolated)
  };

  // How a path-painting operator paints the current path
  enum shape_paint_mode {
    SHAPE_PAINT_STROKE,        // S, s
    SHAPE_PAINT_FILL,          // f, F, f*
    SHAPE_PAINT_FILL_STROKE,   // B, B*, b, b*
  };

  enum shape_fill_rule {
    SHAPE_FILL_NONZERO,        // f, B, b
    SHAPE_FILL_EVEN_ODD,       // f*, B*, b*
  };

  // Exact path segment commands (each subpath starts with an implicit
  // move-to). Kept alongside the flattened polyline so the renderer can
  // rebuild true curves at device resolution.
  enum shape_segment_op {
    SEGMENT_LINE_TO,    // consumes 1 point:  end
    SEGMENT_CUBIC_TO,   // consumes 3 points: ctrl1, ctrl2, end
  };

  // Families of PDF colour spaces (8.6) that the SC/SCN/sc/scn operands are
  // interpreted against once CS/cs has resolved a named /ColorSpace resource.
  enum color_space_family {
    COLOR_SPACE_UNKNOWN,

    COLOR_SPACE_GRAY,       // DeviceGray, CalGray, ICCBased /N 1
    COLOR_SPACE_RGB,        // DeviceRGB, CalRGB, ICCBased /N 3
    COLOR_SPACE_CMYK,       // DeviceCMYK, ICCBased /N 4
    COLOR_SPACE_LAB,        // Lab (approximated by its L* component)

    COLOR_SPACE_INDEXED,    // palette lookup into a base space
    COLOR_SPACE_SEPARATION, // single tint, through a tint transform
    COLOR_SPACE_DEVICE_N,   // multiple tints, through a tint transform

    COLOR_SPACE_PATTERN
  };

  // The /ShadingType of a shading dictionary (Table 78). Only the axial and
  // radial types are painted; the others are recognised so that an
  // unsupported shading is reported by name instead of dropped silently.
  enum shading_type_name {
    SHADING_UNKNOWN               = 0,
    SHADING_FUNCTION_BASED        = 1,
    SHADING_AXIAL                 = 2,
    SHADING_RADIAL                = 3,
    SHADING_FREE_FORM_GOURAUD     = 4,
    SHADING_LATTICE_FORM_GOURAUD  = 5,
    SHADING_COONS_PATCH           = 6,
    SHADING_TENSOR_PATCH          = 7,
  };

  std::string to_string(shading_type_name name)
  {
    switch(name)
      {
      case SHADING_FUNCTION_BASED:       { return "function-based (1)"; }
      case SHADING_AXIAL:                { return "axial (2)"; }
      case SHADING_RADIAL:               { return "radial (3)"; }
      case SHADING_FREE_FORM_GOURAUD:    { return "free-form Gouraud triangle mesh (4)"; }
      case SHADING_LATTICE_FORM_GOURAUD: { return "lattice-form Gouraud triangle mesh (5)"; }
      case SHADING_COONS_PATCH:          { return "Coons patch mesh (6)"; }
      case SHADING_TENSOR_PATCH:         { return "tensor-product patch mesh (7)"; }

      default: { return "unknown (0)"; }
      }
  }

  shading_type_name to_shading_type_name(int type)
  {
    switch(type)
      {
      case 1: { return SHADING_FUNCTION_BASED; }
      case 2: { return SHADING_AXIAL; }
      case 3: { return SHADING_RADIAL; }
      case 4: { return SHADING_FREE_FORM_GOURAUD; }
      case 5: { return SHADING_LATTICE_FORM_GOURAUD; }
      case 6: { return SHADING_COONS_PATCH; }
      case 7: { return SHADING_TENSOR_PATCH; }

      default: { return SHADING_UNKNOWN; }
      }
  }

  // Blend modes of an ExtGState /BM entry (11.3.5). The separable modes are
  // composited; the four non-separable ones (Table 137) are parsed so that
  // documents relying on them are identifiable instead of silently rendered
  // as Normal.
  enum blend_mode_name {
    BLEND_MODE_UNKNOWN,

    // separable blend modes (Table 136)
    BLEND_MODE_NORMAL,
    BLEND_MODE_MULTIPLY,
    BLEND_MODE_SCREEN,
    BLEND_MODE_OVERLAY,
    BLEND_MODE_DARKEN,
    BLEND_MODE_LIGHTEN,
    BLEND_MODE_COLOR_DODGE,
    BLEND_MODE_COLOR_BURN,
    BLEND_MODE_HARD_LIGHT,
    BLEND_MODE_SOFT_LIGHT,
    BLEND_MODE_DIFFERENCE,
    BLEND_MODE_EXCLUSION,

    // non-separable blend modes (Table 137)
    BLEND_MODE_HUE,
    BLEND_MODE_SATURATION,
    BLEND_MODE_COLOR,
    BLEND_MODE_LUMINOSITY,
  };

  // State of an ExtGState /SMask entry. A present soft mask is the one
  // transparency parameter whose omission makes the output wrong rather than
  // merely approximate, so absent and /None are kept apart.
  enum soft_mask_state {
    SOFT_MASK_ABSENT,   // key not in the dictionary: inherit the current mask
    SOFT_MASK_NONE,     // /None: clear the current soft mask
    SOFT_MASK_PRESENT,  // a mask dictionary (not applied)
  };

  std::string to_string(blend_mode_name name)
  {
    switch(name)
      {
      case BLEND_MODE_NORMAL:      { return "/Normal"; }
      case BLEND_MODE_MULTIPLY:    { return "/Multiply"; }
      case BLEND_MODE_SCREEN:      { return "/Screen"; }
      case BLEND_MODE_OVERLAY:     { return "/Overlay"; }
      case BLEND_MODE_DARKEN:      { return "/Darken"; }
      case BLEND_MODE_LIGHTEN:     { return "/Lighten"; }
      case BLEND_MODE_COLOR_DODGE: { return "/ColorDodge"; }
      case BLEND_MODE_COLOR_BURN:  { return "/ColorBurn"; }
      case BLEND_MODE_HARD_LIGHT:  { return "/HardLight"; }
      case BLEND_MODE_SOFT_LIGHT:  { return "/SoftLight"; }
      case BLEND_MODE_DIFFERENCE:  { return "/Difference"; }
      case BLEND_MODE_EXCLUSION:   { return "/Exclusion"; }
      case BLEND_MODE_HUE:         { return "/Hue"; }
      case BLEND_MODE_SATURATION:  { return "/Saturation"; }
      case BLEND_MODE_COLOR:       { return "/Color"; }
      case BLEND_MODE_LUMINOSITY:  { return "/Luminosity"; }

      default: { return "/Unknown"; }
      }
  }

  // /Compatible is a deprecated synonym of /Normal (11.3.5.2)
  blend_mode_name to_blend_mode_name(const std::string& name)
  {
    if(name=="/Normal" or name=="/Compatible") { return BLEND_MODE_NORMAL; }
    if(name=="/Multiply")                      { return BLEND_MODE_MULTIPLY; }
    if(name=="/Screen")                        { return BLEND_MODE_SCREEN; }
    if(name=="/Overlay")                       { return BLEND_MODE_OVERLAY; }
    if(name=="/Darken")                        { return BLEND_MODE_DARKEN; }
    if(name=="/Lighten")                       { return BLEND_MODE_LIGHTEN; }
    if(name=="/ColorDodge")                    { return BLEND_MODE_COLOR_DODGE; }
    if(name=="/ColorBurn")                     { return BLEND_MODE_COLOR_BURN; }
    if(name=="/HardLight")                     { return BLEND_MODE_HARD_LIGHT; }
    if(name=="/SoftLight")                     { return BLEND_MODE_SOFT_LIGHT; }
    if(name=="/Difference")                    { return BLEND_MODE_DIFFERENCE; }
    if(name=="/Exclusion")                     { return BLEND_MODE_EXCLUSION; }
    if(name=="/Hue")                           { return BLEND_MODE_HUE; }
    if(name=="/Saturation")                    { return BLEND_MODE_SATURATION; }
    if(name=="/Color")                         { return BLEND_MODE_COLOR; }
    if(name=="/Luminosity")                    { return BLEND_MODE_LUMINOSITY; }

    return BLEND_MODE_UNKNOWN;
  }

}

#endif
