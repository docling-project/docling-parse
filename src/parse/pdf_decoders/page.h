//-*-C++-*-

#ifndef PDF_PAGE_DECODER_H
#define PDF_PAGE_DECODER_H

#include <optional>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <nlohmann/json.hpp>

#include <parse/qpdf/logger.h>

namespace pdflib
{

  template<>
  class pdf_decoder<PAGE>
  {
  public:

    pdf_decoder(QPDFObjectHandle page, int page_num);

    // Thread-safe constructor: creates its own QPDF document from the shared buffer
    pdf_decoder(std::shared_ptr<std::string> buffer,
                std::optional<std::string> password,
                int orig_page_num, // original page-number of the pdf
		int curr_page_num, // page number in the buffer. The buffer might only be a single pdf page
                bool keep_qpdf_warnings = false);

    ~pdf_decoder();

    int get_page_number();

    bool is_thread_safe() const { return thread_safe; }

    // Typed accessors for direct pybind11 binding
    page_item<PAGE_CELLS>& get_page_cells() { return page_cells; }
    page_item<PAGE_SHAPES>& get_page_shapes() { return page_shapes; }
    page_item<PAGE_IMAGES>& get_page_images() { return page_images; }
    page_item<PAGE_DIMENSION>& get_page_dimension() { return page_dimension; }

    page_item<PAGE_WIDGETS>& get_page_widgets() { return page_widgets; }
    page_item<PAGE_HYPERLINKS>& get_page_hyperlinks() { return page_hyperlinks; }

    // page_cells is the internal base stream used to derive words/lines.
    // char_cells is the public/exposed char output, which may be suppressed.
    page_item<PAGE_CELLS>& get_char_cells() { return char_cells; }
    page_item<PAGE_CELLS>& get_word_cells() { return word_cells; }
    page_item<PAGE_CELLS>& get_line_cells() { return line_cells; }

    bool has_word_cells() const { return word_cells_created; }
    bool has_line_cells() const { return line_cells_created; }

    // Create word/line cells from page_cells
    void create_word_cells(const decode_config& config);
    void create_line_cells(const decode_config& config);

    // JSON serialization
    nlohmann::json get(const decode_config& config);

    void decode_page(const decode_config& config);

    // Get timing information for this page
    pdf_timings& get_timings() { return timings; }
    const pdf_timings& get_timings() const { return timings; }

    // Get render instructions collected during decode
    pdf_render_instructions& get_instructions() { return instructions; }

    // Export this page as a standalone one-page PDF.
    void save_pdf_page(std::filesystem::path const& out_path) const;

  private:

    void decode_dimensions();

    // Resources
    void decode_resources(const decode_config& config);
    void decode_resources_low_level(const decode_config& config);

    void decode_grphs();

    void decode_fonts();

    void decode_colorspaces();

    void decode_xobjects();

    // Contents
    void decode_contents(const decode_config& config);

    void decode_annots_from_qpdf();
    void extract_page_items_from_annots(QPDFObjectHandle annots);

    void add_page_cell_from_annot(QPDFObjectHandle annot);
    void add_page_hyperlink_from_annot(QPDFObjectHandle annot);
    void add_page_widget_from_annot(QPDFObjectHandle annot);

    void add_textfield(QPDFObjectHandle annot, const std::array<double, 4>& bbox);
    void add_button   (QPDFObjectHandle annot, const std::array<double, 4>& bbox);
    void add_choice   (QPDFObjectHandle annot, const std::array<double, 4>& bbox);
    void add_signature(QPDFObjectHandle annot, const std::array<double, 4>& bbox);

    // Load /AcroForm/DR/Font into acroform_fonts (called once before widget processing).
    void load_acroform_dr_fonts();

    // Resolve /AP/N — a single stream, or a dictionary of appearance
    // states (checkboxes / radio buttons) selected by /AS — and decode
    // the selected stream.
    void decode_annot_appearance(QPDFObjectHandle annot, const std::array<double, 4>& bbox);

    // Parse the /AP/N appearance stream, extract cells in AP-local coords,
    // shift by bbox origin to page coords, and append to page_cells.
    void decode_ap_stream(QPDFObjectHandle ap_stream, const std::array<double, 4>& bbox);

    void rotate_contents();

    void sanitise_contents(std::string page_boundary);

  private:

    bool thread_safe;

    // Owned QPDF document (only used in thread-safe mode)
    std::shared_ptr<std::string> owned_buffer;
    std::unique_ptr<QPDF> owned_qpdf_document;

    QPDFObjectHandle qpdf_page;

    int orig_page_number;
    int curr_page_number;

    QPDFObjectHandle qpdf_resources;
    QPDFObjectHandle qpdf_grphs;
    QPDFObjectHandle qpdf_fonts;
    QPDFObjectHandle qpdf_colorspaces;
    QPDFObjectHandle qpdf_xobjects;

    // Debug-only: populated when config.populate_json_objects is true
    nlohmann::json json_page;
    nlohmann::json json_annots;

    page_item<PAGE_DIMENSION> page_dimension;

    page_item<PAGE_CELLS>  page_cells;
    page_item<PAGE_CELLS>  char_cells;
    page_item<PAGE_SHAPES> page_shapes;
    page_item<PAGE_IMAGES> page_images;

    page_item<PAGE_WIDGETS>    page_widgets;
    page_item<PAGE_HYPERLINKS> page_hyperlinks;

    page_item<PAGE_CELLS>  cells;
    page_item<PAGE_SHAPES> shapes;
    page_item<PAGE_IMAGES> images;

