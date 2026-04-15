// Implementation of the public jbig2_decode() interface.
// Compiled as part of the pdfium_jbig2 static library, which is the only
// translation unit that depends on PDFium internal headers.

#include "third_party/pdfium_jbig2.h"

#include <span>
#include <vector>

#include <loguru.hpp>

#include "core/fxcodec/jbig2/JBig2_DocumentContext.h"
#include "core/fxcodec/jbig2/jbig2_decoder.h"
#include "core/fxcrt/span.h"

static const char* fxcodec_status_name(FXCODEC_STATUS s) {
    switch (s) {
        case FXCODEC_STATUS::kDecodeFinished:      return "kDecodeFinished";
        case FXCODEC_STATUS::kDecodeToBeContinued: return "kDecodeToBeContinued";
        case FXCODEC_STATUS::kError:               return "kError";
        default:                                    return "kUnknown";
    }
}

std::vector<uint8_t> jbig2_decode(
    std::span<const uint8_t> page_data,
    std::span<const uint8_t> globals_data,
    std::uint32_t width, std::uint32_t height)
{
    LOG_S(INFO) << "jbig2_decode: entry"
                << " width=" << width
                << " height=" << height
                << " page_size=" << page_data.size()
                << " globals_size=" << globals_data.size();

    if (width == 0 or height == 0 or page_data.empty()) {
        LOG_S(WARNING) << "jbig2_decode: rejected — zero width/height or empty page_data";
        return {};
    }

    const uint32_t pitch    = (width + 7u) / 8u;
    const std::size_t total = static_cast<std::size_t>(pitch) * height;

    LOG_S(INFO) << "jbig2_decode: pitch=" << pitch << " total_bytes=" << total;

    std::vector<uint8_t> dest(total, 0xff);

    // Create a fresh document context per decode — safe under threaded
    // page processing because no mutable state is shared across calls.
    JBig2_DocumentContext  doc_ctx;
    Jbig2Context           jbig2_ctx;

    pdfium::span<const uint8_t> src_span(page_data.data(), page_data.size());
    pdfium::span<const uint8_t> global_span;
    if (not globals_data.empty()) {
        global_span = pdfium::span<const uint8_t>(
            globals_data.data(), globals_data.size());
    }
    pdfium::span<uint8_t> dest_span(dest.data(), total);

    FXCODEC_STATUS status = Jbig2Decoder::StartDecode(
        &jbig2_ctx,   &doc_ctx,
        width,        height,
        src_span,     /*src_key=*/   0,
        global_span,  /*global_key=*/0,
        dest_span,    pitch,
        /*pPause=*/   nullptr,
        /*reject_large_regions_when_fuzzing=*/false);

    LOG_S(INFO) << "jbig2_decode: StartDecode returned " << fxcodec_status_name(status);

    int iteration = 0;
    while (status == FXCODEC_STATUS::kDecodeToBeContinued) {
        status = Jbig2Decoder::ContinueDecode(&jbig2_ctx, /*pPause=*/nullptr);
        ++iteration;
        LOG_S(INFO) << "jbig2_decode: ContinueDecode[" << iteration << "] returned "
                    << fxcodec_status_name(status);
    }

    if (status != FXCODEC_STATUS::kDecodeFinished) {
        LOG_S(WARNING) << "jbig2_decode: decode failed with status=" << fxcodec_status_name(status)
                       << " after " << iteration << " continuation(s)";
        return {};
    }

    LOG_S(INFO) << "jbig2_decode: success after " << iteration << " continuation(s)"
                << " total_bytes=" << total;
    return dest;
}
