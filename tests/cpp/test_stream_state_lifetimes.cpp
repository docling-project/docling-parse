// SPDX-License-Identifier: MIT
// Deterministic native exception/recovery checks; no actual memory exhaustion.
// Fault checkpoints exist only in this dedicated target, never in the extension
// or memory probe. Assertions remain active in Release builds.
#include <parse.h>

#include <cmath>
#include <new>
#include <stdexcept>

namespace pdflib
{
struct stream_state_lifetime_test_access
{
  using decoder = pdf_decoder<STREAM>;
  enum class failure { recoverable, allocation, nonstandard };
  struct failure_marker {};
  inline static decoder* root = nullptr;
  inline static failure kind = failure::recoverable;
  inline static bool fired = false;

  static void throw_failure()
  {
    fired = true;
    switch(kind)
      {
      case failure::recoverable: throw std::runtime_error("nested failure marker");
      case failure::allocation: throw std::bad_alloc();
      case failure::nonstandard: throw failure_marker{};
      }
  }

  static void child_checkpoint(decoder& stream)
  {
    if(&stream==root) { return; }
    decoder::interpretation_test_hook = nullptr;
    throw_failure();
  }

  static void parse_checkpoint(std::vector<qpdf_stream_instruction>& partial)
  {
    pdf_resource<PAGE_PATTERN>::parse_test_hook = nullptr;
    auto token = QPDFObjectHandle::newInteger(777);
    partial.emplace_back(token);
    throw_failure();
  }

  static void inject(decoder& stream, failure selected)
  {
    root = &stream;
    kind = selected;
    fired = false;
    decoder::interpretation_test_hook = child_checkpoint;
  }

  static void inject_parse(failure selected)
  {
    kind = selected;
    fired = false;
    pdf_resource<PAGE_PATTERN>::parse_test_hook = parse_checkpoint;
  }

  static void reset()
  {
    decoder::interpretation_test_hook = nullptr;
    pdf_resource<PAGE_PATTERN>::parse_test_hook = nullptr;
    root = nullptr;
  }

  static std::size_t frames(const decoder& stream) { return stream.stack.size(); }
  static int pattern_depth() { return decoder::tiling_depth(); }
  static std::array<double, 9> matrix(decoder& stream)
  { return stream.current_global_state().trafo_matrix; }
  static bool paints_glyph(decoder& stream)
  { return stream.current_bitmap_state().paints_type3_glyph; }
  static bool cache_empty(const pdf_resource<PAGE_PATTERN>& pattern)
  { return pattern.stream_cache_.empty() and pattern.stream_cache_.capacity()==0; }
  static void glyph(decoder& stream, const pdf_state<TEXT>::type3_procedure& proc)
  { stream.do_type3_glyph(proc); }
};
}

namespace
{
using access = pdflib::stream_state_lifetime_test_access;
using failure = access::failure;

void require(bool condition, const std::string& message)
{
  if(not condition) { throw std::runtime_error(message); }
}

QPDFObjectHandle decorated_stream(QPDF& pdf, const std::string& content,
                                  const std::string& dictionary)
{
  auto stream = pdf.newStream(content);
  auto extra = QPDFObjectHandle::parse(dictionary);
  auto dict = stream.getDict();
  for(const auto& key : extra.getKeys()) { dict.replaceKey(key, extra.getKey(key)); }
  return stream;
}

struct fixture
{
  QPDF pdf;
  pdflib::decode_config config;
  pdflib::page_item<pdflib::PAGE_DIMENSION> dimension;
  pdflib::page_item<pdflib::PAGE_CELLS> cells;
  pdflib::page_item<pdflib::PAGE_SHAPES> shapes;
  pdflib::page_item<pdflib::PAGE_IMAGES> images;
  std::shared_ptr<pdflib::pdf_resource<pdflib::PAGE_FONTS>> fonts =
    std::make_shared<pdflib::pdf_resource<pdflib::PAGE_FONTS>>();
  std::shared_ptr<pdflib::pdf_resource<pdflib::PAGE_GRPHS>> grphs =
    std::make_shared<pdflib::pdf_resource<pdflib::PAGE_GRPHS>>();
  std::shared_ptr<pdflib::pdf_resource<pdflib::PAGE_COLORSPACES>> colorspaces =
    std::make_shared<pdflib::pdf_resource<pdflib::PAGE_COLORSPACES>>();
  std::shared_ptr<pdflib::pdf_resource<pdflib::PAGE_SHADINGS>> shadings =
    std::make_shared<pdflib::pdf_resource<pdflib::PAGE_SHADINGS>>();
  std::shared_ptr<pdflib::pdf_resource<pdflib::PAGE_PATTERNS>> patterns =
    std::make_shared<pdflib::pdf_resource<pdflib::PAGE_PATTERNS>>();
  std::shared_ptr<pdflib::pdf_resource<pdflib::PAGE_XOBJECTS>> xobjects =
    std::make_shared<pdflib::pdf_resource<pdflib::PAGE_XOBJECTS>>();
  pdflib::pdf_render_instructions instructions;
  pdflib::pdf_timings timings;
  pdflib::pdf_decoder<pdflib::STREAM> stream;

  fixture(): stream(config, dimension, cells, shapes, images, fonts, grphs,
                    colorspaces, shadings, patterns, xobjects, instructions, timings)
  {
    pdf.emptyPDF();
    pdf.setSuppressWarnings(true);
    config.do_thread_safe = false;
    dimension.execute(QPDFObjectHandle::parse("<< /MediaBox [0 0 200 200] >>"));
    dimension.set_page_boundaries("crop_box");
    run("n\n");
  }
  ~fixture() { access::reset(); }

