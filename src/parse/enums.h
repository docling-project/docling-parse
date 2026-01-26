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

  enum xobject_subtype_name {
    XOBJECT_UNKNOWN,

    XOBJECT_FORM,
    XOBJECT_IMAGE
  };


}

#endif