    // Computed cell aggregations
    page_item<PAGE_CELLS> word_cells;
    page_item<PAGE_CELLS> line_cells;
    bool word_cells_created = false;
    bool line_cells_created = false;

    std::shared_ptr<pdf_resource<PAGE_GRPHS> > page_grphs;
    std::shared_ptr<pdf_resource<PAGE_FONTS> > page_fonts;
    std::shared_ptr<pdf_resource<PAGE_COLORSPACES> > page_colorspaces;
    std::shared_ptr<pdf_resource<PAGE_XOBJECTS> > page_xobjects;

    decode_config page_config;  // saved at the start of decode_page for use in widget handlers

    // AcroForm /DR/Font — loaded once before widget processing, used as
    // fallback in decode_ap_stream when the AP stream has no /Resources/Font.
    std::shared_ptr<pdf_resource<PAGE_FONTS>> acroform_fonts;

    pdf_render_instructions instructions;

    pdf_timings timings;
  };

  pdf_decoder<PAGE>::pdf_decoder(QPDFObjectHandle page, int page_num):
    thread_safe(false),
    owned_buffer(nullptr),
    owned_qpdf_document(nullptr),
    qpdf_page(page),
    orig_page_number(page_num),
    curr_page_number(page_num),
    page_grphs(std::make_shared<pdf_resource<PAGE_GRPHS>>()),
    page_fonts(std::make_shared<pdf_resource<PAGE_FONTS>>()),
    page_colorspaces(std::make_shared<pdf_resource<PAGE_COLORSPACES>>()),
    page_xobjects(std::make_shared<pdf_resource<PAGE_XOBJECTS>>())
  {}

  pdf_decoder<PAGE>::pdf_decoder(std::shared_ptr<std::string> buffer,
                                 std::optional<std::string> password,
                                 int orig_page_num,
				 int curr_page_num,
                                 bool keep_qpdf_warnings):
    thread_safe(true),
    owned_buffer(buffer),
    owned_qpdf_document(std::make_unique<QPDF>()),
    qpdf_page(),
    orig_page_number(orig_page_num),
    curr_page_number(curr_page_num),
    page_grphs(std::make_shared<pdf_resource<PAGE_GRPHS>>()),
    page_fonts(std::make_shared<pdf_resource<PAGE_FONTS>>()),
    page_colorspaces(std::make_shared<pdf_resource<PAGE_COLORSPACES>>()),
    page_xobjects(std::make_shared<pdf_resource<PAGE_XOBJECTS>>())
  {
    std::string description = "thread-safe page " + std::to_string(orig_page_num);

    configure_qpdf_warnings(*owned_qpdf_document);
    owned_qpdf_document->setSuppressWarnings(!keep_qpdf_warnings);

    if(password.has_value())
      {
        owned_qpdf_document->processMemoryFile(description.c_str(),
                                               owned_buffer->c_str(),
                                               owned_buffer->size(),
                                               password.value().c_str());
      }
    else
      {
        owned_qpdf_document->processMemoryFile(description.c_str(),
                                               owned_buffer->c_str(),
                                               owned_buffer->size());
      }

    std::vector<QPDFObjectHandle> pages = owned_qpdf_document->getAllPages();

    if(curr_page_number < 0 || curr_page_number >= static_cast<int>(pages.size()))
      {
        LOG_S(ERROR) << "page " << curr_page_num << " is out of bounds (0-" << pages.size()-1 << ")";
        throw std::out_of_range("page number out of bounds: " + std::to_string(curr_page_number));
      }

    qpdf_page = pages.at(curr_page_number);
  }

  pdf_decoder<PAGE>::~pdf_decoder()
  {
    LOG_S(INFO) << "releasing memory for pdf page decoder";
  }

  int pdf_decoder<PAGE>::get_page_number()
  {
    return orig_page_number;
  }

  void pdf_decoder<PAGE>::save_pdf_page(std::filesystem::path const& out_path) const
  {
    std::filesystem::create_directories(out_path.parent_path());

    QPDF out_pdf;
    out_pdf.emptyPDF();

    QPDFPageDocumentHelper out_pages(out_pdf);
    QPDFPageObjectHelper source_page(qpdf_page);
    out_pages.addPage(source_page, false);

    QPDFWriter writer(out_pdf, out_path.string().c_str());
    writer.write();
  }

  nlohmann::json pdf_decoder<PAGE>::get(const decode_config& config)
  {
    bool keep_char_cells = config.keep_char_cells;
    bool keep_shapes = config.keep_shapes;
    bool keep_bitmaps = config.keep_bitmaps;
    bool do_sanitization = config.do_sanitization;

    LOG_S(INFO) << "pdf_decoder<PAGE>::get "
                << "keep_char_cells: " << keep_char_cells << ", "
                << "keep_shapes: " << keep_shapes << ", "
                << "keep_bitmaps: " << keep_bitmaps << ", "
                << "do_sanitization: " << do_sanitization << ", ";

    nlohmann::json result;
    {
      result["page_number"] = orig_page_number;

      result["annotations"] = json_annots;

      nlohmann::json& timings_ = result["timings"];
      {
        // Serialize timings as sums for backward compatibility
        auto sum_map = timings.to_sum_map();
        for(auto itr=sum_map.begin(); itr!=sum_map.end(); itr++)
          {
            timings_[itr->first] = itr->second;
          }
      }

      {
        nlohmann::json& original = result["original"];

        original["dimension"] = page_dimension.get();

        if(keep_bitmaps)
          {
            original["images"] = page_images.get();
          }
        else
          {
            LOG_S(WARNING) << "skipping the serialization of `images` to json!";
          }

        if(keep_char_cells)
          {
            original["cells"] = page_cells.get();
          }
        else
          {
            LOG_S(WARNING) << "skipping the serialization of `cells` to json!";
          }

        if(keep_shapes)
          {
            original["shapes"] = page_shapes.get();
          }
        else
          {
            LOG_S(WARNING) << "skipping the serialization of `shapes` to json!";
          }

            original["widgets"] = page_widgets.get();
        original["hyperlinks"] = page_hyperlinks.get();
      }

      if(do_sanitization)
        {
          nlohmann::json& sanitized = result["sanitized"];

          sanitized["dimension"] = page_dimension.get();

          if(keep_bitmaps)
            {
              sanitized["images"] = images.get();
            }

          if(keep_char_cells)
            {
              sanitized["cells"] = cells.get();
            }

          if(keep_shapes)
            {
              sanitized["shapes"] = shapes.get();
            }
        }
      else
        {
          LOG_S(WARNING) << "skipping the serialization of `sanitzed` page to json!";
        }
    }

    return result;
  }

