//-*-C++-*-

#ifndef PYBIND_PDF_EDITOR_H
#define PYBIND_PDF_EDITOR_H

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <pybind/docling_parser.h>

#include <edit.h>
#include <render/blend2d_renderer.h>

namespace docling
{
  // Inspection, editing and writing of one PDF document. All functionality of
  // PdfReader, PdfWriter and PdfEditor lives here; pdf_reader and pdf_writer
  // only choose which of these operations they publish.
  //
  // States. An editor is closed, reading or editing:
  //
  //   - reading: a source was opened and nothing was changed. The document is
  //     the inherited parser's decoder under SOURCE_KEY.
  //   - editing: the editor owns a destination QPDF. It gets one from
  //     create_empty(), or on the first mutating call after an open, which
  //     parses the source bytes a second time (a clone: the whole document
  //     survives, nothing is remapped). The source bytes are never modified.
  //
  // Thread safety. Not multi-threaded, but thread safe: every public method
  // holds the inherited parser_mutex for its whole duration, so calls from
  // several threads on one instance are serialized. Two editors never share a
  // QPDF: what crosses between them is a pdf_source (immutable bytes), which
  // the receiver parses for itself, so no operation takes two editors' locks.
  // Nothing here touches Python; the binding releases the GIL around each
  // call.
  //
  // Page handles. Page arguments are zero-based. Each takes the generation
  // the caller last saw; a call with an outdated generation fails with
  // edit_error::STALE_HANDLE instead of acting on whichever page now has that
  // index. Pass ANY_GENERATION to skip the check.
  //
  // Errors. Every failure leaves as pdflib::pdf_edit_error (category plus
  // message); open_* instead return false and record the reason, like the
  // inherited load_document.
  class docling_editor: public docling_parser
  {
    typedef pdflib::pdf_decoder<pdflib::DOCUMENT> document_decoder_type;
    typedef std::shared_ptr<document_decoder_type> document_decoder_ptr_type;

    typedef std::shared_ptr<pdflib::pdf_import_document> import_document_ptr_type;

  public:

    static constexpr std::int64_t ANY_GENERATION = -1;

    docling_editor(std::string loglevel = "fatal");
    ~docling_editor();

    // ---- lifecycle ----

    bool open_file(std::string filename, std::optional<std::string> password);
    bool open_buffer(std::shared_ptr<std::string> buffer, std::optional<std::string> password);
    bool open_source(const pdflib::pdf_source& source);

    // {"category": ..., "message": ...} of the last failed open, or empty.
    std::map<std::string, std::string> take_open_error();

    // Start an empty destination with no pages (assembly mode).
    void create_empty();

    bool is_open();
    bool is_editing();

    void close();

    // ---- inspection ----

    int page_count();

    // Changes whenever existing page indices may have changed meaning.
    std::int64_t generation();

    pdflib::pdf_document_info document_info();
    void validate_strict();
    pdflib::pdf_page_info page_info(int page, std::int64_t expected_generation);

    // The page's line cells in the order the parser emits them, joined by
    // "\n"; or, with layout, placed on a character grid that keeps the page's
    // columns, indentation and vertical spacing (pdf_text_layout).
    std::string extract_text(int page, std::int64_t expected_generation, bool layout);

    // One-based page number, unlike the internal page indices used by the
    // pypdf-compatible surfaces. Returns owned RGBA pixels and {height, width, 4}.
    std::pair<std::shared_ptr<std::vector<std::uint8_t>>, std::array<int, 3>>
    render_page(int page_no, double scale);

    // A bounded JSON view of one object, for inspection: `id` is its object
    // number, 0 the trailer, -1 the catalog.
    nlohmann::json object_json(int id, int depth);

    // The images the page draws, in drawing order, with their encoded bytes.
    std::vector<pdflib::pdf_exported_image> page_images(int page, std::int64_t expected_generation);

    nlohmann::json outline();
    nlohmann::json annotations();

    // Document-level content, read from the document as it currently is.
    // The JSON shapes are documented on the pdf_*_ops functions they come
    // from (src/edit).
    nlohmann::json document_settings();   // pdf_catalog_ops::read_settings
    std::vector<std::string> page_labels();
    nlohmann::json named_destinations();
    nlohmann::json outline_tree();        // pdf_outline_ops::read
    nlohmann::json form();                // pdf_form_ops::read
    nlohmann::json page_annotations(int page, std::int64_t expected_generation);
    nlohmann::json attachments();         // pdf_attachment_ops::read
    std::string attachment_data(std::string name);
    nlohmann::json javascript();

    // The bytes this document can be re-parsed from: the opened source while
    // reading, the serialized current state while editing.
    pdflib::pdf_source get_source();

    // Same, but fails with STALE_HANDLE when the page list is no longer the
    // one a page handle of this document was taken from. Used when another
    // editor is about to read one of this document's pages by index.
    pdflib::pdf_source get_source(std::int64_t expected_generation);

    // ---- editing ----

    void remove_page(int page, std::int64_t expected_generation, bool clean = false);
    void move_page(int from, int to, std::int64_t expected_generation);

    void rotate_page(int page, std::int64_t expected_generation, int angle, bool relative);
    void set_page_box(int page,
                      std::int64_t expected_generation,
                      std::string box,
                      std::array<double, 4> rectangle);

    // Concatenate [a b c d e f] to the page's content and annotations.
    void transform_page(int page,
                        std::int64_t expected_generation,
                        std::array<double, 6> matrix);

    // Scale content, annotations and all page boundaries by (sx, sy).
    void scale_page(int page, std::int64_t expected_generation, double sx, double sy);

    // Draw page `other_page` of another document on this page as vector
    // content: above it, or with under below it. See pdf_page_ops::overlay
    // for fit. The other page's annotations are not copied.
    void overlay_page(int page,
                      std::int64_t expected_generation,
                      const pdflib::pdf_source& other,
                      int other_page,
                      bool under,
                      bool fit,
                      std::array<double, 6> matrix);

    // Turn the page's /Rotate into a rotation of its content.
    void flatten_page_rotation(int page, std::int64_t expected_generation);

    // `at` is the index the first new page gets; negative appends at the end.
    // All three return the index of the first new page.
    int duplicate_page(int page, std::int64_t expected_generation, int at);
    int insert_blank_page(int at, double width, double height);

    // A page showing one image; see pdf_page_ops::make_image_page.
    int insert_image_page(int at,
                          int width,
                          int height,
                          std::string colorspace,
                          std::string data,
                          bool is_jpeg,
                          bool invert,
                          std::string alpha,
                          double page_width,
                          double page_height,
                          std::array<double, 4> placement,
                          int rotate);

    // Draw text on a page in one of the standard fonts; see
    // pdf_page_ops::draw_text.
    void draw_text(int page,
                   std::int64_t expected_generation,
                   std::string text,
                   double x,
                   double y,
                   double size,
                   std::string font,
                   std::array<double, 3> color);
    // With import_outline, the other document's bookmarks that lead to a
    // copied page are recreated at the top level of this document's outline.
    int append_pages(const pdflib::pdf_source& other,
                     std::optional<std::vector<int> > pages,
                     int at,
                     bool import_outline);

    // ---- document-level editing ----

    // Entries of /Info; a key without a value is removed.
    void set_info(std::map<std::string, std::optional<std::string> > entries);
    void set_xmp(std::optional<std::string> packet);

    // Empty name removes the entry.
    void set_page_layout(std::string name);
    void set_page_mode(std::string name);

    // The new /ViewerPreferences dictionary in PDF syntax; empty removes it.
    void set_viewer_preferences(std::string dictionary);

    // Page to show when the document is opened; -1 removes the open action.
    void set_open_destination(int page);

