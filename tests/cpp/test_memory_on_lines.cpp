// SPDX-License-Identifier: MIT
// Manual native memory experiment for issues #448 and #345 (macOS/Linux).
// Build: cmake -S . -B build -DDOCLING_PARSE_BUILD_MEMORY_PROBE=ON
//        cmake --build build --target test_memory_on_lines -j2
// Generate PDFs in a SEPARATE process so generation cannot affect peak RSS:
//   test_memory_on_lines generate dash-each 8000 /tmp/lines.pdf
//   test_memory_on_lines measure render /tmp/lines.pdf [pause_seconds] [out.png]
// Modes: qpdf-discard, collect (tokens is an alias), decode, render, skip-shapes.
// Raw QPDF modes select the generated Form's LEAF stream once; they do not run Do.
// Generate pathstorm-form N PDF [repeats=1] [depth=1] for Form fixtures.
// Each measurement needs a fresh process. RSS values are bytes; peak RSS is a
// process-lifetime high-water mark.
// Generation variants: solid, dash-each, dash-once, dash-q, empty-each,
// pathstorm (the grid generator from docling#4058, with its rounded line count).
// Generate pathstorm-appearance/pathstorm-widget N PDF [appearances=1].
// Generate pathstorm-type3 N PDF [glyphs=1] for procedural CharProc delivery.
// Generate pathstorm-soft-mask N PDF [paints=1] for one cached mask group.
// Generate pathstorm-pattern[-lattice] N PDF [fills=1] for pattern replay.
// Generate clip-chain/clip-scoped N PDF (10..4000) for clip snapshot growth.
// page_* fields cover /Contents only. form_* diagnostics aggregate Do calls
// through that page's ordinary Form tree; appearances have separate counters.
#include <parse.h>
#include <render.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>

#include <zlib.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>
#else
#error "This manual memory probe currently supports macOS and Linux."
#endif

namespace
{
using json = nlohmann::json;
using clock_type = std::chrono::steady_clock;
const auto started = clock_type::now();
constexpr const char* page_content_delivery = "callback";
constexpr const char* form_content_delivery = "callback";
constexpr const char* appearance_content_delivery = "callback";
constexpr const char* type3_content_delivery = "callback";
constexpr const char* soft_mask_content_delivery = "callback";
constexpr const char* pattern_content_delivery = "borrowed";
constexpr const char* clip_history_storage = "shared";

void snapshot(const std::string& stage, int pause_seconds, json details = json::object())
{
  rusage usage{};
  if(getrusage(RUSAGE_SELF, &usage)!=0)
    { throw std::runtime_error("getrusage failed"); }
#if defined(__APPLE__)
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if(task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
               reinterpret_cast<task_info_t>(&info), &count)!=KERN_SUCCESS)
    { throw std::runtime_error("task_info failed"); }
  details["rss_bytes"] = info.resident_size;
  details["peak_rss_bytes"] = usage.ru_maxrss;
#else
  std::ifstream statm("/proc/self/statm");
  std::size_t total_pages = 0, resident_pages = 0;
  if(not (statm >> total_pages >> resident_pages))
    { throw std::runtime_error("cannot read /proc/self/statm"); }
  details["rss_bytes"] = resident_pages * sysconf(_SC_PAGESIZE);
  details["peak_rss_bytes"] = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
  details["stage"] = stage;
  details["elapsed_s"] = std::chrono::duration<double>(clock_type::now()-started).count();
  std::cout << details.dump() << std::endl;
  if(pause_seconds>0) { std::this_thread::sleep_for(std::chrono::seconds(pause_seconds)); }
}

// Forward to the production collector only when requested. The discard case
// keeps scalar counters and no QPDF objects or per-object string representations.
class counting_callbacks : public QPDFObjectHandle::ParserCallbacks
{
public:
  explicit counting_callbacks(pdflib::qpdf_stream_decoder* collector_ = nullptr):
    collector(collector_) {}

  using QPDFObjectHandle::ParserCallbacks::handleObject;

  void handleObject(QPDFObjectHandle obj, std::size_t offset, std::size_t length) override
  {
    ++tokens;
    operators += obj.isOperator();
    if(collector) { collector->handleObject(obj, offset, length); }
  }

  void contentSize(std::size_t size) override
  {
    decompressed_bytes += size;
    ++content_size_calls;
  }

  void handleEOF() override { eof_reached = true; }

