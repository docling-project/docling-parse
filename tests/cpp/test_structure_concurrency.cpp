// Run against a multipage tagged PDF, optionally compiled with ThreadSanitizer.
// This exercises parser-owned synchronization; linked third-party libraries
// must also be instrumented for a complete audit of their internals.
#include <parse.h>

#include <atomic>
#include <thread>

int main(int argc, char** argv)
{
  if(argc!=2) { return 2; }
  loguru::g_stderr_verbosity = loguru::Verbosity_FATAL;
  nlohmann::json font_config = {{
    pdflib::pdf_resource<pdflib::PAGE_FONT>::RESOURCE_DIR_KEY,
    resource_utils::get_resources_dir(true).string()}};
  std::unordered_map<std::string, double> font_timings;
  pdflib::pdf_resource<pdflib::PAGE_FONT>::initialise(font_config, font_timings);
  pdflib::pdf_decoder<pdflib::DOCUMENT> document;
  std::string filename = argv[1];
  std::optional<std::string> password;
  if(not document.process_document_from_file(filename, password)) { return 3; }
  std::atomic<bool> start{false};
  std::atomic<bool> failed{false};
  std::vector<std::thread> readers;
  for(int i=0; i<8; ++i)
    {
      readers.emplace_back([&] {
        while(not start.load()) { std::this_thread::yield(); }
        for(int repetition=0; repetition<10; ++repetition)
          {
            if(document.get_structure().is_null()) { failed = true; }
            (void)document.get_annotations();
            (void)document.get_document_timings();
          }
      });
    }
  start = true;
  pdflib::decode_config config;
  config.do_thread_safe = false;
  for(int page=0; page<document.get_number_of_pages(); ++page)
    {
      // Decode directly from the shared QPDF, concurrently with first readers.
      auto decoded = document.decode_page(page, config);
      if(not decoded) { failed = true; continue; }
      for(auto& cell : decoded->get_char_cells())
        {
          auto json = cell.get();
          pdflib::page_item<pdflib::PAGE_CELL> copy;
          if(not copy.init_from(json) or copy.mcid!=cell.mcid or
             copy.get_artifact_type()!=cell.get_artifact_type() or
             copy.get_artifact_subtype()!=cell.get_artifact_subtype()) { failed = true; }
          // Reusing a cell for an old record must clear all linkage fields.
          json.erase(json.begin()+25, json.end());
          if(not copy.init_from(json) or copy.mcid!=-1 or copy.artifact) { failed = true; }
        }
    }
  for(auto& reader : readers) { reader.join(); }
  return failed ? 1 : 0;
}