  void pdf_decoder<PAGE>::decode_page(const decode_config& config)
  {
    page_config = config;

    if(owned_qpdf_document != nullptr)
      {
        owned_qpdf_document->setSuppressWarnings(!config.keep_qpdf_warnings);
      }

    utils::timer global, local;

    if(config.populate_json_objects)
      {
        local.reset();
        json_page = to_json(qpdf_page);
        timings.add_timing(pdf_timings::KEY_TO_JSON_PAGE, local.get_time());

        //LOG_S(INFO) << json_page.dump(2);
      }

    if(config.populate_json_objects)
      {
        local.reset();
        json_annots = extract_annots_in_json(qpdf_page);
        timings.add_timing(pdf_timings::KEY_EXTRACT_ANNOTS_JSON, local.get_time());

        //LOG_S(INFO) << json_annots.dump(2);
      }

    {
      local.reset();
      decode_dimensions();
      timings.add_timing(pdf_timings::KEY_DECODE_DIMENSIONS, local.get_time());
    }

    {
      local.reset();
      decode_resources(config);
      timings.add_timing(pdf_timings::KEY_DECODE_RESOURCES, local.get_time());
    }

    {
      local.reset();
      decode_contents(config);
      timings.add_timing(pdf_timings::KEY_DECODE_CONTENTS, local.get_time());
    }

    {
      local.reset();
      decode_annots_from_qpdf();
      timings.add_timing(pdf_timings::KEY_DECODE_ANNOTS, local.get_time());
    }

    {
      local.reset();
      rotate_contents();
      timings.add_timing(pdf_timings::KEY_ROTATE_CONTENTS, local.get_time());
    }

    // fix the orientation
    {
      local.reset();
      page_item_sanitator<PAGE_DIMENSION> sanitator(page_dimension);

      sanitator.sanitize(config.page_boundary); // update the top-level bbox
      sanitator.sanitize(page_cells, config.page_boundary);
      sanitator.sanitize(page_shapes, config.page_boundary);
      sanitator.sanitize(page_images, config.page_boundary);
      timings.add_timing(pdf_timings::KEY_SANITIZE_ORIENTATION, local.get_time());
    }

    {
      local.reset();
      page_item_sanitator<PAGE_CELLS> sanitator;

      {
        sanitator.remove_duplicate_cells(page_cells, 0.5, true);
      }

      {
        sanitator.sanitize_text(page_cells);
      }
      timings.add_timing(pdf_timings::KEY_SANITIZE_CELLS, local.get_time());
    }

    if(config.do_sanitization)
      {
        local.reset();
        sanitise_contents(config.page_boundary);
        timings.add_timing(pdf_timings::KEY_SANITISE_CONTENTS, local.get_time());
      }
    else
      {
        LOG_S(WARNING) << "skipping sanitization!";
      }

    if(config.keep_char_cells)
      {
        char_cells = page_cells;
      }
    else
      {
        char_cells.clear();
      }

    timings.add_timing(pdf_timings::KEY_DECODE_PAGE, global.get_time());
  }

  void pdf_decoder<PAGE>::decode_dimensions()
  {
    LOG_S(INFO) << __FUNCTION__;

    page_dimension.execute(qpdf_page);

    instructions.set_size_instruction(page_dimension.get_media_bbox(),
                                      page_dimension.get_crop_bbox());
  }

  void pdf_decoder<PAGE>::decode_resources(const decode_config& config)
  {
    LOG_S(INFO) << __FUNCTION__;

    bool has_resources = qpdf_page.hasKey("/Resources");
    bool has_parent = qpdf_page.hasKey("/Parent");

    if(has_resources and has_parent)
      {
        auto parent = qpdf_page.getKey("/Parent");
        if(parent.hasKey("/Resources"))
          {
            qpdf_resources = parent.getKey("/Resources");
            decode_resources_low_level(config);
          }
        else
          {
            LOG_S(INFO) << "parent of page has no resources!";
          }

        // This might overwrite resources from the parent ...
        qpdf_resources = qpdf_page.getKey("/Resources");
        decode_resources_low_level(config);
      }
    else if(has_resources)
      {
        qpdf_resources = qpdf_page.getKey("/Resources");
        decode_resources_low_level(config);
      }
    else if(has_parent)
      {
        auto parent = qpdf_page.getKey("/Parent");
        if(parent.hasKey("/Resources"))
          {
            qpdf_resources = parent.getKey("/Resources");

            LOG_S(INFO) << "parent of page has resources!";

            decode_resources_low_level(config);
          }
        else
          {
            LOG_S(ERROR) << "page has no /Resources nor a /Parent with /Resources.";
          }
      }
    else
      {
        LOG_S(WARNING) << "page does not have any resources!";
      }

    {
      auto font_keys = page_fonts->keys();

      LOG_S(INFO) << "fonts: " << font_keys.size();
      for(auto key:font_keys)
        {
          LOG_S(INFO) << " -> font-key: '" << key << "'";
        }
    }
  }