  json statistics() const
  {
    return {{"tokens", tokens}, {"operators", operators},
            {"decompressed_bytes", decompressed_bytes},
            {"content_size_calls", content_size_calls}, {"eof_reached", eof_reached}};
  }

private:
  pdflib::qpdf_stream_decoder* collector;
  std::size_t tokens = 0, operators = 0, decompressed_bytes = 0, content_size_calls = 0;
  bool eof_reached = false;
};

// Select only our marked generated Form chain or first appearance source.
// Arbitrary PDFs keep first-page /Contents; this is not a general Do/AP executor.
QPDFObjectHandle selected_content(QPDFObjectHandle page, json& details)
{
  auto content = page.getKey("/Contents");
  details["measured_stream"] = "page";
  details["stream_executions"] = 1;
  auto pattern_fills = page.getKey("/MemoryProbePatternFills");
  if(not pattern_fills.isNull())
    {
      if(not pattern_fills.isInteger() or pattern_fills.getIntValue()<1)
        { throw std::runtime_error("invalid generated pattern fill count"); }
      auto cell = page.getKey("/Resources").getKey("/Pattern").getKey("/P0");
      if(not cell.isStream())
        { throw std::runtime_error("generated pattern cell is missing"); }
      details["measured_stream"] = "pattern_cell";
      details["page_pattern_fills"] = pattern_fills.getIntValue();
      return cell;
    }
  auto mask_paints = page.getKey("/MemoryProbeSoftMaskPaints");
  if(not mask_paints.isNull())
    {
      if(not mask_paints.isInteger() or mask_paints.getIntValue()<1)
        { throw std::runtime_error("invalid generated soft-mask paint count"); }
      auto group = page.getKey("/Resources").getKey("/ExtGState")
        .getKey("/GS").getKey("/SMask").getKey("/G");
      if(not group.isStream())
        { throw std::runtime_error("generated soft-mask group is missing"); }
      details["measured_stream"] = "soft_mask_group";
      details["page_soft_mask_paints"] = mask_paints.getIntValue();
      return group;
    }
  auto type3_repeats = page.getKey("/MemoryProbeType3Repeats");
  if(not type3_repeats.isNull())
    {
      if(not type3_repeats.isInteger() or type3_repeats.getIntValue()<1)
        { throw std::runtime_error("invalid generated Type3 repeat count"); }
      auto font = page.getKey("/Resources").getKey("/Font").getKey("/T3");
      auto charproc = font.getKey("/CharProcs").getKey("/g");
      if(not charproc.isStream())
        { throw std::runtime_error("generated CharProc is missing"); }
      details["measured_stream"] = "type3_charproc";
      details["page_type3_repeats"] = type3_repeats.getIntValue();
      return charproc;
    }
  auto appearance_count = page.getKey("/MemoryProbeAppearanceCount");
  if(not appearance_count.isNull())
    {
      if(not appearance_count.isInteger() or appearance_count.getIntValue()<1)
        { throw std::runtime_error("invalid generated appearance count"); }
      auto annots = page.getKey("/Annots");
      if(not annots.isArray() or annots.getArrayNItems()!=appearance_count.getIntValue())
        { throw std::runtime_error("generated annotation count differs"); }
      auto appearance = annots.getArrayItem(0).getKey("/AP").getKey("/N");
      if(not appearance.isStream())
        { throw std::runtime_error("generated appearance source is missing"); }
      details["measured_stream"] = "appearance";
      details["page_appearance_count"] = appearance_count.getIntValue();
      return appearance;
    }
  auto depth_obj = page.getKey("/MemoryProbeFormDepth");
  if(depth_obj.isNull()) { return content; }
  if(not depth_obj.isInteger())
    { throw std::runtime_error("invalid generated Form depth"); }
  const auto depth = depth_obj.getIntValue();
  if(depth<1 or depth>32)
    { throw std::runtime_error("generated Form depth must be 1..32"); }
  details["measured_stream"] = "form_leaf";
  details["form_depth"] = depth;
  auto repeats = page.getKey("/MemoryProbeFormRepeats");
  if(repeats.isInteger()) { details["page_form_repeats"] = repeats.getIntValue(); }

  auto owner = page;
  for(long long level=0; level<depth; ++level)
    {
      auto dict = owner.isStream() ? owner.getDict() : owner;
      auto resources = dict.getKey("/Resources");
      if(not resources.isDictionary())
        { throw std::runtime_error("generated Form resources are missing"); }
      auto xobjects = resources.getKey("/XObject");
      if(not xobjects.isDictionary())
        { throw std::runtime_error("generated Form XObject dictionary is missing"); }
      owner = xobjects.getKey("/Fm0");
      if(not owner.isStream())
        { throw std::runtime_error("generated Form /Fm0 stream is missing"); }
    }
  return owner;
}

