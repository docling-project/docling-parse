// SPDX-License-Identifier: MIT
// Native callback/recovery regressions. Run through CTest after building the
// optional memory-probe targets. These checks remain active in Release builds.
#include <parse.h>

#include <limits>
#include <new>
#include <stdexcept>
#include <zlib.h>

namespace
{
void require(bool condition, const std::string& message)
{
  if(not condition) { throw std::runtime_error(message); }
}

struct consumer_error : std::runtime_error
{
  consumer_error(): std::runtime_error("consumer failure marker") {}
};

struct recording_consumer
{
  std::vector<std::string> values;
  std::size_t calls = 0, inline_images = 0;
  std::size_t throw_after = std::numeric_limits<std::size_t>::max();
  bool exhaust_memory = false;

  static void consume(void* opaque, pdflib::qpdf_stream_instruction& row)
  {
    auto& self = *static_cast<recording_consumer*>(opaque);
    ++self.calls;
    if(self.values.size()==self.throw_after)
      {
        if(self.exhaust_memory) { throw std::bad_alloc(); }
        throw consumer_error();
      }
    self.inline_images += row.obj.isInlineImage();
    self.values.push_back(row.key + ":" + row.val);
  }
};

QPDFObjectHandle flate_stream(QPDF& pdf, const std::string& content)
{
  uLongf size = compressBound(content.size());
  std::string compressed(size, '\0');
  require(compress2(reinterpret_cast<Bytef*>(compressed.data()), &size,
                    reinterpret_cast<const Bytef*>(content.data()),
                    content.size(), Z_DEFAULT_COMPRESSION)==Z_OK, "compression failed");
  compressed.resize(size);
  auto stream = pdf.newStream("");
  stream.replaceStreamData(compressed, QPDFObjectHandle::newName("/FlateDecode"),
                           QPDFObjectHandle::newNull());
  return stream;
}

QPDFObjectHandle broken_stream(QPDF& pdf)
{
  auto stream = pdf.newStream("");
  stream.replaceStreamData("this is not zlib data",
                           QPDFObjectHandle::newName("/FlateDecode"),
                           QPDFObjectHandle::newNull());
  return stream;
}

QPDFObjectHandle broken_flate_with_prefix(QPDF& pdf, const std::string& content)
{
  // Sync-flush a valid non-final prefix, then append an invalid DEFLATE block
  // type. QPDF's all-at-once decode fails; prefix recovery can still read it.
  std::string compressed(compressBound(content.size())+64, '\0');
  z_stream encoder{};
  require(deflateInit(&encoder, Z_DEFAULT_COMPRESSION)==Z_OK, "deflateInit failed");
  encoder.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(content.data()));
  encoder.avail_in = static_cast<uInt>(content.size());
  encoder.next_out = reinterpret_cast<Bytef*>(compressed.data());
  encoder.avail_out = static_cast<uInt>(compressed.size());
  const int result = deflate(&encoder, Z_SYNC_FLUSH);
  const auto size = encoder.total_out;
  const bool complete = encoder.avail_in==0 and encoder.avail_out>0;
  deflateEnd(&encoder);
  require(result==Z_OK and complete, "prefix compression failed");
  compressed.resize(size);
  compressed.push_back('\x07'); // BFINAL=1, BTYPE=3 (reserved/invalid).
  auto stream = pdf.newStream("");
  stream.replaceStreamData(compressed, QPDFObjectHandle::newName("/FlateDecode"),
                           QPDFObjectHandle::newNull());
  return stream;
}