  void pdf_decoder<PAGE>::decode_resources_low_level(const decode_config& config)
  {
    LOG_S(INFO) << __FUNCTION__;

    if(qpdf_resources.hasKey("/ExtGState"))
      {
        qpdf_grphs = qpdf_resources.getKey("/ExtGState");
        decode_grphs();
      }
    else
      {
        LOG_S(WARNING) << "page does not have any graphics state!";
      }

    if(qpdf_resources.hasKey("/Font"))
      {
        qpdf_fonts = qpdf_resources.getKey("/Font");
        decode_fonts();
      }
    else
      {
        LOG_S(WARNING) << "page does not have any fonts!";
      }

    if(qpdf_resources.hasKey("/ColorSpace"))
      {
        qpdf_colorspaces = qpdf_resources.getKey("/ColorSpace");
        decode_colorspaces();
      }
    else
      {
        LOG_S(INFO) << "page does not have any color spaces!";
      }

    if(qpdf_resources.hasKey("/XObject"))
      {
        qpdf_xobjects = qpdf_resources.getKey("/XObject");
        decode_xobjects();
      }
    else
      {
        LOG_S(WARNING) << "page does not have any xobjects!";
      }
  }

  void pdf_decoder<PAGE>::decode_grphs()
  {
    LOG_S(INFO) << __FUNCTION__;

    page_grphs->set(qpdf_grphs, timings);
  }

  void pdf_decoder<PAGE>::decode_fonts()
  {
    LOG_S(INFO) << __FUNCTION__;

    page_fonts->set(qpdf_fonts, timings);
  }

  void pdf_decoder<PAGE>::decode_colorspaces()
  {
    LOG_S(INFO) << __FUNCTION__;

    page_colorspaces->set(qpdf_colorspaces);
  }

  void pdf_decoder<PAGE>::decode_xobjects()
  {
    LOG_S(INFO) << __FUNCTION__;

    page_xobjects->set(qpdf_xobjects, timings);
  }

  void pdf_decoder<PAGE>::decode_contents(const decode_config& config)
  {
    LOG_S(INFO) << __FUNCTION__;

    QPDFPageObjectHelper          qpdf_page_object(qpdf_page);
    std::vector<QPDFObjectHandle> contents = qpdf_page_object.getPageContents();

    pdf_decoder<STREAM> stream_decoder(config,

                                       page_dimension,
                                       page_cells,
                                       page_shapes,
                                       page_images,
                                       page_fonts,
                                       page_grphs,
                                       page_colorspaces,
                                       page_xobjects,
                                       instructions,
                                       timings);

    int cnt = 0;

    // Split decode_contents into: page content-stream tokenization
    // (content_decode_total) vs. operator-execution self-time
    // (interprete_ops_total). The latter is the interpretation wall time minus
    // the sub-work attributed to other buckets (resource set(), parse_stream,
    // do_image, do_form machinery) while interpreting -- see note_attributed().
    double interprete_seconds = 0.0;
    double attributed_before  = timings.attributed_total();

    std::vector<qpdf_stream_instruction> parameters;
    for(auto content:contents)
      {
        LOG_S(INFO) << "--------------- start decoding content stream (" << (cnt++) << ")... ---------------";

        {
          utils::timer content_decode_timer;
          stream_decoder.decode(content);
          timings.add_timing(pdf_timings::KEY_CONTENT_DECODE_TOTAL, content_decode_timer.get_time());
        }
        //stream_decoder.print();

        {
          utils::timer interprete_timer;
          stream_decoder.interprete(parameters);
          interprete_seconds += interprete_timer.get_time();
        }

        if(parameters.size()>0)
          {
            LOG_S(WARNING) << "stream is ending with non-zero number of parameters";
          }
      }

    double attributed_during = timings.attributed_total() - attributed_before;
    timings.add_timing(pdf_timings::KEY_INTERPRETE_OPS_TOTAL,
                       interprete_seconds - attributed_during);
  }

  void pdf_decoder<PAGE>::load_acroform_dr_fonts()
  {
    LOG_S(INFO) << __FUNCTION__;

    // page_fonts is already fully populated (decode_fonts ran before us).
    // Make it the base of the chain so AP streams can fall back to page-level
    // fonts (e.g. /F2) without any re-parsing.
    acroform_fonts = std::make_shared<pdf_resource<PAGE_FONTS>>(page_fonts);

    try
      {
        // Reach the document root regardless of thread-safe vs shared mode.
        QPDF* qpdf_ptr = nullptr;
        if(thread_safe and owned_qpdf_document)
          {
            qpdf_ptr = owned_qpdf_document.get();
          }
        else
          {
            qpdf_ptr = qpdf_page.getOwningQPDF();
          }

        if(not qpdf_ptr) { return; }

        auto root = qpdf_ptr->getRoot();
        if(not root.hasKey("/AcroForm")) { return; }

        auto acroform = root.getKey("/AcroForm");
        if(not acroform.isDictionary() or not acroform.hasKey("/DR")) { return; }

        auto dr = acroform.getKey("/DR");
        if(not dr.isDictionary() or not dr.hasKey("/Font")) { return; }

        auto dr_font_dict = dr.getKey("/Font");
        acroform_fonts->set(dr_font_dict, timings);

        LOG_S(INFO) << "loaded " << acroform_fonts->size() << " AcroForm /DR font(s)";
      }
    catch(const std::exception& e)
      {
        LOG_S(WARNING) << "load_acroform_dr_fonts failed: " << e.what();
      }
  }