std::size_t positive_count(const std::string& text)
{
  if(text.empty() or text.find_first_not_of("0123456789")!=std::string::npos)
    { throw std::invalid_argument("counts must be positive decimal integers"); }
  const auto value = std::stoull(text);
  if(value==0 or value>std::numeric_limits<std::size_t>::max())
    { throw std::invalid_argument("count is out of range"); }
  return static_cast<std::size_t>(value);
}

std::string stream_object(const std::string& content, bool compress,
                          const std::string& extra = "")
{
  std::string payload = content;
  if(compress)
    {
      uLongf size = compressBound(content.size());
      payload.resize(size);
      if(compress2(reinterpret_cast<Bytef*>(payload.data()), &size,
                   reinterpret_cast<const Bytef*>(content.data()),
                   content.size(), Z_DEFAULT_COMPRESSION)!=Z_OK)
        { throw std::runtime_error("PDF compression failed"); }
      payload.resize(size);
    }
  return "<< /Length " + std::to_string(payload.size()) + extra +
    (compress ? " /Filter /FlateDecode" : "") + " >>\nstream\n" + payload + "\nendstream";
}

void generate(const std::string& variant, std::size_t requested,
              const std::string& filename, std::size_t repeats, std::size_t depth)
{
  if(requested<10 or requested>static_cast<std::size_t>(std::numeric_limits<int>::max()))
    { throw std::invalid_argument("line count must be 10..INT_MAX"); }
  const bool form = variant=="pathstorm-form";
  const bool widget = variant=="pathstorm-widget";
  const bool appearance = variant=="pathstorm-appearance" or widget;
  const bool type3 = variant=="pathstorm-type3";
  const bool soft_mask = variant=="pathstorm-soft-mask";
  const bool lattice = variant=="pathstorm-pattern-lattice";
  const bool pattern = variant=="pathstorm-pattern" or lattice;
  const bool clip_chain = variant=="clip-chain";
  const bool clip_scoped = variant=="clip-scoped";
  const bool clipping = clip_chain or clip_scoped;
  if(clipping and requested>4000)
    { throw std::invalid_argument("clip fixtures require at most 4000 lines"); }
  if(depth<1 or depth>32)
    { throw std::invalid_argument("Form depth must be 1..32"); }
  if((not form and not appearance and not type3 and not soft_mask and not pattern and repeats!=1) or (not form and depth!=1))
    { throw std::invalid_argument("repeats require Forms/appearances/Type3/masks/patterns; depth requires Forms"); }
  if(variant!="solid" and variant!="dash-each" and variant!="dash-once" and
     variant!="dash-q" and variant!="empty-each" and variant!="pathstorm" and not form and not appearance and not type3 and not soft_mask and not pattern and not clipping)
    { throw std::invalid_argument("unknown PDF variant"); }

  std::ostringstream ops;
  std::size_t lines = 0;
  const bool grid = variant=="pathstorm" or form or appearance or type3 or soft_mask or pattern;
  if(grid)
    {
      // Match the published generator, including the rounding of each grid.
      const int w = 595, h = 842;
      const int rows = static_cast<int>(std::sqrt(requested/2.0) * std::sqrt(double(h)/w));
      const int per_row = (requested/2)/rows;
      const double dash = double(w)/per_row/1.6;
      ops << "0.4 w\n" << std::fixed << std::setprecision(1);
      for(int r=0; r<rows; ++r)
        for(int c=0; c<per_row; ++c)
          {
            double y = 20.0 + (h-40.0)*r/rows;
            double x = 15.0 + (w-30.0)*c/per_row;
            ops << x << " " << y << " m " << x+dash << " " << y << " l S\n";
            ++lines;
          }
      const int cols = rows*w/h;
      const int per_col = (requested/2)/cols;
      const double dash_v = double(h)/per_col/1.6;
      for(int c=0; c<cols; ++c)
        for(int r=0; r<per_col; ++r)
          {
            double x = 20.0 + (w-40.0)*c/cols;
            double y = 15.0 + (h-30.0)*r/per_col;
            ops << x << " " << y << " m " << x << " " << y+dash_v << " l S\n";
            ++lines;
          }
    }
  else
    {
      // Match #448's coordinates, pattern and independent stroke operators.
      for(std::size_t i=0; i<requested; ++i)
        {
          if(variant=="dash-q" or clip_scoped) { ops << "q\n"; }
          if(clipping) { ops << "0 0 1190 842 re W n\n"; }
          if(variant=="dash-each" or variant=="dash-q" or (variant=="dash-once" and i==0))
            { ops << "[18 2 3 2] 0 d\n"; }
          if(variant=="empty-each") { ops << "[] 0 d\n"; }
          auto x = i%1000, y = (i/10)%800;
          ops << x << " " << y << " m " << x+1 << " " << y << " l S\n";
          if(variant=="dash-q" or clip_scoped) { ops << "Q\n"; }
          ++lines;
        }
    }

  // Strokes deliberately take the procedural fallback: the cached vector-mask
  // analyzer does not rasterize S. Its first-use analysis still affects RSS.
  const std::string content = (type3 ? "595 0 d0\n" : "") + ops.str();
  if(lines>std::numeric_limits<std::size_t>::max()/repeats)
    { throw std::overflow_error("total painted line count is out of range"); }
  const std::string bbox = grid ? "595 842" : "1190 842";
  std::string resources = "<< >>", marker;
  std::string catalog_fields;
  std::string page_content = content;
  if(form)
    {
      resources = "<< /XObject << /Fm0 5 0 R >> >>";
      marker = " /MemoryProbeFormDepth " + std::to_string(depth) +
               " /MemoryProbeFormRepeats " + std::to_string(repeats);
      page_content.clear();
      for(std::size_t i=0; i<repeats; ++i) { page_content += "q /Fm0 Do Q\n"; }
    }
  if(appearance)
    {
      page_content.clear();
      std::string refs;
      for(std::size_t i=0; i<repeats; ++i)
        { refs += std::to_string(6+i) + " 0 R "; }
      marker = " /MemoryProbeAppearanceCount " + std::to_string(repeats) +
               " /Annots [" + refs + "]";
      if(widget) { catalog_fields = " /AcroForm << /Fields [" + refs + "] >>"; }
    }
  if(type3)
    {
      resources = "<< /Font << /T3 5 0 R >> >>";
      marker = " /MemoryProbeType3Repeats " + std::to_string(repeats);
      page_content = "BT /T3 1 Tf\n";
      for(std::size_t i=0; i<repeats; ++i)
        { page_content += "1 0 0 1 0 0 Tm (a) Tj\n"; }
      page_content += "ET\n";
    }
  if(soft_mask)
    {
      resources = "<< /ExtGState << /GS << /SMask << /S /Alpha /G 5 0 R >> >> >> >>";
      marker = " /MemoryProbeSoftMaskPaints " + std::to_string(repeats);
      page_content.clear();
      // Every gs installs the same entry at the same CTM. Only one mask group
      // should be decoded; its N strokes cover each of these black rectangles.
      for(std::size_t i=0; i<repeats; ++i)
        { page_content += "/GS gs 0 0 595 842 re f\n"; }
    }
  if(pattern)
    {
      resources = "<< /Pattern << /P0 5 0 R >> >>";
      marker = " /MemoryProbePatternFills " + std::to_string(repeats);
      page_content = "/Pattern cs /P0 scn\n";
      for(std::size_t i=0; i<repeats; ++i)
        { page_content += "0 0 595 842 re f\n"; }
    }
  std::vector<std::string> objects = {
    "<< /Type /Catalog /Pages 2 0 R" + catalog_fields + " >>",
    "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + bbox +
      "] /Resources " + resources + marker + " /Contents 4 0 R >>",
    stream_object(page_content, grid)
  };
  if(form)
    {
      for(std::size_t level=0; level<depth; ++level)
        {
          const bool leaf = level+1==depth;
          const std::string local_resources = leaf ? "<< >>" :
            "<< /XObject << /Fm0 " + std::to_string(6+level) + " 0 R >> >>";
          const std::string extra = " /Type /XObject /Subtype /Form /FormType 1"
            " /BBox [0 0 " + bbox + "] /Matrix [1 0 0 1 0 0] /Resources " + local_resources;
          objects.push_back(stream_object(leaf ? content : "q /Fm0 Do Q\n", true, extra));
        }
    }
  if(appearance)
    {
      objects.push_back(stream_object(content, true,
        " /Type /XObject /Subtype /Form /BBox [0 0 " + bbox +
        "] /Matrix [1 0 0 1 0 0] /Resources << >>"));
      for(std::size_t i=0; i<repeats; ++i)
        {
          objects.push_back("<< /Type /Annot /Subtype " + std::string(widget ? "/Widget" : "/Stamp") +
            " /Rect [0 0 " + bbox + "] /F 4 /AP << /N 5 0 R >>" +
            (widget ? " /FT /Btn /T (probe" + std::to_string(i) + ")" : "") + " >>");
        }
    }
  if(type3)
    {
      objects.push_back("<< /Type /Font /Subtype /Type3 /FontBBox [0 0 " + bbox +
        "] /FontMatrix [1 0 0 1 0 0] /CharProcs 6 0 R"
        " /Encoding << /Type /Encoding /Differences [97 /g] >>"
        " /FirstChar 97 /LastChar 97 /Widths [595] /Resources << >> >>");
      objects.push_back("<< /g 7 0 R >>");
      objects.push_back(stream_object(content, true));
    }
  if(soft_mask)
    {
      objects.push_back(stream_object(content, true,
        " /Type /XObject /Subtype /Form /BBox [0 0 " + bbox +
        "] /Resources << >> /Group << /S /Transparency /CS /DeviceGray >>"));
    }
  if(pattern)
    {
      // A page-sized step uses the decoded-tile cache. Doubling both steps
      // selects lattice replay at indices [0,1] x [0,1]: four executions.
      const std::string pattern_box = lattice ? "1190 1684" : "595 842";
      objects.push_back(stream_object(content, true,
        " /Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1"
        " /BBox [0 0 " + pattern_box + "] /XStep " + (lattice ? "1190" : "595") +
        " /YStep " + (lattice ? "1684" : "842") + " /Resources << >>"));
    }
  std::ofstream out(filename, std::ios::binary);
  out.exceptions(std::ios::failbit | std::ios::badbit);
  out << "%PDF-1.4\n";
  std::vector<std::streamoff> offsets;
  for(std::size_t i=0; i<objects.size(); ++i)
    {
      offsets.push_back(out.tellp());
      out << i+1 << " 0 obj\n" << objects[i] << "\nendobj\n";
    }
  const auto xref = out.tellp();
  out << "xref\n0 " << objects.size()+1 << "\n0000000000 65535 f \n";
  for(auto offset : offsets) { out << std::setw(10) << std::setfill('0') << offset << " 00000 n \n"; }
  out << "trailer\n<< /Size " << objects.size()+1 << " /Root 1 0 R >>\nstartxref\n" << xref << "\n%%EOF\n";
  out.close();
  std::cout << json{{"variant", variant}, {"requested_lines", requested},
                   {"unique_lines", lines}, {"lines", lines*repeats},
                   {"form_repeats", form ? repeats : 1}, {"form_depth", form ? depth : 0},
                   {"appearance_count", appearance ? repeats : 0},
                   {"widget_count", widget ? repeats : 0},
                   {"type3_repeats", type3 ? repeats : 0},
                   {"soft_mask_paints", soft_mask ? repeats : 0},
                   {"pattern_fills", pattern ? repeats : 0},
                   {"pattern_lattice_cells_per_fill", lattice ? 4 : 0},
                   {"clip_mode", clipping ? (clip_chain ? "chain" : "scoped") : "none"},
                   {"page_content_bytes", page_content.size()},
                   {"content_bytes", content.size()}, {"pdf_bytes", std::filesystem::file_size(filename)}}.dump()
            << std::endl;
}

