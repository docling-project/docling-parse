message(STATUS "entering extlib_pdfium_jbig2.cmake")

set(PDFIUM_JBIG2_ROOT ${TOPLEVEL_PREFIX_PATH}/src/third_party/pdfium_jbig2)

add_library(pdfium_jbig2 STATIC
    # allocator stub (replaces Chromium PartitionAlloc with plain malloc/free)
    ${PDFIUM_JBIG2_ROOT}/core/fxcrt/fx_memory.cpp
    # fax G4 stub (FaxG4Decode only, no FaxDecoder/FaxEncoder)
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/fax/faxmodule.cpp
    # jbig2 decoder
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_ArithDecoder.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_ArithIntDecoder.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_BitStream.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_Context.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_DocumentContext.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_GrdProc.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_GrrdProc.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_HtrdProc.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_HuffmanDecoder.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_HuffmanTable.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_Image.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_PatternDict.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_PddProc.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_SddProc.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_Segment.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_SymbolDict.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/JBig2_TrdProc.cpp
    ${PDFIUM_JBIG2_ROOT}/core/fxcodec/jbig2/jbig2_decoder.cpp
    # public wrapper
    ${PDFIUM_JBIG2_ROOT}/jbig2_decode.cpp
)

# PDFium headers use root-relative includes (e.g. "core/fxcrt/span.h").
# The vendor root must be PRIVATE so PDFium internals never leak to consumers.
# loguru is added PRIVATE so jbig2_decode.cpp can use LOG_S().
target_include_directories(pdfium_jbig2
    PRIVATE
        ${PDFIUM_JBIG2_ROOT}
        $<TARGET_PROPERTY:loguru,INTERFACE_INCLUDE_DIRECTORIES>
)

set_property(TARGET pdfium_jbig2 PROPERTY CXX_STANDARD 20)
set_target_properties(pdfium_jbig2 PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Suppress warnings from vendored upstream code.
target_compile_options(pdfium_jbig2 PRIVATE -w)