  void pdf_decoder<PAGE>::decode_annots_from_qpdf()
  {
    LOG_S(INFO) << __FUNCTION__;

    load_acroform_dr_fonts();

    if(not qpdf_page.isDictionary())
      {
        return;
      }

    if(qpdf_page.hasKey("/Annot"))
      {
        LOG_S(INFO) << "found `/Annot`";
        QPDFObjectHandle annot = qpdf_page.getKey("/Annot");
        if(annot.isNull())
          {
            LOG_S(WARNING) << "`/Annot` key exists but resolves to null, skipping";
          }
        else
          {
            auto annot_json = to_json(annot);
            LOG_S(INFO) << "annot: " << annot_json.dump(2);

            extract_page_items_from_annots(annot);
          }
      }

    if(qpdf_page.hasKey("/Annots"))
      {
        LOG_S(INFO) << "found `/Annots`";
        QPDFObjectHandle annots = qpdf_page.getKey("/Annots");
        if(annots.isNull())
          {
            LOG_S(WARNING) << "`/Annots` key exists but resolves to null, skipping";
          }
        else
          {
            extract_page_items_from_annots(annots);
          }
      }
  }

  // FIXME: we need to expand the capabilities of the annotation extraction!
  void pdf_decoder<PAGE>::extract_page_items_from_annots(QPDFObjectHandle annots)
  {
    LOG_S(INFO) << __FUNCTION__;

    if(not annots.isArray())
      {
        LOG_S(WARNING) << "annotation is not an array";
        return;
      }

    for(int l=0; l<annots.getArrayNItems(); l++)
      {
        QPDFObjectHandle annot = annots.getArrayItem(l);

        if(annot.isString())
          {
            auto annots_json = to_json(annots);
            LOG_S(WARNING) << "skipping annot, it is a string: " << annots_json.dump(2);
            continue;
          }

        if(not annot.isDictionary())
          {
            LOG_S(WARNING) << "skipping annot, not of type `dict`!";
            continue;
          }

        // auto annot_json = to_json(annot);
        // LOG_S(INFO) << "annot " << l << ": " << annot_json.dump(2);

        auto [has_type, type] = to_string(annot, "/Type");
        if((not has_type) or (type!="/Annot"))
          {
            continue;
          }

        auto [has_subtype, subtype] = to_string(annot, "/Subtype");
        if(not has_subtype)
          {
            continue;
          }

        // LOG_S(INFO) << "type: " << type << ", subtype: " << subtype;

        /*
          if(subtype=="/Widget" and
          annot.hasKey("/Rect") and
          annot.getKey("/Rect").isArray() and
          annot.hasKey("/V") and
          annot.hasKey("/T")
          )
          {
          add_page_cell_from_annot(annot);
          }
          else*/
        if(subtype=="/Link" and
           annot.hasKey("/Rect") and
           annot.getKey("/Rect").isArray() and
           annot.hasKey("/A")
           )
          {
            add_page_hyperlink_from_annot(annot);
          }
        else if(subtype=="/Widget" and
                annot.hasKey("/Rect") and
                annot.getKey("/Rect").isArray()
                )
          {
            add_page_widget_from_annot(annot);
          }
        else
          {
            LOG_S(WARNING) << "annot is being skipped!";
          }
      }
  }

  void pdf_decoder<PAGE>::add_page_cell_from_annot(QPDFObjectHandle annot)
  {
    auto rect = annot.getKey("/Rect");

    std::array<double, 4> bbox = {0., 0., 0., 0.};
    for(int l=0; l<rect.getArrayNItems() and l<bbox.size(); l++)
      {
        QPDFObjectHandle num = rect.getArrayItem(l);
        if(num.isNumber())
          {
            bbox[l] = utils::numeric::locale_safe_numeric_value(num);
          }
      }

    auto [has_value, text] = to_inherited_string(annot, "/V");
    if(not has_value)
      {
        text = "<unknown>";
      }

    page_item<PAGE_CELL> cell;
    {
      cell.widget = true;

      cell.x0 = bbox[0];
      cell.y0 = bbox[1];
      cell.x1 = bbox[2];
      cell.y1 = bbox[3];

      cell.r_x0 = bbox[0];
      cell.r_y0 = bbox[1];
      cell.r_x1 = bbox[2];
      cell.r_y1 = bbox[1];
      cell.r_x2 = bbox[2];
      cell.r_y2 = bbox[3];
      cell.r_x3 = bbox[0];
      cell.r_y3 = bbox[3];

      cell.text = text;
      cell.rendering_mode = 0;

      cell.space_width = 0;
      //cell.chars  = {};//chars;
      //cell.widths = {};//widths;

      cell.enc_name = "Form-font"; //font.get_encoding_name();

      cell.font_enc = "Form-font"; //to_string(font.get_encoding());
      cell.font_key = "Form-font"; //font.get_key();

      cell.font_name = "Form-font"; //font.get_name();
      cell.font_size = 0; //font_size/1000.0;

      cell.italic = false;
      cell.bold   = false;

      cell.ocr        = false;
      cell.confidence = -1.0;

      cell.stack_size  = -1;
      cell.block_count = -1;
      cell.instr_count = -1;
    }
    page_cells.push_back(cell);

  }