    void set_page_label(int from, int to, std::string style, std::string prefix, int start);
    void add_named_destination(std::string name, int page);

    // Returns the id of the new item, usable as `parent` of later items.
    int add_outline_item(std::string title, int page, int parent);

    // Outline editing; the JSON entries are documented on
    // pdf_outline_ops::add_item and update_item.
    int add_outline_entry(nlohmann::json spec);
    void update_outline_item(int id, nlohmann::json changes);
    void remove_outline_item(int id);
    void move_outline_item(int id, int parent, int index);
    void clear_outline();

    // ---- forms, annotations, attachments, scripts ----

    int set_form_values(std::map<std::string, std::string> values);
    void set_need_appearances(bool value);
    void flatten_annotations();

    // Form fields; the JSON entries are documented on pdf_form_ops::add_field
    // and update_field.
    void add_form_field(int page, std::int64_t expected_generation, nlohmann::json spec);
    void update_form_field(std::string name, nlohmann::json changes);
    void remove_form_field(std::string name);

    // `dictionary` is the annotation in PDF syntax.
    void add_annotation(int page, std::int64_t expected_generation, std::string dictionary);
    void add_page_link(int page,
                       std::int64_t expected_generation,
                       std::array<double, 4> rectangle,
                       int target);
    int remove_annotations(int page,
                           std::int64_t expected_generation,
                           std::set<std::string> subtypes);

    void add_attachment(std::string name,
                        std::string bytes,
                        std::string description,
                        std::string mime_type);
    bool remove_attachment(std::string name);

    void add_javascript(std::string name, std::string script);
    int remove_javascript();

    // ---- output ----

    // Encrypt everything written from now on with AES-256 (ISO 32000-2,
    // revision 6), the one password scheme PDF 2.0 retains. The user password
    // opens the document; an empty one lets anyone open it. The owner
    // password defaults to the user password.
    void set_encryption(std::string user_password,
                        std::string owner_password,
                        pdflib::pdf_permissions permissions,
                        std::string algorithm = "AES-256");
    void clear_encryption();

    // How the file is written. compress_streams: Flate-compress streams that
    // are not compressed. object_streams: "preserve", "generate" (smaller
    // files) or "disable". linearize: order the file for page-at-a-time
    // loading over a network.
    void set_output_options(bool compress_streams, std::string object_streams, bool linearize);

    // Full rewrite of the current state; unencrypted unless set_encryption
    // was called, whatever the source was. The returned buffer is immutable
    // and shared with later callers until the next edit.
    std::shared_ptr<std::string> write_to_memory();

    // Writes a temporary sibling file and renames it over `filename`, so a
    // failed write never leaves a truncated file behind.
    void write_to_file(std::string filename);

  private:

    // All helpers below expect the caller to hold parser_mutex.

    template<typename function_type>
    decltype(auto) guarded(function_type&& function);

    template<typename function_type>
    decltype(auto) with_document(function_type&& function);

    void reset_state();

    // The three shapes almost every operation has. Each takes the lock and
    // translates errors; the editing ones also make sure there is a
    // destination and mark the document as changed.
    template<typename function_type>
    decltype(auto) read_document(function_type&& function);

    template<typename function_type>
    decltype(auto) edit_document(function_type&& function);

    template<typename function_type>
    decltype(auto) edit_page(int page, std::int64_t expected_generation, function_type&& function);

    void require_open();
    void check_generation(std::int64_t expected_generation);
    void check_page(int page, int number_of_pages);

    document_decoder_ptr_type require_source_decoder();

    // Clone the opened source into a destination, if there is none yet.
    void ensure_destination();
    std::vector<QPDFPageObjectHelper> destination_pages();

    void touch(bool page_list_changed);

    void ensure_snapshot();
    std::string decode_key();

    // The password that opens the snapshot, if it is encrypted and needs one.
    std::optional<std::string> snapshot_password() const;

    // A fresh decode of one page with exactly this config.
    std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE> > decode_page(int page,
                                                                     const pdflib::decode_config& config);

    // Resolve an insertion index: returns the position, sets at_end.
    int insertion_position(int at, int number_of_pages, bool& at_end);

    import_document_ptr_type find_or_create_import(const pdflib::pdf_source& other);

    QPDFObjectHandle find_or_create_overlay_form(const pdflib::pdf_source& other,
                                                 int other_page,
                                                 bool fit);

    static std::filesystem::path to_path(const std::string& filename);
    static std::string random_suffix();

  private:

    static constexpr const char* SOURCE_KEY = "docling-editor-source";
    static constexpr const char* SNAPSHOT_KEY = "docling-editor-snapshot";

    // Decoded pages kept by the inherited cache; text extraction needs none
    // of them once the text is returned.
    static constexpr int PAGE_CACHE_SIZE = 4;

    // All guarded by the inherited parser_mutex; the editor adds no second lock.

    pdflib::pdf_source source;

    // Order matters: the destination reads lazily from destination_buffer
    // (qpdf does not copy the bytes it parses) and from the imported
    // documents (foreign stream data is copied at write time), so it is
    // declared last and therefore destroyed first.
    std::shared_ptr<std::string> destination_buffer;
    std::map<const std::string*, import_document_ptr_type> imports;
    std::unique_ptr<QPDF> destination;

    // Pages of imported documents already turned into form XObjects of the
    // destination, by (source bytes, page, fit): a watermark stamped on every
    // page is copied once. The handles point into the destination, so they
    // are declared after it and released before it.
    typedef std::tuple<const std::string*, int, bool> overlay_key_type;
    std::map<overlay_key_type, QPDFObjectHandle> overlay_forms;

    // Standard font dictionaries already added to the destination, by name,
    // so that text drawn on many pages shares one font object.
    std::map<std::string, QPDFObjectHandle> standard_fonts;

    std::int64_t current_generation; // page-list identity, for page handles
    std::int64_t current_revision;   // any change, for the snapshot

    // The destination serialized at snapshot_revision. It backs write, text
    // extraction and get_source(), so each edit is serialized at most once.
    std::shared_ptr<std::string> snapshot;
    std::int64_t snapshot_revision;
    bool snapshot_loaded; // registered with the parser under SNAPSHOT_KEY

    // Output encryption, off by default. The snapshot is the output, so it is
    // encrypted too and read back with the user password.
    bool output_encrypted;
    std::string output_algorithm = "AES-256";
    std::string output_user_password;
    std::string output_owner_password;
    pdflib::pdf_permissions output_permissions;

    bool output_compress_streams;
    qpdf_object_stream_e output_object_streams;
    bool output_linearize;
  };

  inline docling_editor::docling_editor(std::string loglevel):
    docling_parser(loglevel, PAGE_CACHE_SIZE),
    source(),
    destination_buffer(nullptr),
    imports({}),
    destination(nullptr),
    overlay_forms({}),
    standard_fonts({}),
    current_generation(0),
    current_revision(0),
    snapshot(nullptr),
    snapshot_revision(-1),
    snapshot_loaded(false),
    output_encrypted(false),
    output_user_password(""),
    output_owner_password(""),
    output_permissions(),
    output_compress_streams(true),
    output_object_streams(qpdf_o_preserve),
    output_linearize(false)
  {}

  inline docling_editor::~docling_editor()
  {}

  template<typename function_type>
  decltype(auto) docling_editor::guarded(function_type&& function)
  {
    try
      {
        return function();
      }
    catch(const pdflib::pdf_edit_error&)
      {
        throw;
      }
    catch(const QPDFExc& exc)
      {
        throw pdflib::pdf_edit_error(pdflib::load_error::category_of(exc.getErrorCode()),
                                     exc.what());
      }
    catch(const pdflib::missing_page_object_error& exc)
      {
        throw pdflib::pdf_edit_error(pdflib::load_error::DAMAGED, exc.what());
      }
    catch(const std::exception& exc)
      {
        throw pdflib::pdf_edit_error(pdflib::load_error::UNKNOWN, exc.what());
      }
  }

