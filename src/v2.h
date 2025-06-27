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
#include <mutex>
#include <thread>

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

// specifics of v2 parser
#include <v2/enums.h>
#include <v2/utils.h>

#include <v2/qpdf/to_json.h>
#include <v2/qpdf/annots.h>
#include <v2/qpdf/instruction.h>
#include <v2/qpdf/stream_decoder.h>

#include <v2/pdf_resource.h>
#include <v2/pdf_resources/page_dimension.h>

#include <v2/pdf_resources/page_font/glyphs.h>
#include <v2/pdf_resources/page_font/font_cid.h>
#include <v2/pdf_resources/page_font/font_cids.h>
#include <v2/pdf_resources/page_font/encoding.h>
#include <v2/pdf_resources/page_font/encodings.h>
#include <v2/pdf_resources/page_font/base_font.h>
#include <v2/pdf_resources/page_font/base_fonts.h>
#include <v2/pdf_resources/page_font/cmap.h>
#include <v2/pdf_resources/page_font/char_description.h>
#include <v2/pdf_resources/page_font/char_processor.h>

#include <v2/pdf_resources/page_font.h>
#include <v2/pdf_resources/page_fonts.h>

#include <v2/pdf_resources/page_grph.h>
#include <v2/pdf_resources/page_grphs.h>

#include <v2/pdf_resources/page_xobject.h>
#include <v2/pdf_resources/page_xobjects.h>

#include <v2/pdf_resources/page_cell.h>
#include <v2/pdf_resources/page_cells.h>

#include <v2/pdf_resources/page_line.h>
#include <v2/pdf_resources/page_lines.h>

#include <v2/pdf_resources/page_image.h>
#include <v2/pdf_resources/page_images.h>

#include <v2/pdf_sanitator.h>
#include <v2/pdf_sanitators/constants.h>
#include <v2/pdf_sanitators/lines.h>
#include <v2/pdf_sanitators/cells.h>
#include <v2/pdf_sanitators/page_dimension.h>

#include <v2/pdf_state.h>
#include <v2/pdf_states/text.h>
#include <v2/pdf_states/line.h>
#include <v2/pdf_states/grph.h>
#include <v2/pdf_states/global.h>

#include <v2/pdf_decoder.h>
#include <v2/pdf_decoders/stream_enums.h>
#include <v2/pdf_decoders/stream.h>
#include <v2/pdf_decoders/page.h>
#include <v2/pdf_decoders/document.h>

#include <v2/parser.h>