  void pdf_decoder<PAGE>::add_page_hyperlink_from_annot(QPDFObjectHandle annot)
  {
    LOG_S(INFO) << __FUNCTION__;

    auto rect = annot.getKey("/Rect");

    std::array<double, 4> bbox = {0., 0., 0., 0.};
    for(int l=0; l<rect.getArrayNItems() and l<bbox.size(); l++)
      {
        QPDFObjectHandle num = rect.getArrayItem(l);
        if(num.isNumber())
          {
            bbox[l] = utils::numeric::locale_safe_numeric_value(num);
          }
      }

    std::string uri = "";
    QPDFObjectHandle action = annot.getKey("/A");
    if(action.isDictionary())
      {
        auto [has_s, s_val] = to_string(action, "/S");
        if(has_s and s_val=="/URI")
          {
            auto [has_uri, uri_val] = to_string(action, "/URI");
            if(has_uri)
              {
                uri = uri_val;
              }
          }
      }

    page_item<PAGE_HYPERLINK> hyperlink;
    {
      hyperlink.x0 = bbox[0];
      hyperlink.y0 = bbox[1];
      hyperlink.x1 = bbox[2];
      hyperlink.y1 = bbox[3];

      hyperlink.uri = uri;
    }
    page_hyperlinks.push_back(hyperlink);
  }

  void pdf_decoder<PAGE>::add_page_widget_from_annot(QPDFObjectHandle annot)
  {
    LOG_S(INFO) << __FUNCTION__;

    auto rect = annot.getKey("/Rect");

    std::array<double, 4> bbox = {0., 0., 0., 0.};
    for(int l=0; l<rect.getArrayNItems() and l<bbox.size(); l++)
      {
        QPDFObjectHandle num = rect.getArrayItem(l);
        if(num.isNumber())
          {
            bbox[l] = utils::numeric::locale_safe_numeric_value(num);
          }
      }

    auto [has_value, ft_str] = to_inherited_string(annot, "/FT");
    if(not has_value)
      {
        ft_str = "";
      }

    if(ft_str=="/Tx")
      {
        add_textfield(annot, bbox);
      }
    else if(ft_str=="/Btn")
      {
        add_button(annot, bbox);
      }
    else if(ft_str=="/Ch")
      {
        add_choice(annot, bbox);
      }
    else if(ft_str=="/Sig")
      {
        add_signature(annot, bbox);
      }
    else
      {
        LOG_S(WARNING) << "undefined ft: " << ft_str;
      }

  }

  void pdf_decoder<PAGE>::add_textfield(QPDFObjectHandle annot,
                                        const std::array<double, 4>& bbox)
  {
    LOG_S(INFO) << __FUNCTION__;

    auto [has_value, text] = to_inherited_string(annot, "/V");
    if(not has_value)
      {
        text = "";
      }

    auto [has_field_name, field_name] = to_inherited_string(annot, "/T");
    if(not has_field_name)
      {
        field_name = "";
      }

    auto [has_field_type, field_type] = to_inherited_string(annot, "/FT");
    if(not has_field_type)
      {
        field_type = "";
      }

    page_item<PAGE_WIDGET> widget;
    {
      widget.name = TEXT_FIELD;

      widget.x0 = bbox[0];
      widget.y0 = bbox[1];
      widget.x1 = bbox[2];
      widget.y1 = bbox[3];

      widget.text       = text;
      widget.field_name = field_name;
      widget.field_type = field_type;
    }
    page_widgets.push_back(widget);

    // Emit a render instruction so the renderer draws a light-blue rectangle
    // over the widget area.
    {
      text_widget_instruction winstr(text,
                                     bbox[0], bbox[1],
                                     bbox[2], bbox[3],
                                     bbox[0], bbox[1],
                                     bbox[2], bbox[1],
                                     bbox[2], bbox[3],
                                     bbox[0], bbox[3]);
      instructions.add_widget_instruction(std::move(winstr));
    }

    // Parse /AP/N (Normal appearance stream) to extract the actual rendered
    // text cells positioned within the widget bounding box.
    decode_annot_appearance(annot, bbox);
  }

  void pdf_decoder<PAGE>::decode_annot_appearance(QPDFObjectHandle annot,
                                                  const std::array<double, 4>& bbox)
  {
    if(not annot.hasKey("/AP")) { return; }

    auto ap = annot.getKey("/AP");
    if(not ap.isDictionary() or not ap.hasKey("/N")) { return; }

    auto normal = ap.getKey("/N");
    if(normal.isStream())
      {
        decode_ap_stream(normal, bbox);
        return;
      }

    // Checkboxes and radio buttons carry one appearance stream per state
    // (e.g. /Off, /1); /AS selects the active one. /Off states typically
    // have an empty (or missing) appearance, which decodes to nothing.
    if(normal.isDictionary())
      {
        auto [has_state, state] = to_string(annot, "/AS");
        if(has_state and normal.hasKey(state) and normal.getKey(state).isStream())
          {
            decode_ap_stream(normal.getKey(state), bbox);
          }
      }
  }

