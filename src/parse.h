//-*-C++-*-

// std libraries 
#include <set>
#include <map>
#include <vector>
#include <assert.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <regex>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <share.h> // to define _SH_DENYNO for loguru
//#define _SH_DENYNO
#endif

// specific libraries
#include <cxxopts.hpp>

#define LOGURU_WITH_STREAMS 1
#include <loguru.hpp>

//#include <utf8/utf8.h>
#include <utf8.h>
#include <nlohmann/json.hpp>

#define POINTERHOLDER_TRANSITION 0 // eliminate warnings from QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

// code to locate pdf-resources (eg fonts)
#include <resources.h>

// specifics of parser
#include <parse/enums.h>
#include <parse/utils.h>

#include <parse/qpdf/to_json.h>
#include <parse/qpdf/annots.h>
#include <parse/qpdf/instruction.h>
#include <parse/qpdf/stream_decoder.h>

#include <parse/pdf_resource.h>
#include <parse/pdf_resources/page_dimension.h>

#include <parse/pdf_resources/page_font/glyphs.h>
#include <parse/pdf_resources/page_font/font_cid.h>
#include <parse/pdf_resources/page_font/font_cids.h>
#include <parse/pdf_resources/page_font/encoding.h>
#include <parse/pdf_resources/page_font/encodings.h>
#include <parse/pdf_resources/page_font/base_font.h>
#include <parse/pdf_resources/page_font/base_fonts.h>
#include <parse/pdf_resources/page_font/cmap.h>
#include <parse/pdf_resources/page_font/char_description.h>
#include <parse/pdf_resources/page_font/char_processor.h>

#include <parse/pdf_resources/page_font.h>
#include <parse/pdf_resources/page_fonts.h>

#include <parse/pdf_resources/page_grph.h>
#include <parse/pdf_resources/page_grphs.h>

#include <parse/pdf_resources/page_xobject.h>
#include <parse/pdf_resources/page_xobjects.h>

#include <parse/pdf_resources/page_cell.h>
#include <parse/pdf_resources/page_cells.h>

#include <parse/pdf_resources/page_line.h>
#include <parse/pdf_resources/page_lines.h>

#include <parse/pdf_resources/page_image.h>
#include <parse/pdf_resources/page_images.h>

#include <parse/pdf_sanitator.h>
#include <parse/pdf_sanitators/constants.h>
#include <parse/pdf_sanitators/lines.h>
#include <parse/pdf_sanitators/cells.h>
#include <parse/pdf_sanitators/page_dimension.h>

#include <parse/pdf_state.h>
#include <parse/pdf_states/text.h>
#include <parse/pdf_states/line.h>
#include <parse/pdf_states/grph.h>
#include <parse/pdf_states/global.h>

#include <parse/pdf_decoder.h>
#include <parse/pdf_decoders/stream_enums.h>
#include <parse/pdf_decoders/stream.h>
#include <parse/pdf_decoders/page.h>
#include <parse/pdf_decoders/document.h>

#include <parse/parser.h>