void measure(std::string mode, std::string filename, int pause_seconds, const std::string& png)
{
  const std::string requested_mode = mode;
  if(mode=="tokens") { mode = "collect"; }
  if(mode!="qpdf-discard" and mode!="collect" and mode!="decode" and
     mode!="render" and mode!="skip-shapes")
    { throw std::invalid_argument("unknown measurement mode"); }
  snapshot("baseline", pause_seconds,
           {{"mode", mode}, {"requested_mode", requested_mode}, {"file", filename},
            {"qpdf_version", QPDF::QPDFVersion()},
            {"page_content_delivery", page_content_delivery},
            {"form_content_delivery", form_content_delivery},
            {"appearance_content_delivery", appearance_content_delivery},
            {"type3_content_delivery", type3_content_delivery},
            {"soft_mask_content_delivery", soft_mask_content_delivery},
            {"pattern_content_delivery", pattern_content_delivery},
            {"clip_history_storage", clip_history_storage},
            {"sizeof_token", sizeof(pdflib::qpdf_stream_instruction)},
            {"sizeof_page_shape", sizeof(pdflib::page_item<pdflib::PAGE_SHAPE>)},
            {"sizeof_shape_instruction", sizeof(pdflib::shape_instruction)},
            {"sizeof_subpath", sizeof(pdflib::shape_subpath)}});
  if(mode=="qpdf-discard" or mode=="collect")
    {
      QPDF pdf;
      pdf.processFile(filename.c_str());
      auto pages = pdf.getAllPages();
      json selected = json::object();
      auto content = selected_content(pages.at(0), selected);
      snapshot("loaded", pause_seconds, selected);
      if(mode=="qpdf-discard")
        {
          counting_callbacks callbacks;
          QPDFObjectHandle::parseContentStream(content, &callbacks);
          auto details = callbacks.statistics();
          details.update(selected);
          details["collector"] = "count_and_discard";
          details["retained_tokens"] = 0;
          details["token_capacity"] = 0;
          details["token_inline_bytes"] = 0;
          snapshot("tokenized", pause_seconds, details);
        }
      else
        {
          std::vector<pdflib::qpdf_stream_instruction> tokens;
          pdflib::qpdf_stream_decoder collector(tokens);
          counting_callbacks callbacks(&collector);
          // Use the same QPDF entry point as discard. A raw-token comparison
          // fails on parse errors; production damaged-stream recovery is unchanged.
          QPDFObjectHandle::parseContentStream(content, &callbacks);
          auto details = callbacks.statistics();
          details.update(selected);
          details["collector"] = "docling_collect";
          details["retained_tokens"] = tokens.size();
          details["token_capacity"] = tokens.capacity();
          details["token_inline_bytes"] = tokens.capacity()*sizeof(tokens[0]);
          snapshot("tokenized", pause_seconds, details);
        }
      return;
    }

  json font_config = {{pdflib::pdf_resource<pdflib::PAGE_FONT>::RESOURCE_DIR_KEY,
                        resource_utils::get_resources_dir(true).string()}};
  std::unordered_map<std::string, double> font_timings;
  pdflib::pdf_resource<pdflib::PAGE_FONT>::initialise(font_config, font_timings);
  pdflib::pdf_decoder<pdflib::DOCUMENT> document;
  std::optional<std::string> password;
  if(not document.process_document_from_file(filename, password))
    { throw std::runtime_error(document.get_load_error_message()); }
  snapshot("loaded", pause_seconds);
  pdflib::decode_config config;
  config.do_thread_safe = false;
  config.keep_shapes = mode!="skip-shapes";
  // Preserve other defaults, including sanitization. There are no text/images.
  const auto decode_started = clock_type::now();
  auto page = document.decode_page(0, config);
  const double decode_work_s = std::chrono::duration<double>(clock_type::now()-decode_started).count();
  if(not page) { throw std::runtime_error("page decode failed"); }
  const auto& content_stats = page->get_content_statistics();
  const auto& appearance_stats = page->get_appearance_statistics();
  const auto& soft_mask_stats = page->get_soft_mask_statistics();
  const auto& pattern_stats = page->get_pattern_statistics();
  const auto& shapes = page->get_instructions().get_shape_instructions();
  std::size_t dash_entries = 0, max_dash = 0, points = 0, subpaths = 0, segments = 0;
  std::size_t geometry_bytes = 0, page_dash_entries = 0;
  std::size_t clip_paths = 0, max_clip_paths = 0, clip_points = 0, clip_bytes = 0;
  // Physical accounting deduplicates history nodes and their geometry; it
  // excludes allocator overhead and shared_ptr control blocks. Logical totals
  // still count each path occurrence visible to each instruction.
  std::size_t logical_clip_bytes = 0, unique_clip_paths = 0, unique_clip_points = 0;
  std::unordered_set<const void*> clip_nodes;
  for(const auto& shape : shapes)
    {
      dash_entries += shape.get_dash_array().size();
      max_dash = std::max(max_dash, shape.get_dash_array().size());
      const auto& clips = shape.get_clip_state().get_paths();
      clip_paths += clips.size();
      max_clip_paths = std::max(max_clip_paths, clips.size());
      clip_points += clips.point_count();
      logical_clip_bytes += clips.logical_bytes();
      clips.visit_storage([&](const void* identity, std::size_t path_count,
                              std::size_t point_count, std::size_t storage_bytes)
        {
          if(not clip_nodes.insert(identity).second) { return false; }
          clip_bytes += storage_bytes;
          unique_clip_paths += path_count;
          unique_clip_points += point_count;
          return true;
        });
      points += shape.size();
      subpaths += shape.get_subpaths().size();
      geometry_bytes += shape.get_subpaths().capacity()*sizeof(pdflib::shape_subpath);
      for(const auto& path : shape.get_subpaths())
        {
          segments += path.get_ops().size();
          geometry_bytes += path.get_ops().capacity()*sizeof(pdflib::shape_segment_op) +
                            (path.get_px().capacity()+path.get_py().capacity())*sizeof(double);
        }
    }
  for(const auto& shape : page->get_page_shapes()) { page_dash_entries += shape.get_dash_array().size(); }
  snapshot("decoded", pause_seconds,
           {{"decode_work_s", decode_work_s},
            {"page_shapes", page->get_page_shapes().size()}, {"shape_instructions", shapes.size()},
            {"instruction_capacity", shapes.capacity()}, {"subpaths", subpaths},
            {"points", points}, {"segments", segments},
            {"instruction_inline_bytes", shapes.capacity()*sizeof(pdflib::shape_instruction)},
            {"instruction_geometry_bytes", geometry_bytes},
            {"instruction_dash_entries", dash_entries}, {"max_dash_entries", max_dash},
            {"page_shape_dash_entries", page_dash_entries},
            {"stored_dash_bytes", (dash_entries+page_dash_entries)*sizeof(double)},
            {"instruction_clip_paths", clip_paths}, {"max_clip_paths", max_clip_paths},
            {"instruction_clip_points", clip_points}, {"instruction_clip_bytes", clip_bytes},
            {"clip_history_storage", clip_history_storage},
            {"instruction_clip_logical_bytes", logical_clip_bytes},
            {"instruction_clip_history_nodes", clip_nodes.size()},
            {"instruction_clip_unique_paths", unique_clip_paths},
            {"instruction_clip_unique_points", unique_clip_points},
            {"page_content_delivery", content_stats.callback_delivery ? "callback" : "buffered"},
            {"page_source_objects", content_stats.source_objects},
            {"page_source_bytes_observed", content_stats.source_bytes_observed},
            {"page_buffered_tokens", content_stats.buffered_tokens},
            {"page_buffered_token_capacity", content_stats.buffered_token_capacity},
            {"page_operand_diagnostics", content_stats.operand_diagnostics_enabled},
            {"page_max_pending_operands", content_stats.max_pending_operands},
            {"form_content_delivery", form_content_delivery},
            {"form_diagnostics", content_stats.operand_diagnostics_enabled},
            {"form_executions", content_stats.form_executions},
            {"form_source_objects", content_stats.form_source_objects},
            {"form_materialized_tokens", content_stats.form_materialized_tokens},
            {"form_max_pending_operands", content_stats.form_max_pending_operands},
            {"form_max_depth", content_stats.form_max_depth},
            {"appearance_content_delivery", appearance_content_delivery},
            {"appearance_diagnostics", content_stats.operand_diagnostics_enabled},
            {"appearance_executions", appearance_stats.executions},
            {"appearance_widget_executions", appearance_stats.widget_executions},
            {"appearance_source_objects", appearance_stats.source_objects},
            {"appearance_source_bytes_observed", appearance_stats.source_bytes_observed},
            {"appearance_materialized_tokens", appearance_stats.materialized_tokens},
            {"appearance_max_buffered_token_capacity", appearance_stats.max_buffered_token_capacity},
            {"appearance_max_pending_operands", appearance_stats.max_pending_operands},
            {"text_instructions", page->get_instructions().get_text_instructions().size()},
            {"type3_content_delivery", type3_content_delivery},
            {"type3_diagnostics", content_stats.operand_diagnostics_enabled},
            {"type3_executions", content_stats.type3_executions},
            {"type3_source_objects", content_stats.type3_source_objects},
            {"type3_materialized_tokens", content_stats.type3_materialized_tokens},
            {"type3_max_pending_operands", content_stats.type3_max_pending_operands},
            {"type3_max_depth", content_stats.type3_max_depth},
            {"type3_recursion_skips", content_stats.type3_recursion_skips},
            {"soft_mask_content_delivery", soft_mask_content_delivery},
            {"soft_mask_diagnostics", content_stats.operand_diagnostics_enabled},
            {"soft_mask_executions", soft_mask_stats.executions},
            {"soft_mask_cache_hits", soft_mask_stats.cache_hits},
            {"soft_mask_source_objects", soft_mask_stats.source_objects},
            {"soft_mask_source_bytes_observed", soft_mask_stats.source_bytes_observed},
            {"soft_mask_materialized_tokens", soft_mask_stats.materialized_tokens},
            {"soft_mask_max_buffered_token_capacity", soft_mask_stats.max_buffered_token_capacity},
            {"soft_mask_max_pending_operands", soft_mask_stats.max_pending_operands},
            {"soft_mask_shape_instructions", soft_mask_stats.shape_instructions},
            {"pattern_content_delivery", pattern_content_delivery},
            {"pattern_diagnostics", content_stats.operand_diagnostics_enabled},
            {"pattern_source_parses", pattern_stats.source_parses},
            {"pattern_source_cache_hits", pattern_stats.source_cache_hits},
            {"pattern_cached_tokens", pattern_stats.cached_tokens},
            {"pattern_cached_token_capacity", pattern_stats.cached_token_capacity},
            {"pattern_decoded_cells", pattern_stats.decoded_cells},
            {"pattern_decoded_cache_hits", pattern_stats.decoded_cache_hits},
            {"pattern_decoded_shape_instructions", pattern_stats.decoded_shape_instructions},
            {"pattern_lattice_executions", pattern_stats.lattice_executions},
            {"pattern_replay_source_objects", pattern_stats.replay_source_objects},
            {"pattern_replay_materialized_tokens", pattern_stats.replay_materialized_tokens},
            {"pattern_max_replay_token_capacity", pattern_stats.max_replay_token_capacity},
            {"pattern_max_pending_operands", pattern_stats.max_pending_operands}});
  if(mode=="render")
    {
      const auto render_started = clock_type::now();
      pdflib::render_config render_config;
      render_config.scale = 1.0;
      pdflib::renderer<pdflib::BLEND2D> renderer(render_config);
      page->get_instructions().iterate_over_instructions(renderer);
      // get_canvas flushes pending work and retrieves an independent pixel buffer.
      const auto canvas = renderer.get_canvas();
      const double render_work_s = std::chrono::duration<double>(clock_type::now()-render_started).count();
      snapshot("rendered", pause_seconds, {{"render_work_s", render_work_s}});
      if(not png.empty()) { renderer.save(png); }
    }
}
}

