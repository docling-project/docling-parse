//-*-C++-*-

#ifndef QPDF_STREAM_FILTERS_H
#define QPDF_STREAM_FILTERS_H

// ---------------------------------------------------------------------------
// Undoing the transport filters that sit in front of an image codec.
//
// A stream's /Filter entry is a chain applied in order at encode time, so the
// decoders have to run in the same order to undo it (ISO 32000-1, 7.4). The
// image codecs -- /DCTDecode, /JPXDecode, /CCITTFaxDecode and /JBIG2Decode --
// turn the byte stream into samples and are therefore always last in the
// chain (7.4.1, Table 6); everything in front of them is transport encoding
// such as /ASCII85Decode or /FlateDecode.
//
// QPDF cannot invert /CCITTFaxDecode, /JPXDecode or /JBIG2Decode at all, and
// leaves /DCTDecode alone at its default decode level. It treats that as the
// *whole* chain being unfilterable, so `getStreamData()` throws and we are
// left with `getRawStreamData()` -- the fully encoded bytes. For
// `/Filter [/ASCII85Decode /CCITTFaxDecode]` those bytes are ASCII85 text,
// and feeding them to a CCITT decoder decodes noise.
//
// These helpers apply exactly the filters in front of the codec, using QPDF's
// own filter implementations, so the codec sees the bytes it was handed at
// encode time.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#ifndef LOGURU_WITH_STREAMS
#define LOGURU_WITH_STREAMS 1
#endif
#include <loguru.hpp>

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <parse/qpdf/qpdf_compat.h>

namespace pdflib
{
  namespace stream_filters
  {
    // True for the /Filter names whose output is image samples rather than a
    // byte stream. Abbreviations are the inline-image forms (Table 93).
    bool is_image_codec(std::string const& filter_name);

    // Position of the first image codec in the chain, i.e. the number of
    // transport filters in front of it. Returns filters.size() when the chain
    // is pure transport encoding.
    std::size_t image_codec_index(std::vector<std::string> const& filters);

    // Applies filters[0, count) to `data` with QPDF's filter implementations.
    // `decode_parms` is the stream's /DecodeParms entry (a null object when
    // absent). Returns nullptr when the chain cannot be undone.
    std::shared_ptr<Buffer> apply_filters(std::shared_ptr<Buffer>         data,
                                          std::vector<std::string> const& filters,
                                          std::size_t                     count,
                                          QPDFObjectHandle                decode_parms);

    inline bool is_image_codec(std::string const& filter_name)
    {
      return filter_name == "/DCTDecode"      or filter_name == "/DCT"
          or filter_name == "/JPXDecode"
          or filter_name == "/CCITTFaxDecode" or filter_name == "/CCF"
          or filter_name == "/JBIG2Decode";
    }

    inline std::size_t image_codec_index(std::vector<std::string> const& filters)
    {
      for(std::size_t i = 0; i < filters.size(); ++i)
        {
          if(is_image_codec(filters[i]))
            {
              return i;
            }
        }

      return filters.size();
    }

    namespace detail
    {
      // The /DecodeParms entry that belongs to filter `index`. `decode_parms`
      // is taken by value and must be an initialized handle -- a null object
      // when the stream has no /DecodeParms -- because QPDF's type predicates
      // are non-const in some releases and undefined on an empty handle.
      inline QPDFObjectHandle decode_parms_for(QPDFObjectHandle   decode_parms,
                                               std::size_t        index,
                                               std::string const& filter_name)
      {
        if(decode_parms.isArray())
          {
            if(index < static_cast<std::size_t>(decode_parms.getArrayNItems()))
              {
                QPDFObjectHandle item = decode_parms.getArrayItem(static_cast<int>(index));
                if(item.isDictionary())
                  {
                    return item;
                  }
              }

            return QPDFObjectHandle::newNull();
          }

        // A single /DecodeParms dictionary next to a /Filter array does not say
        // which filter it belongs to. Of the transport filters only
        // /FlateDecode and /LZWDecode take parameters (a /Predictor, Table 8),
        // so the dictionary can only be meant for one of those; QPDF rejects a
        // non-null /DecodeParms on any other filter and would then declare the
        // whole chain unfilterable.
        if(decode_parms.isDictionary()
           and (filter_name == "/FlateDecode" or filter_name == "/Fl"
                or filter_name == "/LZWDecode" or filter_name == "/LZW"))
          {
            return decode_parms;
          }

        return QPDFObjectHandle::newNull();
      }
    }

    inline std::shared_ptr<Buffer> apply_filters(std::shared_ptr<Buffer>         data,
                                                 std::vector<std::string> const& filters,
                                                 std::size_t                     count,
                                                 QPDFObjectHandle                decode_parms)
    {
      if(not data or count == 0)
        {
          return data;
        }

      if(count > filters.size())
        {
          LOG_S(WARNING) << "apply_filters: asked for " << count
                         << " filters but the chain has only " << filters.size();
          return nullptr;
        }

      QPDFObjectHandle filter_array = QPDFObjectHandle::newArray();
      QPDFObjectHandle parms_array  = QPDFObjectHandle::newArray();

      for(std::size_t i = 0; i < count; ++i)
        {
          filter_array.appendItem(QPDFObjectHandle::newName(filters[i]));
          parms_array.appendItem(detail::decode_parms_for(decode_parms, i, filters[i]));
        }

      try
        {
          // A scratch document keeps this off the document being parsed: the
          // stream exists only to hand QPDF the data together with the filters
          // that encode it, so that getStreamData() runs its decoders over it.
          QPDF scratch;
          scratch.emptyPDF();

          QPDFObjectHandle stream = QPDFObjectHandle::newStream(&scratch);
          stream.replaceStreamData(data, filter_array, parms_array);

          auto decoded = to_shared_ptr(stream.getStreamData());

          LOG_S(INFO) << "apply_filters: undid " << count << " transport filter(s), "
                      << data->getSize() << " -> "
                      << (decoded ? decoded->getSize() : 0) << " bytes";

          return decoded;
        }
      catch(std::exception const& e)
        {
          LOG_S(WARNING) << "apply_filters: failed to undo " << count
                         << " transport filter(s): " << e.what();
          return nullptr;
        }
    }

  }
}

#endif