  void pdf_decoder<PAGE>::add_button(QPDFObjectHandle annot,
                                     const std::array<double, 4>& bbox)
  {
    LOG_S(INFO) << __FUNCTION__;

    auto [has_value, text] = to_inherited_string(annot, "/V");
    if(not has_value)
      {
        text = "";
      }

    auto [has_field_name, field_name] = to_inherited_string(annot, "/T");
    if(not has_field_name)
      {
        field_name = "";
      }

    auto [has_field_type, field_type] = to_inherited_string(annot, "/FT");
    if(not has_field_type)
      {
        field_type = "";
      }

    page_item<PAGE_WIDGET> widget;
    {
      widget.name = BUTTON;

      widget.x0 = bbox[0];
      widget.y0 = bbox[1];
      widget.x1 = bbox[2];
      widget.y1 = bbox[3];

      widget.text       = text;
      widget.field_name = field_name;
      widget.field_type = field_type;
    }
    page_widgets.push_back(widget);

    // Draw the active appearance state (check mark, radio dot, ...).
    decode_annot_appearance(annot, bbox);
  }

  void pdf_decoder<PAGE>::add_choice(QPDFObjectHandle annot,
                                     const std::array<double, 4>& bbox)
  {
    LOG_S(INFO) << __FUNCTION__;

    auto [has_value, text] = to_inherited_string(annot, "/V");
    if(not has_value)
      {
        text = "";
      }

    auto [has_field_name, field_name] = to_inherited_string(annot, "/T");
    if(not has_field_name)
      {
        field_name = "";
      }

    auto [has_field_type, field_type] = to_inherited_string(annot, "/FT");
    if(not has_field_type)
      {
        field_type = "";
      }

    page_item<PAGE_WIDGET> widget;
    {
      widget.name = CHOICE;

      widget.x0 = bbox[0];
      widget.y0 = bbox[1];
      widget.x1 = bbox[2];
      widget.y1 = bbox[3];

      widget.text       = text;
      widget.field_name = field_name;
      widget.field_type = field_type;
    }
    page_widgets.push_back(widget);

    decode_annot_appearance(annot, bbox);
  }

  void pdf_decoder<PAGE>::add_signature(QPDFObjectHandle annot,
                                        const std::array<double, 4>& bbox)
  {
    LOG_S(INFO) << __FUNCTION__;

    auto [has_value, text] = to_inherited_string(annot, "/V");
    if(not has_value)
      {
        text = "";
      }

    auto [has_field_name, field_name] = to_inherited_string(annot, "/T");
    if(not has_field_name)
      {
        field_name = "";
      }

    auto [has_field_type, field_type] = to_inherited_string(annot, "/FT");
    if(not has_field_type)
      {
        field_type = "";
      }

    page_item<PAGE_WIDGET> widget;
    {
      widget.name = SIGNATURE;

      widget.x0 = bbox[0];
      widget.y0 = bbox[1];
      widget.x1 = bbox[2];
      widget.y1 = bbox[3];

      widget.text       = text;
      widget.field_name = field_name;
      widget.field_type = field_type;
    }
    page_widgets.push_back(widget);

    decode_annot_appearance(annot, bbox);
  }

  void pdf_decoder<PAGE>::decode_ap_stream(QPDFObjectHandle ap_stream,
                                           const std::array<double, 4>& bbox)
  {
    LOG_S(INFO) << __FUNCTION__;

    if(not ap_stream.isStream())
      {
        LOG_S(WARNING) << "AP/N is not a stream, skipping";
        return;
      }

    // Font fallback chain (built once in load_acroform_dr_fonts, reused here):
    //   ap_fonts  (AP stream's own /Resources/Font — most specific)
    //     → acroform_fonts  (AcroForm /DR/Font, e.g. /Helv)
    //       → page_fonts    (page-level fonts, e.g. /F2)
    //
    // The color spaces chain the same way: AP /Resources/ColorSpace → page.
    //
    // No re-parsing: page_fonts and acroform_fonts are already populated.
    //
    // hasKey/getKey operate on the stream *dictionary*, never on the stream
    // handle itself — calling them on the stream silently returns false/null.
    auto ap_fonts = std::make_shared<pdf_resource<PAGE_FONTS>>(acroform_fonts);
    auto ap_colorspaces = std::make_shared<pdf_resource<PAGE_COLORSPACES>>(page_colorspaces);
    auto ap_dict = ap_stream.getDict();
    if(ap_dict.isDictionary() and ap_dict.hasKey("/Resources"))
      {
        auto ap_resources = ap_dict.getKey("/Resources");
        if(ap_resources.isDictionary() and ap_resources.hasKey("/Font"))
          {
            auto ap_font_dict = ap_resources.getKey("/Font");
            ap_fonts->set(ap_font_dict, timings);
          }
        if(ap_resources.isDictionary() and ap_resources.hasKey("/ColorSpace"))
          {
            auto ap_colorspace_dict = ap_resources.getKey("/ColorSpace");
            ap_colorspaces->set(ap_colorspace_dict);
          }
      }

    // Temporary containers — the cells and shapes are merged into the page
    // containers below; the rest is discarded after this call.
    page_item<PAGE_DIMENSION> ap_dimension;
    page_item<PAGE_CELLS>     ap_cells;
    page_item<PAGE_SHAPES>    ap_shapes;
    page_item<PAGE_IMAGES>    ap_images;
    pdf_render_instructions   ap_instructions;

    pdf_decoder<STREAM> stream_decoder(page_config,
                                       ap_dimension,
                                       ap_cells,
                                       ap_shapes,
                                       ap_images,
                                       ap_fonts,
                                       page_grphs,
                                       ap_colorspaces,
                                       page_xobjects,
                                       ap_instructions,
                                       timings);

    std::vector<qpdf_stream_instruction> parameters;
    stream_decoder.decode(ap_stream);
    stream_decoder.interprete(parameters);

    // The AP stream uses a local coordinate system whose origin is the
    // bottom-left corner of the widget /Rect.  Shift every cell and shape
    // by (bbox[0], bbox[1]) to bring it into page coordinate space.
    const double ox = bbox[0];
    const double oy = bbox[1];

    // Shapes drawn by the appearance stream (field borders, backgrounds,
    // check marks drawn as paths, ...): keep them in the parsed output and
    // re-emit them for the renderer before the text cells, so the field
    // value stays on top of background fills.
    const std::array<double, 9> ap_shift = {1.0, 0.0, 0.0,
                                            0.0, 1.0, 0.0,
                                            ox,  oy,  1.0};
    for(auto& shape : ap_shapes)
      {
        shape.transform(ap_shift);
        page_shapes.push_back(shape);
      }

    for(const auto& shape_instr : ap_instructions.get_shape_instructions())
      {
        instructions.add_shape_instruction(shape_instr.translated(ox, oy));
      }

    // Re-emit the text instructions of the sub-decode in page coordinates
    // so the renderer draws the glyphs on top of the widget rect; the
    // translated copies keep the fill color, embedded font program and
    // char codes that a reconstruction from the cells would lose.
    for(const auto& text_instr : ap_instructions.get_text_instructions())
      {
        instructions.add_text_instruction(text_instr.translated(ox, oy));
      }

    for(auto& cell : ap_cells)
      {
        cell.x0  += ox;  cell.y0  += oy;
        cell.x1  += ox;  cell.y1  += oy;
        cell.r_x0 += ox; cell.r_y0 += oy;
        cell.r_x1 += ox; cell.r_y1 += oy;
        cell.r_x2 += ox; cell.r_y2 += oy;
        cell.r_x3 += ox; cell.r_y3 += oy;
        cell.widget = true;
        page_cells.push_back(cell);
      }

    LOG_S(INFO) << "AP stream yielded " << ap_cells.size() << " cell(s) and "
                << ap_shapes.size() << " shape(s) for widget";
  }