  template<typename function_type>
  decltype(auto) docling_editor::with_document(function_type&& function)
  {
    if(destination != nullptr)
      {
        return function(*destination);
      }

    // Reading: the decoder serializes this with its own page extraction.
    return require_source_decoder()->with_qpdf(function);
  }

  template<typename function_type>
  decltype(auto) docling_editor::read_document(function_type&& function)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this, &function]() {
      return with_document(function);
    });
  }

  template<typename function_type>
  decltype(auto) docling_editor::edit_document(function_type&& function)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this, &function]() {
      ensure_destination();

      // before the change: a failure half way still invalidates the snapshot
      touch(false);

      return function(*destination);
    });
  }

  template<typename function_type>
  decltype(auto) docling_editor::edit_page(int page,
                                           std::int64_t expected_generation,
                                           function_type&& function)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this, page, expected_generation, &function]() {
      check_generation(expected_generation);
      ensure_destination();

      std::vector<QPDFPageObjectHelper> pages = destination_pages();
      check_page(page, static_cast<int>(pages.size()));

      touch(false);

      return function(*destination, pages.at(page));
    });
  }

  inline void docling_editor::reset_state()
  {
    source = pdflib::pdf_source();

    // destination before what it reads from, as in the destructor
    overlay_forms.clear();
    standard_fonts.clear();
    destination.reset();
    imports.clear();
    destination_buffer.reset();

    snapshot.reset();
    snapshot_revision = -1;
    snapshot_loaded = false;

    output_encrypted = false;
    output_user_password.clear();
    output_owner_password.clear();
    output_permissions = pdflib::pdf_permissions();

    output_compress_streams = true;
    output_object_streams = qpdf_o_preserve;
    output_linearize = false;

    // A reopened editor is a different document: outstanding page handles
    // must not match it.
    current_generation += 1;
    current_revision += 1;

    unload_documents();
  }

  inline void docling_editor::require_open()
  {
    if(destination == nullptr and not source.is_valid())
      {
        throw pdflib::pdf_edit_error(pdflib::edit_error::INVALID_EDIT,
                                     "no document is open");
      }
  }

  inline void docling_editor::check_generation(std::int64_t expected_generation)
  {
    if(expected_generation != ANY_GENERATION and expected_generation != current_generation)
      {
        throw pdflib::pdf_edit_error(pdflib::edit_error::STALE_HANDLE,
                                     "the page list changed since this page handle was obtained");
      }
  }

  inline void docling_editor::check_page(int page, int number_of_pages)
  {
    if(page < 0 or page >= number_of_pages)
      {
        std::stringstream message;
        message << "page index " << page << " is out of range for a document of "
                << number_of_pages << " pages";
        throw pdflib::pdf_edit_error(pdflib::edit_error::INVALID_EDIT, message.str());
      }
  }

  inline docling_editor::document_decoder_ptr_type docling_editor::require_source_decoder()
  {
    require_open();

    document_decoder_ptr_type decoder = get_document_decoder(SOURCE_KEY);
    if(decoder == nullptr)
      {
        throw pdflib::pdf_edit_error(pdflib::edit_error::INVALID_EDIT,
                                     "no document is open");
      }

    return decoder;
  }

  inline void docling_editor::ensure_destination()
  {
    if(destination != nullptr)
      {
        return;
      }

    require_open();

    // Clone: a second parse of the same immutable bytes. Everything the file
    // contains is carried over because nothing is copied object by object.
    auto qpdf = std::make_unique<QPDF>();
    qpdf->setSuppressWarnings(true);

    const std::shared_ptr<std::string>& buffer = source.get_buffer();
    if(source.get_password().has_value())
      {
        qpdf->processMemoryFile("docling-editor clone",
                                buffer->c_str(),
                                buffer->size(),
                                source.get_password().value().c_str());
      }
    else
      {
        qpdf->processMemoryFile("docling-editor clone",
                                buffer->c_str(),
                                buffer->size());
      }

    destination_buffer = buffer;
    destination = std::move(qpdf);

    // The page list is the same, so page handles stay valid; from here on the
    // destination is the document and the source decoder is not needed.
    if(is_loaded(SOURCE_KEY))
      {
        unload_document(SOURCE_KEY);
      }
  }

  inline std::vector<QPDFPageObjectHelper> docling_editor::destination_pages()
  {
    return QPDFPageDocumentHelper(*destination).getAllPages();
  }

  inline void docling_editor::touch(bool page_list_changed)
  {
    current_revision += 1;

    if(page_list_changed)
      {
        current_generation += 1;
      }
  }

  inline void docling_editor::ensure_snapshot()
  {
    ensure_destination();

    if(snapshot != nullptr and snapshot_revision == current_revision)
      {
        return;
      }

    try
      {
        QPDFWriter writer(*destination);
        writer.setOutputMemory();

        writer.setCompressStreams(output_compress_streams);
        writer.setObjectStreamMode(output_object_streams);
        writer.setLinearization(output_linearize);

        if(output_encrypted)
          {
            qpdf_r3_print_e print = qpdf_r3p_full;
            if(output_permissions.get_print() == 1) { print = qpdf_r3p_low; }
            if(output_permissions.get_print() >= 2) { print = qpdf_r3p_none; }

            if(output_algorithm == "RC4-40")
              {
                writer.setR2EncryptionParametersInsecure(output_user_password.c_str(),
                  output_owner_password.c_str(), output_permissions.get_print() != 2,
                  output_permissions.get_modify(), output_permissions.get_extract(),
                  output_permissions.get_annotate());
              }
            else if(output_algorithm == "RC4-128")
              {
                writer.setR3EncryptionParametersInsecure(output_user_password.c_str(),
                  output_owner_password.c_str(), output_permissions.get_accessibility(),
                  output_permissions.get_extract(), output_permissions.get_assemble(),
                  output_permissions.get_annotate(), output_permissions.get_fill_forms(),
                  output_permissions.get_modify(), print);
              }
            else if(output_algorithm == "AES-128")
              {
                writer.setR4EncryptionParametersInsecure(output_user_password.c_str(),
                  output_owner_password.c_str(), output_permissions.get_accessibility(),
                  output_permissions.get_extract(), output_permissions.get_assemble(),
                  output_permissions.get_annotate(), output_permissions.get_fill_forms(),
                  output_permissions.get_modify(), print, true, true);
              }
            else if(output_algorithm == "AES-256-R5")
              {
                writer.setR5EncryptionParameters(output_user_password.c_str(),
                  output_owner_password.c_str(), output_permissions.get_accessibility(),
                  output_permissions.get_extract(), output_permissions.get_assemble(),
                  output_permissions.get_annotate(), output_permissions.get_fill_forms(),
                  output_permissions.get_modify(), print, true);
              }
            else
              {
                writer.setR6EncryptionParameters(output_user_password.c_str(),
                                             output_owner_password.c_str(),
                                             output_permissions.get_accessibility(),
                                             output_permissions.get_extract(),
                                             output_permissions.get_assemble(),
                                             output_permissions.get_annotate(),
                                             output_permissions.get_fill_forms(),
                                             output_permissions.get_modify(),
                                             print,
                                             true); // encrypt_metadata_aes
              }
          }
        else
          {
            // Output is unencrypted unless encryption was asked for: the
            // encryption of the source is not carried over.
            writer.setPreserveEncryption(false);
          }
        writer.write();

        std::shared_ptr<Buffer> out = writer.getBufferSharedPointer();

        snapshot = std::make_shared<std::string>(reinterpret_cast<const char*>(out->getBuffer()),
                                                 out->getSize());
        snapshot_revision = current_revision;
        snapshot_loaded = false;
      }
    catch(const std::exception& exc)
      {
        throw pdflib::pdf_edit_error(pdflib::edit_error::WRITE, exc.what());
      }
  }

  inline std::string docling_editor::decode_key()
  {
    if(destination == nullptr)
      {
        require_source_decoder();
        return SOURCE_KEY;
      }

    // Decoding reads a finished PDF, so an edited document is decoded from
    // its serialized state.
    ensure_snapshot();

    if(not snapshot_loaded)
      {
        if(not load_document_from_buffer(SNAPSHOT_KEY, snapshot, snapshot_password()))
          {
            std::map<std::string, std::string> error = take_load_error(SNAPSHOT_KEY);
            throw pdflib::pdf_edit_error(pdflib::edit_error::WRITE,
                                         "the edited document could not be read back: " + error["message"]);
          }
        snapshot_loaded = true;
      }

    return SNAPSHOT_KEY;
  }

  inline docling_editor::import_document_ptr_type
  docling_editor::find_or_create_import(const pdflib::pdf_source& other)
  {
    // One parsed QPDF per distinct byte buffer: pages imported in several
    // calls then share the objects qpdf already copied for the first one.
    const std::string* identity = other.get_buffer().get();

    auto itr = imports.find(identity);
    if(itr != imports.end())
      {
        return itr->second;
      }

    auto document = std::make_shared<pdflib::pdf_import_document>(other, "docling-editor import");
    imports[identity] = document;

    return document;
  }

  inline std::filesystem::path docling_editor::to_path(const std::string& filename)
  {
    // The filename is UTF-8; see pdf_decoder<DOCUMENT>::process_document_from_file
    // for why Windows needs the explicit conversion.
#ifdef _WIN32
    return std::filesystem::path(std::u8string(filename.begin(), filename.end()));
#else
    return std::filesystem::path(filename);
#endif
  }

  inline std::string docling_editor::random_suffix()
  {
    std::random_device device;

    std::stringstream stream;
    stream << std::hex << device() << device();

    return stream.str();
  }

  inline bool docling_editor::open_file(std::string filename, std::optional<std::string> password)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    reset_state();

    if(not load_document(SOURCE_KEY, filename, password))
      {
        return false;
      }

    document_decoder_ptr_type decoder = get_document_decoder(SOURCE_KEY);
    source = pdflib::pdf_source(decoder->get_source_buffer(), decoder->get_password());

    return true;
  }

  inline bool docling_editor::open_buffer(std::shared_ptr<std::string> buffer,
                                          std::optional<std::string> password)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    reset_state();

    if(not load_document_from_buffer(SOURCE_KEY, buffer, password))
      {
        return false;
      }

    source = pdflib::pdf_source(buffer, password);

    return true;
  }

  inline bool docling_editor::open_source(const pdflib::pdf_source& source_)
  {
    return open_buffer(source_.get_buffer(), source_.get_password());
  }

  inline std::map<std::string, std::string> docling_editor::take_open_error()
  {
    return take_load_error(SOURCE_KEY);
  }

  inline void docling_editor::create_empty()
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    guarded([this]() {
      reset_state();

      auto qpdf = std::make_unique<QPDF>();
      qpdf->setSuppressWarnings(true);
      qpdf->emptyPDF();

      destination = std::move(qpdf);
    });
  }

  inline bool docling_editor::is_open()
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return (destination != nullptr or source.is_valid());
  }

  inline bool docling_editor::is_editing()
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return (destination != nullptr);
  }

  inline void docling_editor::close()
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    reset_state();
  }

  inline int docling_editor::page_count()
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this]() -> int {
      if(destination != nullptr)
        {
          return static_cast<int>(destination_pages().size());
        }

      return require_source_decoder()->get_number_of_pages();
    });
  }

  inline std::int64_t docling_editor::generation()
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return current_generation;
  }

  inline pdflib::pdf_document_info docling_editor::document_info()
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this]() {
      pdflib::pdf_document_info info = with_document([](QPDF& qpdf) {
        return pdflib::pdf_document_info::read(qpdf);
      });

      if(destination != nullptr)
        {
          // While editing, report the encryption of what will be written,
          // not of what was opened: none by default, and with set_encryption
          // exactly what the serialized document declares.
          pdflib::pdf_encryption_info encryption;
          if(output_encrypted)
            {
              document_decoder_ptr_type decoder = get_document_decoder(decode_key());
              if(decoder != nullptr)
                {
                  encryption = decoder->with_qpdf([](QPDF& qpdf) {
                    return pdflib::pdf_encryption_info::read(qpdf);
                  });
                }
            }
          info.set_encryption(encryption);
        }

      return info;
    });
  }

  inline void docling_editor::validate_strict()
  {
    read_document([](QPDF& qpdf) {
      if(qpdf.anyWarnings())
        {
          throw pdflib::pdf_edit_error(pdflib::load_error::DAMAGED, "strict mode rejects a PDF that required repairs or produced structural warnings");
        }
    });
  }

  inline pdflib::pdf_page_info docling_editor::page_info(int page, std::int64_t expected_generation)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this, page, expected_generation]() {
      check_generation(expected_generation);

      return with_document([this, page](QPDF& qpdf) {
        std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        check_page(page, static_cast<int>(pages.size()));

        return pdflib::pdf_page_info::read(pages.at(page));
      });
    });
  }

  inline nlohmann::json docling_editor::object_json(int id, int depth)
  {
    return read_document([id, depth](QPDF& qpdf) {
      if(depth < 1 or depth > 32)
        {
          throw pdflib::pdf_edit_error(pdflib::edit_error::INVALID_EDIT,
                                       "the depth of an object view is between 1 and 32");
        }

      QPDFObjectHandle object = QPDFObjectHandle::newNull();
      if(id == 0)       { object = qpdf.getTrailer(); }
      else if(id == -1) { object = qpdf.getRoot(); }
      else if(id > 0)   { object = qpdf.getObjectByID(id, 0); }

      if(object.isNull())
        {
          throw pdflib::pdf_edit_error(pdflib::edit_error::INVALID_EDIT,
                                       "the document has no object " + std::to_string(id));
        }

      return pdflib::pdf_object_json::to_json(object, depth);
    });
  }

  inline std::string docling_editor::extract_text(int page,
                                                  std::int64_t expected_generation,
                                                  bool layout)
  {
    // Held across the decode, unlike docling_parser::get_page_decoder on its
    // own: the page index and the snapshot must stay the ones that were
    // checked. Calls on one editor are serialized by contract.
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this, page, expected_generation, layout]() {
      check_generation(expected_generation);
      check_page(page, page_count());

      // Text only: skip what the caller cannot ask for through this method.
      pdflib::decode_config config;
      config.keep_shapes = false;
      config.keep_bitmaps = false;
      config.create_word_cells = layout;
      config.create_line_cells = true;

      auto page_decoder = decode_page(page, config);

      if(layout)
        {
          // Words, not lines: a line cell may already join runs that sit far
          // apart on the page, and the distance between them is exactly
          // what the grid is meant to keep.
          auto& word_cells = page_decoder->get_word_cells();

          std::vector<std::string> texts;
          std::vector<pdflib::pdf_text_layout::box_type> boxes;
          std::vector<int> lengths;

          for(auto itr = word_cells.begin(); itr != word_cells.end(); ++itr)
            {
              texts.push_back(itr->text);
              boxes.push_back({std::min(itr->x0, itr->x1), std::min(itr->y0, itr->y1),
                               std::max(itr->x0, itr->x1), std::max(itr->y0, itr->y1)});
              lengths.push_back(itr->number_of_chars());
            }

          return pdflib::pdf_text_layout::render(texts, boxes, lengths);
        }

      auto& line_cells = page_decoder->get_line_cells();

      std::string text;

      for(auto itr = line_cells.begin(); itr != line_cells.end(); ++itr)
        {
          if(not text.empty())
            {
              text += "\n";
            }
          text += itr->text;
        }

      return text;
    });
  }

  inline std::pair<std::shared_ptr<std::vector<std::uint8_t>>, std::array<int, 3>>
  docling_editor::render_page(int page_no, double scale)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this, page_no, scale]() {
      if(page_no < 1 or page_no > page_count())
        {
          throw pdflib::pdf_edit_error(pdflib::edit_error::INVALID_EDIT,
                                       "page_no must be between 1 and the number of pages");
        }
      if(not std::isfinite(scale) or scale <= 0.0 or scale > std::numeric_limits<float>::max())
        {
          throw pdflib::pdf_edit_error(pdflib::edit_error::INVALID_EDIT,
                                       "scale must be finite and positive");
        }

      pdflib::render_config render_config;
      render_config.scale = static_cast<float>(scale);
      pdflib::validate_render_config(render_config);

      pdflib::decode_config decode_config;
      decode_config.extract_font_programs = true;
      decode_config.extract_bitmap_pixels = true;
      decode_config.keep_shapes = true;
      decode_config.keep_bitmaps = true;
      decode_config.create_word_cells = false;
      decode_config.create_line_cells = false;
      decode_config.bitmap_target_pixels_per_unit = render_config.scale;

      auto page_decoder = decode_page(page_no - 1, decode_config);
      pdflib::renderer<pdflib::BLEND2D> renderer(render_config);
      page_decoder->get_instructions().iterate_over_instructions(renderer);
      return std::make_pair(renderer.get_canvas(), renderer.get_shape());
    });
  }

  inline std::shared_ptr<pdflib::pdf_decoder<pdflib::PAGE> >
  docling_editor::decode_page(int page, const pdflib::decode_config& config)
  {
    std::string key = decode_key();

    // The inherited cache is keyed by page only, not by config: a page
    // decoded for its text must not be handed back when its images are asked
    // for. The editor returns owned copies and keeps no decoded page, so the
    // cached entry is dropped and the page decoded as asked.
    unload_document_page(key, page);

    auto page_decoder = get_page_decoder(key, page, config);
    if(page_decoder == nullptr)
      {
        throw pdflib::pdf_edit_error(pdflib::load_error::UNKNOWN,
                                     "the page could not be decoded");
      }

    return page_decoder;
  }

  inline std::vector<pdflib::pdf_exported_image>
  docling_editor::page_images(int page, std::int64_t expected_generation)
  {
    // Held across the decode, for the reason given in extract_text.
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this, page, expected_generation]() {
      check_generation(expected_generation);
      check_page(page, page_count());

      // Images only: no derived text cells, no vector shapes.
      pdflib::decode_config config;
      config.keep_shapes = false;
      config.keep_bitmaps = true;
      config.extract_bitmap_pixels = true;
      config.create_word_cells = false;
      config.create_line_cells = false;

      auto page_decoder = decode_page(page, config);

      std::vector<pdflib::pdf_exported_image> result;

      auto& images = page_decoder->get_page_images();
      for(auto itr = images.begin(); itr != images.end(); ++itr)
        {
          pdflib::pdf_exported_image::box_type bbox = {itr->x0, itr->y0, itr->x1, itr->y1};

          result.emplace_back(itr->xobject_key,
                              bbox,
                              itr->image_width,
                              itr->image_height,
                              itr->get_image_format(),
                              itr->get_pil_mode(),
                              itr->get_image_as_bytes());
        }

      return result;
    });
  }

  inline nlohmann::json docling_editor::outline()
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this]() {
      return get_table_of_contents(decode_key());
    });
  }

  inline nlohmann::json docling_editor::annotations()
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this]() {
      return get_annotations(decode_key());
    });
  }

  inline pdflib::pdf_source docling_editor::get_source()
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this]() {
      require_open();

      if(destination == nullptr)
        {
          return source;
        }

      ensure_snapshot();
      return pdflib::pdf_source(snapshot, snapshot_password());
    });
  }

  inline pdflib::pdf_source docling_editor::get_source(std::int64_t expected_generation)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    guarded([this, expected_generation]() {
      check_generation(expected_generation);
    });

    return get_source();
  }

  inline int docling_editor::insertion_position(int at, int number_of_pages, bool& at_end)
  {
    if(at > number_of_pages)
      {
        check_page(at, number_of_pages);
      }

    at_end = (at < 0 or at == number_of_pages);

    return at_end ? number_of_pages : at;
  }

  inline QPDFObjectHandle docling_editor::find_or_create_overlay_form(const pdflib::pdf_source& other,
                                                                      int other_page,
                                                                      bool fit)
  {
    overlay_key_type key(other.get_buffer().get(), other_page, fit);

    auto itr = overlay_forms.find(key);
    if(itr != overlay_forms.end())
      {
        return itr->second;
      }

    import_document_ptr_type import = find_or_create_import(other);
    std::vector<QPDFPageObjectHelper> import_pages = import->get_pages();
    check_page(other_page, static_cast<int>(import_pages.size()));

    // With fit, the form carries the other page's /Rotate and /UserUnit so it
    // is placed the way that page is displayed; without, it is the page's
    // content exactly as written.
    QPDFObjectHandle form =
      destination->copyForeignObject(import_pages.at(other_page).getFormXObjectForPage(fit));

    overlay_forms[key] = form;

    return form;
  }

  inline void docling_editor::transform_page(int page,
                                             std::int64_t expected_generation,
                                             std::array<double, 6> matrix)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    guarded([this, page, expected_generation, &matrix]() {
      check_generation(expected_generation);
      ensure_destination();

      std::vector<QPDFPageObjectHelper> pages = destination_pages();
      check_page(page, static_cast<int>(pages.size()));

      touch(false);
      pdflib::pdf_page_ops::transform(*destination, pages.at(page), matrix);
    });
  }

  inline void docling_editor::scale_page(int page,
                                         std::int64_t expected_generation,
                                         double sx,
                                         double sy)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    guarded([this, page, expected_generation, sx, sy]() {
      check_generation(expected_generation);
      ensure_destination();

      std::vector<QPDFPageObjectHelper> pages = destination_pages();
      check_page(page, static_cast<int>(pages.size()));

      touch(false);

      // Boxes first: it validates the factors before anything is changed.
      pdflib::pdf_page_ops::scale_boxes(pages.at(page), sx, sy);
      pdflib::pdf_page_ops::transform(*destination, pages.at(page), {sx, 0.0, 0.0, sy, 0.0, 0.0});
    });
  }

  inline void docling_editor::overlay_page(int page,
                                           std::int64_t expected_generation,
                                           const pdflib::pdf_source& other,
                                           int other_page,
                                           bool under,
                                           bool fit,
                                           std::array<double, 6> matrix)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    guarded([this, page, expected_generation, &other, other_page, under, fit, &matrix]() {
      check_generation(expected_generation);

      if(not other.is_valid())
        {
          throw pdflib::pdf_edit_error(pdflib::edit_error::INVALID_EDIT,
                                       "the document to overlay from is not open");
        }

      ensure_destination();

      std::vector<QPDFPageObjectHelper> pages = destination_pages();
      check_page(page, static_cast<int>(pages.size()));

      QPDFObjectHandle form = find_or_create_overlay_form(other, other_page, fit);

      touch(false);
      pdflib::pdf_page_ops::overlay(*destination, pages.at(page), form, under, fit, matrix);
    });
  }

  inline void docling_editor::flatten_page_rotation(int page, std::int64_t expected_generation)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    guarded([this, page, expected_generation]() {
      check_generation(expected_generation);
      ensure_destination();

      std::vector<QPDFPageObjectHelper> pages = destination_pages();
      check_page(page, static_cast<int>(pages.size()));

      touch(false);
      pdflib::pdf_page_ops::flatten_rotation(*destination, pages.at(page));
    });
  }

  inline int docling_editor::duplicate_page(int page, std::int64_t expected_generation, int at)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this, page, expected_generation, at]() -> int {
      check_generation(expected_generation);
      ensure_destination();

      QPDFPageDocumentHelper helper(*destination);
      std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();
      int number_of_pages = static_cast<int>(pages.size());

      check_page(page, number_of_pages);

      bool at_end = false;
      int position = insertion_position(at, number_of_pages, at_end);

      QPDFPageObjectHelper original = pages.at(page);

      // A new page object sharing the original's content and resources; an
      // edit of either page then replaces what it changes on that page only.
      QPDFPageObjectHelper copy = original.shallowCopyPage();

      touch(not at_end);

      if(at_end)
        {
          helper.addPage(copy, false);
        }
      else
        {
          helper.addPageAt(copy, true, pages.at(position));
        }

      // The copy's /Annots still lists the original's annotation objects.
      // Give it annotations and form fields of its own, as qpdf does for a
      // page that appears twice in an assembled document.
      QPDFObjectHandle annots = original.getObjectHandle().getKey("/Annots");
      if(annots.isArray() and annots.getArrayNItems() > 0)
        {
          QPDFAcroFormDocumentHelper acroform(*destination);
          acroform.fixCopiedAnnotations(copy.getObjectHandle(),
                                        original.getObjectHandle(),
                                        acroform);
        }

      return position;
    });
  }

  inline void docling_editor::remove_page(int page, std::int64_t expected_generation, bool clean)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    guarded([this, page, expected_generation, clean]() {
      check_generation(expected_generation);
      ensure_destination();

      std::vector<QPDFPageObjectHelper> pages = destination_pages();
      check_page(page, static_cast<int>(pages.size()));

      touch(true);
      pdflib::pdf_navigation_ops::drop_destinations_to(*destination,
                                                       pages.at(page).getObjectHandle().getObjGen());
      // Bookmarks and an open action leading to the page would otherwise
      // keep it in the file, unreachable and unlisted.
      pdflib::pdf_outline_ops::drop_destinations_to(*destination,
                                                    pages.at(page).getObjectHandle().getObjGen());

      QPDFPageDocumentHelper(*destination).removePage(pages.at(page));
      if(clean)
        {
          destination->replaceObject(pages.at(page).getObjectHandle().getObjGen(), QPDFObjectHandle::newNull());
        }
    });
  }

  inline void docling_editor::move_page(int from, int to, std::int64_t expected_generation)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    guarded([this, from, to, expected_generation]() {
      check_generation(expected_generation);
      ensure_destination();

      std::vector<QPDFPageObjectHelper> pages = destination_pages();
      int number_of_pages = static_cast<int>(pages.size());

      check_page(from, number_of_pages);
      check_page(to, number_of_pages);

      if(from == to)
        {
          return;
        }

      touch(true);

      QPDFPageDocumentHelper helper(*destination);
      QPDFPageObjectHelper page = pages.at(from);

      // `to` is the index the page has afterwards: take it out, then put it
      // back in front of whichever page now sits there.
      helper.removePage(page);

      std::vector<QPDFPageObjectHelper> remaining = helper.getAllPages();
      if(to >= static_cast<int>(remaining.size()))
        {
          helper.addPage(page, false);
        }
      else
        {
          helper.addPageAt(page, true, remaining.at(to));
        }
    });
  }

  inline void docling_editor::rotate_page(int page,
                                          std::int64_t expected_generation,
                                          int angle,
                                          bool relative)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    guarded([this, page, expected_generation, angle, relative]() {
      check_generation(expected_generation);
      ensure_destination();

      std::vector<QPDFPageObjectHelper> pages = destination_pages();
      check_page(page, static_cast<int>(pages.size()));

      pdflib::pdf_page_ops::rotate(pages.at(page), angle, relative);
      touch(false);
    });
  }

  inline void docling_editor::set_page_box(int page,
                                           std::int64_t expected_generation,
                                           std::string box,
                                           std::array<double, 4> rectangle)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    guarded([this, page, expected_generation, &box, &rectangle]() {
      check_generation(expected_generation);
      ensure_destination();

      std::vector<QPDFPageObjectHelper> pages = destination_pages();
      check_page(page, static_cast<int>(pages.size()));

      pdflib::pdf_page_ops::set_box(pages.at(page), box, rectangle);
      touch(false);
    });
  }

  inline int docling_editor::insert_blank_page(int at, double width, double height)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this, at, width, height]() -> int {
      ensure_destination();

      QPDFPageDocumentHelper helper(*destination);
      std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();
      int number_of_pages = static_cast<int>(pages.size());

      bool at_end = false;
      int position = insertion_position(at, number_of_pages, at_end);

      QPDFPageObjectHelper page(pdflib::pdf_page_ops::make_blank_page(*destination, width, height));

      // Appending leaves every existing index as it was; inserting does not.
      touch(not at_end);

      if(at_end)
        {
          helper.addPage(page, false);
        }
      else
        {
          helper.addPageAt(page, true, pages.at(position));
        }

      return position;
    });
  }

  inline int docling_editor::append_pages(const pdflib::pdf_source& other,
                                          std::optional<std::vector<int> > pages,
                                          int at,
                                          bool import_outline)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this, &other, &pages, at, import_outline]() -> int {
      if(not other.is_valid())
        {
          throw pdflib::pdf_edit_error(pdflib::edit_error::INVALID_EDIT,
                                       "the document to import from is not open");
        }

      ensure_destination();

      import_document_ptr_type import = find_or_create_import(other);
      std::vector<QPDFPageObjectHelper> import_pages = import->get_pages();
      int number_of_import_pages = static_cast<int>(import_pages.size());

      std::vector<int> selection;
      if(pages.has_value())
        {
          selection = pages.value();
        }
      else
        {
          for(int index = 0; index < number_of_import_pages; index++)
            {
              selection.push_back(index);
            }
        }

      for(int index : selection)
        {
          check_page(index, number_of_import_pages);
        }

      QPDFPageDocumentHelper helper(*destination);
      int number_of_pages = static_cast<int>(helper.getAllPages().size());

      bool at_end = false;
      int first = insertion_position(at, number_of_pages, at_end);

      if(selection.empty())
        {
          return first;
        }

      // Before the first change: a failure half way must still invalidate
      // the snapshot and, when inserting, the page handles.
      touch(not at_end);

      // One helper for the whole call, as qpdf's own page assembly does; its
      // constructor walks every form field of the destination.
      std::unique_ptr<QPDFAcroFormDocumentHelper> destination_acroform = nullptr;

      std::map<QPDFObjGen, int> page_map;

      int position = first;
      for(int index : selection)
        {
          QPDFPageObjectHelper import_page = import_pages.at(index);

          // where this page of the other document ends up here; a page
          // copied twice is found at its first copy
          page_map.emplace(import_page.getObjectHandle().getObjGen(), position);

          // qpdf copies a page of another QPDF into this one, and makes a
          // shallow copy when the same page is added a second time.
          std::vector<QPDFPageObjectHelper> current = helper.getAllPages();
          if(position >= static_cast<int>(current.size()))
            {
              helper.addPage(import_page, false);
            }
          else
            {
              helper.addPageAt(import_page, true, current.at(position));
            }

          // Widgets were copied with the page, but their fields still belong
          // to the other document's /AcroForm. Same repair, and same guard,
          // as pdf_decoder<DOCUMENT>::get_thread_safe_page_buffer.
          QPDFObjectHandle annots = import_page.getObjectHandle().getKey("/Annots");
          if(annots.isArray() and annots.getArrayNItems() > 0)
            {
              if(destination_acroform == nullptr)
                {
                  destination_acroform = std::make_unique<QPDFAcroFormDocumentHelper>(*destination);
                }

              QPDFPageObjectHelper new_page = helper.getAllPages().at(position);
              destination_acroform->fixCopiedAnnotations(new_page.getObjectHandle(),
                                                         import_page.getObjectHandle(),
                                                         import->get_acroform());
            }

          position += 1;
        }

      if(import_outline)
        {
          pdflib::pdf_outline_ops::import(*destination, import->get_qpdf(), page_map, 0);
        }

      return first;
    });
  }

  inline std::optional<std::string> docling_editor::snapshot_password() const
  {
    if(output_encrypted and not output_user_password.empty())
      {
        return output_user_password;
      }

    return std::nullopt;
  }

  inline void docling_editor::set_encryption(std::string user_password,
                                             std::string owner_password,
                                             pdflib::pdf_permissions permissions,
                                             std::string algorithm)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    guarded([this, &user_password, &owner_password, &permissions, &algorithm]() {
      const std::set<std::string> algorithms = {"RC4-40", "RC4-128", "AES-128", "AES-256-R5", "AES-256"};
      if(algorithms.count(algorithm) == 0)
        {
          throw pdflib::pdf_edit_error(pdflib::edit_error::INVALID_EDIT, "unknown encryption algorithm: " + algorithm);
        }
      if(permissions.get_print() < 0 or permissions.get_print() > 2)
        {
          throw pdflib::pdf_edit_error(pdflib::edit_error::INVALID_EDIT,
                                       "the print permission is 0 (full), 1 (low resolution) or 2 (none)");
        }

      if(user_password.empty() and owner_password.empty())
        {
          throw pdflib::pdf_edit_error(pdflib::edit_error::INVALID_EDIT,
                                       "encryption needs a user password, an owner password, or both");
        }

      ensure_destination();

      output_encrypted = true;
      output_algorithm = algorithm;
      output_user_password = user_password;
      output_owner_password = owner_password.empty() ? user_password : owner_password;
      output_permissions = permissions;

      touch(false);
    });
  }

  inline void docling_editor::clear_encryption()
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    guarded([this]() {
      ensure_destination();

      output_encrypted = false;
      output_user_password.clear();
      output_owner_password.clear();

      touch(false);
    });
  }

  inline std::shared_ptr<std::string> docling_editor::write_to_memory()
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this]() {
      ensure_snapshot();
      return snapshot;
    });
  }

  inline void docling_editor::write_to_file(std::string filename)
  {
    // The bytes are immutable once produced, so the file is written without
    // the lock: a slow disk does not stall other threads using this editor.
    std::shared_ptr<std::string> bytes = write_to_memory();

    std::filesystem::path target = to_path(filename);
    std::filesystem::path temporary = to_path(filename + ".tmp-" + random_suffix());

    try
      {
        {
          std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
          if(not stream.is_open())
            {
              throw std::runtime_error("could not open " + filename + " for writing");
            }

          stream.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
          stream.close();

          if(stream.fail())
            {
              throw std::runtime_error("could not write " + filename);
            }
        }

        std::filesystem::rename(temporary, target);
      }
    catch(const std::exception& exc)
      {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);

        throw pdflib::pdf_edit_error(pdflib::edit_error::WRITE, exc.what());
      }
  }

  // ---------------------------------------------------------------------------
  // Document-level content: thin, locked entry points over src/edit/*_ops.h
  // ---------------------------------------------------------------------------

  inline nlohmann::json docling_editor::document_settings()
  {
    return read_document([](QPDF& qpdf) {
      return pdflib::pdf_catalog_ops::read_settings(qpdf);
    });
  }

  inline std::vector<std::string> docling_editor::page_labels()
  {
    return read_document([](QPDF& qpdf) {
      return pdflib::pdf_catalog_ops::page_labels(qpdf);
    });
  }

  inline nlohmann::json docling_editor::named_destinations()
  {
    return read_document([](QPDF& qpdf) {
      return pdflib::pdf_catalog_ops::named_destinations(qpdf);
    });
  }

  inline nlohmann::json docling_editor::outline_tree()
  {
    return read_document([](QPDF& qpdf) {
      return pdflib::pdf_outline_ops::read(qpdf);
    });
  }

  inline nlohmann::json docling_editor::form()
  {
    return read_document([](QPDF& qpdf) {
      return pdflib::pdf_form_ops::read(qpdf);
    });
  }

  inline nlohmann::json docling_editor::page_annotations(int page, std::int64_t expected_generation)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([this, page, expected_generation]() {
      check_generation(expected_generation);

      return with_document([this, page](QPDF& qpdf) {
        std::vector<QPDFPageObjectHelper> pages = QPDFPageDocumentHelper(qpdf).getAllPages();
        check_page(page, static_cast<int>(pages.size()));

        return pdflib::pdf_annotation_ops::read(qpdf, pages.at(page));
      });
    });
  }

  inline nlohmann::json docling_editor::attachments()
  {
    return read_document([](QPDF& qpdf) {
      return pdflib::pdf_attachment_ops::read(qpdf);
    });
  }

  inline std::string docling_editor::attachment_data(std::string name)
  {
    return read_document([&name](QPDF& qpdf) {
      return pdflib::pdf_attachment_ops::data(qpdf, name);
    });
  }

  inline nlohmann::json docling_editor::javascript()
  {
    return read_document([](QPDF& qpdf) {
      return pdflib::pdf_attachment_ops::javascript(qpdf);
    });
  }

  inline void docling_editor::set_info(std::map<std::string, std::optional<std::string> > entries)
  {
    edit_document([&entries](QPDF& qpdf) {
      pdflib::pdf_catalog_ops::set_info(qpdf, entries);
    });
  }

  inline void docling_editor::set_xmp(std::optional<std::string> packet)
  {
    edit_document([&packet](QPDF& qpdf) {
      pdflib::pdf_catalog_ops::set_xmp(qpdf, packet);
    });
  }

  inline void docling_editor::set_page_layout(std::string name)
  {
    edit_document([&name](QPDF& qpdf) {
      pdflib::pdf_catalog_ops::set_page_layout(qpdf, name);
    });
  }

  inline void docling_editor::set_page_mode(std::string name)
  {
    edit_document([&name](QPDF& qpdf) {
      pdflib::pdf_catalog_ops::set_page_mode(qpdf, name);
    });
  }

  inline void docling_editor::set_viewer_preferences(std::string dictionary)
  {
    edit_document([&dictionary](QPDF& qpdf) {
      pdflib::pdf_catalog_ops::set_viewer_preferences(qpdf, dictionary);
    });
  }

  inline void docling_editor::set_open_destination(int page)
  {
    edit_document([page](QPDF& qpdf) {
      pdflib::pdf_catalog_ops::set_open_destination(qpdf, page);
    });
  }

  inline void docling_editor::set_page_label(int from,
                                             int to,
                                             std::string style,
                                             std::string prefix,
                                             int start)
  {
    edit_document([from, to, &style, &prefix, start](QPDF& qpdf) {
      pdflib::pdf_catalog_ops::set_page_label(qpdf, from, to, style, prefix, start);
    });
  }

  inline void docling_editor::add_named_destination(std::string name, int page)
  {
    edit_document([&name, page](QPDF& qpdf) {
      pdflib::pdf_catalog_ops::add_named_destination(qpdf, name, page);
    });
  }

  inline int docling_editor::add_outline_item(std::string title, int page, int parent)
  {
    return edit_document([&title, page, parent](QPDF& qpdf) {
      return pdflib::pdf_outline_ops::add_item(qpdf, title, page, parent);
    });
  }

  inline int docling_editor::set_form_values(std::map<std::string, std::string> values)
  {
    return edit_document([&values](QPDF& qpdf) {
      return pdflib::pdf_form_ops::set_values(qpdf, values);
    });
  }

  inline void docling_editor::set_need_appearances(bool value)
  {
    edit_document([value](QPDF& qpdf) {
      pdflib::pdf_form_ops::set_need_appearances(qpdf, value);
    });
  }

  inline void docling_editor::flatten_annotations()
  {
    edit_document([](QPDF& qpdf) {
      pdflib::pdf_form_ops::flatten(qpdf);
    });
  }

  inline void docling_editor::add_annotation(int page,
                                             std::int64_t expected_generation,
                                             std::string dictionary)
  {
    edit_page(page, expected_generation, [&dictionary](QPDF& qpdf, QPDFPageObjectHelper target) {
      pdflib::pdf_annotation_ops::add(qpdf, target, dictionary);
    });
  }

  inline void docling_editor::add_page_link(int page,
                                            std::int64_t expected_generation,
                                            std::array<double, 4> rectangle,
                                            int target)
  {
    edit_page(page, expected_generation, [&rectangle, target](QPDF& qpdf, QPDFPageObjectHelper source) {
      pdflib::pdf_annotation_ops::add_page_link(qpdf, source, rectangle, target);
    });
  }

  inline int docling_editor::remove_annotations(int page,
                                                std::int64_t expected_generation,
                                                std::set<std::string> subtypes)
  {
    return edit_page(page, expected_generation, [&subtypes](QPDF& qpdf, QPDFPageObjectHelper target) {
      return pdflib::pdf_annotation_ops::remove(qpdf, target, subtypes);
    });
  }

  inline void docling_editor::add_attachment(std::string name,
                                             std::string bytes,
                                             std::string description,
                                             std::string mime_type)
  {
    edit_document([&name, &bytes, &description, &mime_type](QPDF& qpdf) {
      pdflib::pdf_attachment_ops::add(qpdf, name, bytes, description, mime_type);
    });
  }

  inline bool docling_editor::remove_attachment(std::string name)
  {
    return edit_document([&name](QPDF& qpdf) {
      return pdflib::pdf_attachment_ops::remove(qpdf, name);
    });
  }

  inline void docling_editor::add_javascript(std::string name, std::string script)
  {
    edit_document([&name, &script](QPDF& qpdf) {
      pdflib::pdf_attachment_ops::add_javascript(qpdf, name, script);
    });
  }

  inline int docling_editor::remove_javascript()
  {
    return edit_document([](QPDF& qpdf) {
      return pdflib::pdf_attachment_ops::remove_javascript(qpdf);
    });
  }

  inline void docling_editor::set_output_options(bool compress_streams,
                                                 std::string object_streams,
                                                 bool linearize)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    guarded([this, compress_streams, &object_streams, linearize]() {
      qpdf_object_stream_e mode = qpdf_o_preserve;
      if(object_streams == "generate")     { mode = qpdf_o_generate; }
      else if(object_streams == "disable") { mode = qpdf_o_disable; }
      else if(object_streams != "preserve")
        {
          throw pdflib::pdf_edit_error(pdflib::edit_error::INVALID_EDIT,
                                       "object_streams is 'preserve', 'generate' or 'disable'");
        }

      ensure_destination();

      output_compress_streams = compress_streams;
      output_object_streams = mode;
      output_linearize = linearize;

      touch(false);
    });
  }

  inline int docling_editor::insert_image_page(int at,
                                               int width,
                                               int height,
                                               std::string colorspace,
                                               std::string data,
                                               bool is_jpeg,
                                               bool invert,
                                               std::string alpha,
                                               double page_width,
                                               double page_height,
                                               std::array<double, 4> placement,
                                               int rotate)
  {
    std::lock_guard<std::recursive_mutex> lock(parser_mutex);

    return guarded([&]() -> int {
      ensure_destination();

      QPDFPageDocumentHelper helper(*destination);
      std::vector<QPDFPageObjectHelper> pages = helper.getAllPages();
      int number_of_pages = static_cast<int>(pages.size());

      bool at_end = false;
      int position = insertion_position(at, number_of_pages, at_end);

      // built, and so checked, before the page list changes
      QPDFPageObjectHelper page(pdflib::pdf_page_ops::make_image_page(*destination,
                                                                      width,
                                                                      height,
                                                                      colorspace,
                                                                      data,
                                                                      is_jpeg,
                                                                      invert,
                                                                      alpha,
                                                                      page_width,
                                                                      page_height,
                                                                      placement,
                                                                      rotate));

      touch(not at_end);

      if(at_end)
        {
          helper.addPage(page, false);
        }
      else
        {
          helper.addPageAt(page, true, pages.at(position));
        }

      return position;
    });
  }

  inline void docling_editor::draw_text(int page,
                                        std::int64_t expected_generation,
                                        std::string text,
                                        double x,
                                        double y,
                                        double size,
                                        std::string font,
                                        std::array<double, 3> color)
  {
    edit_page(page, expected_generation, [&](QPDF& qpdf, QPDFPageObjectHelper target) {
      auto itr = standard_fonts.find(font);
      if(itr == standard_fonts.end())
        {
          itr = standard_fonts.emplace(font, pdflib::pdf_page_ops::make_standard_font(qpdf, font)).first;
        }

      pdflib::pdf_page_ops::draw_text(qpdf, target, itr->second, text, x, y, size, color);
    });
  }

  inline int docling_editor::add_outline_entry(nlohmann::json spec)
  {
    return edit_document([&spec](QPDF& qpdf) {
      return pdflib::pdf_outline_ops::add_item(qpdf, spec);
    });
  }

  inline void docling_editor::update_outline_item(int id, nlohmann::json changes)
  {
    edit_document([id, &changes](QPDF& qpdf) {
      pdflib::pdf_outline_ops::update_item(qpdf, id, changes);
    });
  }

  inline void docling_editor::remove_outline_item(int id)
  {
    edit_document([id](QPDF& qpdf) {
      pdflib::pdf_outline_ops::remove_item(qpdf, id);
    });
  }

  inline void docling_editor::move_outline_item(int id, int parent, int index)
  {
    edit_document([id, parent, index](QPDF& qpdf) {
      pdflib::pdf_outline_ops::move_item(qpdf, id, parent, index);
    });
  }

  inline void docling_editor::clear_outline()
  {
    edit_document([](QPDF& qpdf) {
      pdflib::pdf_outline_ops::clear(qpdf);
    });
  }

  inline void docling_editor::add_form_field(int page,
                                             std::int64_t expected_generation,
                                             nlohmann::json spec)
  {
    edit_page(page, expected_generation, [&spec](QPDF& qpdf, QPDFPageObjectHelper target) {
      pdflib::pdf_form_ops::add_field(qpdf, target, spec);
    });
  }

  inline void docling_editor::update_form_field(std::string name, nlohmann::json changes)
  {
    edit_document([&name, &changes](QPDF& qpdf) {
      pdflib::pdf_form_ops::update_field(qpdf, name, changes);
    });
  }

  inline void docling_editor::remove_form_field(std::string name)
  {
    edit_document([&name](QPDF& qpdf) {
      pdflib::pdf_form_ops::remove_field(qpdf, name);
    });
  }

}

#endif
