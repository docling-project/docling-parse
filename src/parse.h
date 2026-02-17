//-*-C++-*-

// std libraries
#include <set>
#include <map>
#include <mutex>
#include <iomanip>
#include <vector>
#include <assert.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <memory>
#include <regex>
#include <fstream>
#include <iostream>
#include <cmath>

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
#include <qpdf/Buffer.hh>

// code to locate pdf-resources (eg fonts)
#include <resources.h>

// specifics of parser
#include <parse/enums.h>
#include <parse/config.h>

#include <parse/utils.h>
#include <parse/utils/pdf_timings.h>

#include <parse/qpdf/to_json.h>
#include <parse/qpdf/annots.h>
#include <parse/qpdf/instruction.h>
#include <parse/qpdf/stream_decoder.h>

// page-item
#include <parse/page_item.h>
#include <parse/page_items/page_dimension.h>
#include <parse/page_items/page_cell.h>
#include <parse/page_items/page_cells.h>
#include <parse/page_items/page_shape.h>
#include <parse/page_items/page_shapes.h>
#include <parse/page_items/page_image.h>
#include <parse/page_items/page_images.h>
#include <parse/page_items/page_widget.h>
#include <parse/page_items/page_widgets.h>
#include <parse/page_items/page_hyperlink.h>
#include <parse/page_items/page_hyperlinks.h>

// pdf-resource
#include <parse/pdf_resource.h>
#include <parse/pdf_resources/page_font/glyphs.h>
#include <parse/pdf_resources/page_font/font_cid.h>
#include <parse/pdf_resources/page_font/font_cids.h>
#include <parse/pdf_resources/page_font/encoding.h>
#include <parse/pdf_resources/page_font/encodings.h>
#include <parse/pdf_resources/page_font/base_font.h>
#include <parse/pdf_resources/page_font/base_fonts.h>
#include <parse/pdf_resources/page_font/cmap_value.h>
#include <parse/pdf_resources/page_font/cmap.h>
#include <parse/pdf_resources/page_font/char_description.h>
#include <parse/pdf_resources/page_font/char_processor.h>

#include <parse/pdf_resources/page_font.h>
#include <parse/pdf_resources/page_fonts.h>

#include <parse/pdf_resources/page_grph.h>
#include <parse/pdf_resources/page_grphs.h>

#include <parse/pdf_resources/page_xobject_image.h>
#include <parse/pdf_resources/page_xobject_form.h>
#include <parse/pdf_resources/page_xobject_postscript.h>
#include <parse/pdf_resources/page_xobjects.h>

//  
#include <parse/pdf_sanitator.h>
#include <parse/page_item_sanitator.h>
#include <parse/pdf_sanitators/constants.h>
#include <parse/page_item_sanitators/shapes.h>
#include <parse/page_item_sanitators/cells.h>
#include <parse/page_item_sanitators/page_dimension.h>

#include <parse/pdf_state.h>
#include <parse/pdf_states/grph.h>
#include <parse/pdf_states/text.h>
#include <parse/pdf_states/shape.h>
#include <parse/pdf_states/bitmap.h>
#include <parse/pdf_states/global.h>

#include <parse/pdf_decoder.h>
#include <parse/pdf_decoders/stream_enums.h>
#include <parse/pdf_decoders/stream.h>
#include <parse/pdf_decoders/page.h>
#include <parse/pdf_decoders/document.h>

#include <parse/parser.h>