int main(int argc, char** argv)
{
  try
    {
      loguru::g_stderr_verbosity = loguru::Verbosity_ERROR;
      if(argc>=5 and argc<=7 and std::string(argv[1])=="generate")
        {
          const auto count = positive_count(argv[3]);
          const auto repeats = argc>=6 ? positive_count(argv[5]) : 1;
          const auto depth = argc>=7 ? positive_count(argv[6]) : 1;
          if(argc>6 and std::string(argv[2])!="pathstorm-form")
            { throw std::invalid_argument("depth argument requires pathstorm-form"); }
          generate(argv[2], count, argv[4], repeats, depth);
        }
      else if(argc>=4 and argc<=6 and std::string(argv[1])=="measure")
        {
          const std::string pause_text = argc>=5 ? argv[4] : "0";
          if(pause_text.empty() or pause_text.find_first_not_of("0123456789")!=std::string::npos)
            { throw std::invalid_argument("pause must be 0..60 seconds"); }
          const int pause = std::stoi(pause_text);
          if(pause<0 or pause>60) { throw std::invalid_argument("pause must be 0..60 seconds"); }
          measure(argv[2], argv[3], pause, argc==6 ? argv[5] : "");
        }
      else
        {
          std::cerr << "Usage: " << argv[0] << " generate VARIANT N PDF [repeats] [form_depth]\n"
                    << "       " << argv[0] << " measure {qpdf-discard|collect|tokens|decode|render|skip-shapes} PDF [pause_seconds] [out.png]\n";
          return 2;
        }
    }
  catch(const std::exception& e)
    {
      std::cerr << e.what() << "\n";
      return 1;
    }
  return 0;
}
