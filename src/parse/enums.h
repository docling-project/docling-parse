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
    COLOR_SPACE_SEPARATION, // single tint (tint transform not evaluated)
    COLOR_SPACE_DEVICE_N,   // multiple tints (tint transform not evaluated)

    COLOR_SPACE_PATTERN
  };

}

#endif