void compare_deliveries(QPDF& pdf, const std::string& content, bool has_inline_image)
{
  auto input = pdf.newStream(content);
  std::vector<pdflib::qpdf_stream_instruction> buffered;
  pdflib::qpdf_stream_decoder collector(buffered);
  collector.decode(input);
  recording_consumer observed;
  pdflib::qpdf_stream_decoder immediate(recording_consumer::consume, &observed);
  immediate.decode(input);

  require(buffered.size()==observed.values.size(), "delivery counts differ");
  for(std::size_t i=0; i<buffered.size(); ++i)
    {
      require(buffered[i].key + ":" + buffered[i].val==observed.values[i],
              "normalized token/order differs");
    }
  require(immediate.get_objects_delivered()==observed.values.size(), "wrong progress count");
  require(immediate.get_source_bytes_observed()==content.size(), "wrong source-byte count");
  require((observed.inline_images>0)==has_inline_image, "inline image token was lost");
}

void consumer_failure_propagates(QPDF& pdf, std::size_t prefix, bool bad_alloc,
                                 int source_case)
{
  auto input = flate_stream(pdf, "11 22 m 33 44 l S\n");
  if(source_case==1)
    {
      input = QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{
        pdf.newStream("11 22 m\n"), broken_stream(pdf), pdf.newStream("33 44 l S\n")
      });
    }
  else if(source_case==2)
    {
      input = broken_flate_with_prefix(pdf, "11 22 m 33 44 l S\n");
    }
  recording_consumer observed;
  observed.throw_after = prefix;
  observed.exhaust_memory = bad_alloc;
  pdflib::qpdf_stream_decoder decoder(recording_consumer::consume, &observed);
  bool propagated = false;
  try
    {
      decoder.decode(input);
    }
  catch(const consumer_error&)
    {
      require(not bad_alloc, "allocation failure changed exception type");
      propagated = true;
    }
  catch(const std::bad_alloc&)
    {
      require(bad_alloc, "consumer failure changed exception type");
      propagated = true;
    }
  require(propagated, "consumer failure was swallowed");
  require(observed.calls==prefix+1, "consumer prefix was replayed");
  require(observed.values.size()==prefix, "unexpected output after consumer failure");
  require(decoder.get_objects_delivered()==prefix, "failed delivery counted as successful");

  // Reusing a decoder must not retain the exception from an earlier consumer.
  observed.throw_after = std::numeric_limits<std::size_t>::max();
  observed.exhaust_memory = false;
  auto broken = broken_stream(pdf);
  decoder.decode(broken);
  require(decoder.get_objects_delivered()==0, "broken stream delivered spurious objects");
}

void damaged_array_keeps_each_good_stream_once(QPDF& pdf)
{
  auto before = pdf.newStream("11 22 m\n");
  auto after = pdf.newStream("33 44 l S\n");
  auto broken = broken_stream(pdf);
  auto contents = QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{before, broken, after});
  recording_consumer observed;
  pdflib::qpdf_stream_decoder decoder(recording_consumer::consume, &observed);
  decoder.decode(contents);
  const std::vector<std::string> expected = {
    "integer:11", "integer:22", "operator:m", "integer:33",
    "integer:44", "operator:l", "operator:S"
  };
  require(observed.values==expected, "damaged array dropped/replayed a good prefix or suffix");
  require(decoder.get_objects_delivered()==expected.size(), "wrong fallback progress count");
}
}

int main()
{
  try
    {
      loguru::g_stderr_verbosity = loguru::Verbosity_FATAL;
      QPDF pdf;
      pdf.emptyPDF();
      pdf.setSuppressWarnings(true);
      compare_deliveries(pdf, "null [18 2 3 2] 0 d 11 22 m 33 44 l S\n", false);
      compare_deliveries(pdf, "BI /W 1 /H 1 /CS /G /BPC 8 ID x EI\n", true);
      for(std::size_t prefix : {std::size_t(0), std::size_t(3)})
        {
          for(int source_case=0; source_case<3; ++source_case)
            {
              consumer_failure_propagates(pdf, prefix, false, source_case);
              consumer_failure_propagates(pdf, prefix, true, source_case);
            }
        }
      damaged_array_keeps_each_good_stream_once(pdf);
      std::cout << "Callback delivery/recovery checks passed\n";
    }
  catch(const std::exception& error)
    {
      std::cerr << error.what() << "\n";
      return 1;
    }
  return 0;
}