  void pdf_decoder<PAGE>::rotate_contents()
  {
    LOG_S(INFO) << __FUNCTION__;

    int angle = page_dimension.get_angle();

    if((angle%360)==0)
      {
        return;
      }
    else if((angle%90)!=0)
      {
        LOG_S(ERROR) << "the /Rotate angle should be a multiple of 90 ...";
      }

    // see Table 30
    LOG_S(WARNING) << "rotating contents clock-wise with angle: " << angle;

    std::pair<double, double> delta = page_dimension.rotate(angle);
    LOG_S(INFO) << "translation delta: " << delta.first << ", " << delta.second;

    page_cells.rotate(angle, delta);
    page_shapes.rotate(angle, delta);
    page_images.rotate(angle, delta);
    page_widgets.rotate(angle, delta);
    page_hyperlinks.rotate(angle, delta);
  }

  void pdf_decoder<PAGE>::sanitise_contents(std::string page_boundary)
  {
    LOG_S(INFO) << __FUNCTION__;

    {
      shapes = page_shapes;
    }

    {
      images = page_images;
    }

    // sanitise the cells
    {
      page_item_sanitator<PAGE_CELLS> sanitator;

      //sanitator.remove_duplicate_chars(page_cells, 0.5);
      //sanitator.sanitize_text(page_cells);

      cells = page_cells;

      double horizontal_cell_tolerance=1.0;
      bool enforce_same_font=true;
      //double space_width_factor_for_merge=1.5;
      double space_width_factor_for_merge=1.0;
      double space_width_factor_for_merge_with_space=0.33;

      sanitator.sanitize_bbox(cells,
                              horizontal_cell_tolerance,
                              enforce_same_font,
                              space_width_factor_for_merge,
                              space_width_factor_for_merge_with_space);

      //sanitator.sanitize_text(cells);

      LOG_S(INFO) << "#-page-cells: " << page_cells.size();
      LOG_S(INFO) << "#-sani-cells: " << cells.size();
    }
  }

  void pdf_decoder<PAGE>::create_word_cells(const decode_config& config)
  {
    LOG_S(INFO) << __FUNCTION__;
    utils::timer timer;

    page_item_sanitator<PAGE_CELLS> sanitizer;

    word_cells = sanitizer.create_word_cells(page_cells, config);

    // Remove duplicates (quadratic but necessary)
    sanitizer.remove_duplicate_cells(word_cells, 0.5, true);

    word_cells_created = true;

    LOG_S(INFO) << "#-page-cells: " << page_cells.size() << " -> #-word-cells: " << word_cells.size();
    timings.add_timing(pdf_timings::KEY_CREATE_WORD_CELLS, timer.get_time());
  }

  void pdf_decoder<PAGE>::create_line_cells(const decode_config& config)
  {
    LOG_S(INFO) << __FUNCTION__;
    utils::timer timer;

    page_item_sanitator<PAGE_CELLS> sanitizer;

    line_cells = sanitizer.create_line_cells(page_cells, config);

    // Remove duplicates (quadratic but necessary)
    sanitizer.remove_duplicate_cells(line_cells, 0.5, true);

    line_cells_created = true;

    LOG_S(INFO) << "#-page-cells: " << page_cells.size() << " -> #-line-cells: " << line_cells.size();
    timings.add_timing(pdf_timings::KEY_CREATE_LINE_CELLS, timer.get_time());
  }

}

#endif