  void run(const std::string& content)
  {
    auto source = pdf.newStream(content);
    std::vector<pdflib::qpdf_stream_instruction> parameters;
    stream.decode_and_interprete(source, parameters);
  }

  void pattern(bool lattice)
  {
    const std::string step = lattice ? "300" : "20";
    auto cell = decorated_stream(pdf, "11 22 m 33 44 l S\n",
      "<< /Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 " +
      step + " " + step + "] /XStep " + step + " /YStep " + step + " >>");
    auto resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/P0", cell);
    patterns->set(resources);
  }

  void form()
  {
    auto form = decorated_stream(pdf, "11 22 m 33 44 l S\n",
      "<< /Type /XObject /Subtype /Form /BBox [0 0 100 100] /Matrix [2 0 0 2 30 40] >>");
    auto resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/Fm", form);
    xobjects->set(resources, timings);
  }

  void check_suffix()
  {
    require(access::frames(stream)==1, "nested failure leaked parent graphics frames");
    require(access::matrix(stream)==std::array<double, 9>{1,0,0,0,1,0,0,0,1},
            "nested failure leaked a parent transform");
    require(access::pattern_depth()==0, "pattern failure poisoned thread-local depth");
    require(not access::paints_glyph(stream), "glyph failure leaked bitmap paint mode");
    const auto& output = instructions.get_shape_instructions();
    require(not output.empty(), "page suffix did not paint");
    const auto& path = output.back().get_subpaths().at(0);
    require(std::abs(path.get_x0()-70)<1e-9 and std::abs(path.get_y0()-80)<1e-9,
            "page suffix was moved by the failed child");
    require(not output.back().get_clip_state().has_clip(), "child clip leaked into suffix");
  }
};

const std::string suffix = "n 0 0 1 RG 2 w 70 80 m 90 80 l S\n";

template<class Operation>
void expect_failure(Operation operation, failure kind)
{
  bool propagated = false;
  try { operation(); }
  catch(const std::bad_alloc&)
    { require(kind==failure::allocation, "failure changed to bad_alloc"); propagated = true; }
  catch(const access::failure_marker&)
    { require(kind==failure::nonstandard, "failure changed to nonstandard"); propagated = true; }
  catch(const std::runtime_error&)
    { require(kind==failure::recoverable, "failure changed to runtime_error"); propagated = true; }
  require(propagated, "resource/interpreter failure was swallowed");
  require(access::fired, "fault checkpoint did not execute");
}

void nested_recovery(bool pattern, bool lattice, failure kind)
{
  fixture f;
  if(pattern) { f.pattern(lattice); }
  else        { f.form(); }
  access::inject(f.stream, kind);
  const std::string operation = pattern
    ? "/Pattern cs /P0 scn 0 0 100 100 re f\n" : "/Fm Do\n";
  if(kind==failure::recoverable)
    {
      f.run(operation + suffix);
      require(access::fired, "recoverable checkpoint did not execute");
    }
  else
    {
      expect_failure([&] { f.run(operation); }, kind);
      f.run(suffix);
    }
  f.check_suffix();
  if(pattern)
    {
      require(not (*f.patterns)["/P0"].is_decoding(), "failed cell left its recursion flag set");
      f.run(operation + suffix);
      f.check_suffix();
    }
}

void glyph_failure_restores_parent(failure kind)
{
  fixture f;
  pdflib::pdf_state<pdflib::TEXT>::type3_procedure glyph{
    f.pdf.newStream("11 22 m 33 44 l S\n"), QPDFObjectHandle::newDictionary(),
    {2,0,0,2,30,40}};
  // More failures than the recursion cap reveal a depth leak on this thread.
  for(int attempt=0; attempt<10; ++attempt)
    {
      access::inject(f.stream, kind);
      expect_failure([&] { access::glyph(f.stream, glyph); }, kind);
    }
  access::glyph(f.stream, glyph);
  f.run(suffix);
  f.check_suffix();
}

void failed_pattern_parse_is_retryable(failure kind)
{
  fixture f;
  f.pattern(false);
  auto& pattern = (*f.patterns)["/P0"];
  access::inject_parse(kind);
  expect_failure([&] { pattern.parse_stream(); }, kind);
  require(not pattern.has_parsed_stream(), "failed source parse was committed as complete");
  require(access::cache_empty(pattern), "failed source retained partial tokens/capacity");
  const auto& source = pattern.parse_stream();
  require(pattern.has_parsed_stream() and source.size()==7, "source parse did not retry");
  auto first = source.front().obj;
  require(first.getIntValue()==11, "retry reused the partial token prefix");
}
}

int main()
{
  try
    {
      loguru::g_stderr_verbosity = loguru::Verbosity_FATAL;
      for(auto kind : {failure::recoverable, failure::allocation, failure::nonstandard})
        {
          nested_recovery(true, false, kind);
          nested_recovery(true, true, kind);
          nested_recovery(false, false, kind);
          glyph_failure_restores_parent(kind);
        }
      failed_pattern_parse_is_retryable(failure::allocation);
      failed_pattern_parse_is_retryable(failure::nonstandard);
      std::cout << "Stream state lifetime/recovery checks passed\n";
    }
  catch(const std::exception& error)
    { std::cerr << error.what() << "\n"; return 1; }
  catch(...)
    { std::cerr << "Unexpected nonstandard exception\n"; return 1; }
  return 0;
}
