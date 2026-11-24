//-*-C++-*-

#ifndef PYBIND_EDIT_BINDINGS_H
#define PYBIND_EDIT_BINDINGS_H

#include <array>
#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <pybind/utils/pybind11_json.h>

#include <pybind/docling_editor.h>
#include <pybind/pdf_reader.h>
#include <pybind/pdf_writer.h>

// Python bindings of docling_editor, pdf_reader and pdf_writer.
//
// The three classes are bound as unrelated Python types: pdf_reader and
// pdf_writer hide their base on purpose, so there is no Python inheritance to
// declare. The binding functions are templates over the bound class, which is
// what keeps one definition per operation.
//
// Every binding converts its arguments with the GIL held, releases the GIL for
// the native call, and builds the Python result after taking it back. Native
// code therefore never waits for the GIL while it holds the editor's mutex.

namespace docling
{
  namespace edit_bindings
  {
    inline pybind11::tuple box_to_tuple(const pdflib::pdf_page_info::box_type& box)
    {
      return pybind11::make_tuple(box[0], box[1], box[2], box[3]);
    }

    inline pybind11::dict document_info_to_dict(const pdflib::pdf_document_info& info)
    {
      pybind11::dict result;

      result["pdf_version"] = info.get_pdf_version();
      result["extension_level"] = info.get_extension_level();
      result["number_of_pages"] = info.get_number_of_pages();

      pybind11::dict entries;
      for(const auto& entry : info.get_info())
        {
          entries[pybind11::str(entry.first)] = pybind11::str(entry.second);
        }
      result["info"] = entries;

      if(info.get_xmp().has_value())
        {
          // XMP is a byte stream whose encoding its own header declares.
          result["xmp"] = pybind11::bytes(info.get_xmp().value());
        }
      else
        {
          result["xmp"] = pybind11::none();
        }

      const pdflib::pdf_encryption_info& encryption = info.get_encryption();
      if(encryption.is_encrypted())
        {
          pybind11::dict values;
          values["revision"] = encryption.get_revision();
          values["version"] = encryption.get_version();
          values["permissions"] = encryption.get_permissions();
          values["stream_method"] = encryption.get_stream_method();
          values["string_method"] = encryption.get_string_method();
          values["file_method"] = encryption.get_file_method();
          result["encryption"] = values;
        }
      else
        {
          result["encryption"] = pybind11::none();
        }

      return result;
    }

    inline pybind11::dict page_info_to_dict(const pdflib::pdf_page_info& info)
    {
      pybind11::dict result;

      result["media_box"] = box_to_tuple(info.get_media_box());
      result["crop_box"] = box_to_tuple(info.get_crop_box());
      result["bleed_box"] = box_to_tuple(info.get_bleed_box());
      result["trim_box"] = box_to_tuple(info.get_trim_box());
      result["art_box"] = box_to_tuple(info.get_art_box());

      result["rotation"] = info.get_rotation();
      result["user_unit"] = info.get_user_unit();

      return result;
    }

    inline pybind11::list images_to_list(const std::vector<pdflib::pdf_exported_image>& images)
    {
      pybind11::list result;

      for(const pdflib::pdf_exported_image& image : images)
        {
          pybind11::dict item;

          item["name"] = image.get_name();
          item["bbox"] = box_to_tuple(image.get_bbox());
          item["width"] = image.get_width();
          item["height"] = image.get_height();
          item["format"] = image.get_format();
          item["mode"] = image.get_mode();

          const std::vector<unsigned char>& data = image.get_data();
          item["data"] = pybind11::bytes(reinterpret_cast<const char*>(data.data()), data.size());

          result.append(item);
        }

      return result;
    }

    // Lifecycle and inspection: bound on all three classes.
    template<typename class_type>
    void bind_inspection(pybind11::class_<class_type>& cls)
    {
      cls
        .def(pybind11::init<const std::string&>(),
             pybind11::arg("loglevel") = "fatal")

        .def("open_file",
             [](class_type& self, const std::string& filename, std::optional<std::string> password,
                const std::vector<std::string>& alternatives) -> bool {
               pybind11::gil_scoped_release release;
               return self.open_file(filename, password, alternatives);
             },
             pybind11::arg("filename"),
             pybind11::arg("password") = pybind11::none(),
             pybind11::arg("alternatives") = std::vector<std::string>{},
             "Open a PDF file. Returns False on failure; see take_open_error().")

        .def("open_bytes",
             [](class_type& self, pybind11::bytes data, std::optional<std::string> password,
                const std::vector<std::string>& alternatives) -> bool {
               auto buffer = std::make_shared<std::string>(data.cast<std::string>());

               pybind11::gil_scoped_release release;
               return self.open_buffer(buffer, password, alternatives);
             },
             pybind11::arg("data"),
             pybind11::arg("password") = pybind11::none(),
             pybind11::arg("alternatives") = std::vector<std::string>{},
             "Open a PDF held in a bytes object. Returns False on failure; see take_open_error().")

        .def("take_open_error",
             [](class_type& self) -> std::map<std::string, std::string> {
               pybind11::gil_scoped_release release;
               return self.take_open_error();
             },
             "Return and clear why the last open failed: {'category': ..., 'message': ...}.")

        .def("is_open", [](class_type& self) -> bool {
          pybind11::gil_scoped_release release;
          return self.is_open();
        })

        .def("close",
             [](class_type& self) -> void {
               pybind11::gil_scoped_release release;
               self.close();
             })

        .def("page_count",
             [](class_type& self) -> int {
               pybind11::gil_scoped_release release;
               return self.page_count();
             })

        .def("generation",
             [](class_type& self) -> std::int64_t {
               pybind11::gil_scoped_release release;
               return self.generation();
             },
             "Changes whenever existing page indices may have changed meaning.")

        .def("page_list_state",
             [](class_type& self) {
               pybind11::gil_scoped_release release;
               return self.page_list_state();
             })

        .def("validate_page_handle",
             [](class_type& self, int page, std::int64_t generation) {
               pybind11::gil_scoped_release release;
               self.validate_page_handle(page, generation);
             })

        .def("document_info",
             [](class_type& self) -> pybind11::dict {
               pdflib::pdf_document_info info;
               {
                 pybind11::gil_scoped_release release;
                 info = self.document_info();
               }
               return document_info_to_dict(info);
             })

        .def("page_info",
             [](class_type& self, int page, std::int64_t generation) -> pybind11::dict {
               pdflib::pdf_page_info info;
               {
                 pybind11::gil_scoped_release release;
                 info = self.page_info(page, generation);
               }
               return page_info_to_dict(info);
             },
             pybind11::arg("page"),
             pybind11::arg("generation") = class_type::ANY_GENERATION)

        .def("validate_strict", [](class_type& self) {
          pybind11::gil_scoped_release release;
          self.validate_strict();
        })

        .def("extract_text",
             [](class_type& self, int page, std::int64_t generation, bool layout) -> std::string {
               pybind11::gil_scoped_release release;
               return self.extract_text(page, generation, layout);
             },
             pybind11::arg("page"),
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             pybind11::arg("layout") = false)

        .def("page_images",
             [](class_type& self, int page, std::int64_t generation) -> pybind11::list {
               std::vector<pdflib::pdf_exported_image> images;
               {
                 pybind11::gil_scoped_release release;
                 images = self.page_images(page, generation);
               }
               return images_to_list(images);
             },
             pybind11::arg("page"),
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             "The images the page draws, in drawing order, each as a dict with its bytes.")

        .def("document_settings",
             [](class_type& self) -> pybind11::object {
               nlohmann::json result;
               {
                 pybind11::gil_scoped_release release;
                 result = self.document_settings();
               }
               return pyjson::from_json(result);
             },
             "Page layout, page mode, viewer preferences and open destination.")

        .def("page_labels",
             [](class_type& self) -> std::vector<std::string> {
               pybind11::gil_scoped_release release;
               return self.page_labels();
             },
             "The label of every page, as a viewer shows it.")

        .def("named_destinations",
             [](class_type& self) -> pybind11::object {
               nlohmann::json result;
               {
                 pybind11::gil_scoped_release release;
                 result = self.named_destinations();
               }
               return pyjson::from_json(result);
             },
             "Named destinations: {name: {page, kind, arguments}}.")

        .def("outline_tree",
             [](class_type& self) -> pybind11::object {
               nlohmann::json result;
               {
                 pybind11::gil_scoped_release release;
                 result = self.outline_tree();
               }
               return pyjson::from_json(result);
             },
             "The outline as [{id, title, page, children}].")

        .def("form",
             [](class_type& self) -> pybind11::object {
               nlohmann::json result;
               {
                 pybind11::gil_scoped_release release;
                 result = self.form();
               }
               return pyjson::from_json(result);
             },
             "The interactive form: flags and fields.")

        .def("form_flags", [](class_type& self) {
          pybind11::gil_scoped_release release;
          return self.form_flags();
        })
        .def("field_pages", [](class_type& self, const std::string& name) {
          pybind11::gil_scoped_release release;
          return self.field_pages(name);
        })
        .def("has_form_on_page", [](class_type& self, int page) {
          pybind11::gil_scoped_release release;
          return self.has_form_on_page(page);
        })

        .def("page_annotations",
             [](class_type& self, int page, std::int64_t generation) -> pybind11::object {
               nlohmann::json result;
               {
                 pybind11::gil_scoped_release release;
                 result = self.page_annotations(page, generation);
               }
               return pyjson::from_json(result);
             },
             pybind11::arg("page"),
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             "The annotations of a page, one dict each.")

        .def("attachments",
             [](class_type& self) -> pybind11::object {
               nlohmann::json result;
               {
                 pybind11::gil_scoped_release release;
                 result = self.attachments();
               }
               return pyjson::from_json(result);
             },
             "Embedded files: [{name, filename, description, size, ...}].")

        .def("attachment_data",
             [](class_type& self, const std::string& name) -> pybind11::bytes {
               std::string data;
               {
                 pybind11::gil_scoped_release release;
                 data = self.attachment_data(name);
               }
               return pybind11::bytes(data);
             },
             pybind11::arg("name"),
             "The bytes of one embedded file.")

        .def("javascript",
             [](class_type& self) -> pybind11::object {
               nlohmann::json result;
               {
                 pybind11::gil_scoped_release release;
                 result = self.javascript();
               }
               return pyjson::from_json(result);
             },
             "Document-level scripts: {name: script}.");
    }

    // open_source(other): one overload per kind of document that can be read
    // from. The other document only hands over its immutable bytes.
    template<typename class_type, typename other_type>
    void bind_open_source(pybind11::class_<class_type>& cls)
    {
      cls.def("open_source",
              [](class_type& self, other_type& other) -> bool {
                pybind11::gil_scoped_release release;
                return self.open_source(other.get_source());
              },
              pybind11::arg("other"),
              "Open the document another reader, writer or editor currently holds.");
    }

    template<typename class_type, typename other_type>
    void bind_append_pages(pybind11::class_<class_type>& cls)
    {
      cls.def("append_pages",
              [](class_type& self,
                 other_type& other,
                 std::optional<std::vector<int> > pages,
                 int at,
                 std::int64_t other_generation,
                 bool import_outline,
                 const std::optional<std::string>& outline_item,
                 bool transient) {
                pybind11::gil_scoped_release release;
                auto source = transient ? other.take_import_source(other_generation)
                                        : other.get_source(other_generation);
                return self.append_pages(source,
                                         pages,
                                         at,
                                         import_outline, outline_item);
              },
              pybind11::arg("other"),
              pybind11::arg("pages") = pybind11::none(),
              pybind11::arg("at") = -1,
              pybind11::arg("other_generation") = other_type::ANY_GENERATION,
              pybind11::arg("import_outline") = false,
              pybind11::arg("outline_item") = pybind11::none(),
              pybind11::arg("transient") = false,
              "Copy pages and return the first new index and generation.");
    }

    // overlay_page(page, other, other_page, ...): one overload per kind of
    // document the overlaid page can come from.
    template<typename class_type, typename other_type>
    void bind_overlay_page(pybind11::class_<class_type>& cls)
    {
      cls.def("overlay_page",
              [](class_type& self,
                 int page,
                 other_type& other,
                 int other_page,
                 bool under,
                 bool fit,
                 std::array<double, 6> matrix,
                 std::int64_t generation,
                 std::int64_t other_generation,
                 bool crop,
                 bool expand) -> void {
                pybind11::gil_scoped_release release;
                self.overlay_page(page,
                                  generation,
                                  other.get_source(other_generation),
                                  other_page,
                                  under,
                                  fit,
                                  matrix,
                                  crop,
                                  expand);
              },
              pybind11::arg("page"),
              pybind11::arg("other"),
              pybind11::arg("other_page"),
              pybind11::arg("under") = false,
              pybind11::arg("fit") = false,
              pybind11::arg("matrix") = std::array<double, 6>{1.0, 0.0, 0.0, 1.0, 0.0, 0.0},
              pybind11::arg("generation") = class_type::ANY_GENERATION,
              pybind11::arg("other_generation") = other_type::ANY_GENERATION,
              pybind11::arg("crop") = false,
              pybind11::arg("expand") = false,
              "Draw a page of another document on this page, above or below its content.");
    }

    // Editing and output: bound on the writer and the editor.
    template<typename class_type>
    void bind_editing(pybind11::class_<class_type>& cls)
    {
      cls
        .def("create_empty",
             [](class_type& self) -> void {
               pybind11::gil_scoped_release release;
               self.create_empty();
             },
             "Start an empty document with no pages.")

        .def("remove_page",
             [](class_type& self, int page, std::int64_t generation, bool clean,
                bool detach_references) -> void {
               pybind11::gil_scoped_release release;
               self.remove_page(page, generation, clean, detach_references);
             },
             pybind11::arg("page"),
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             pybind11::arg("clean") = false,
             pybind11::arg("detach_references") = true)

        .def("move_page",
             [](class_type& self, int from, int to, std::int64_t generation) -> void {
               pybind11::gil_scoped_release release;
               self.move_page(from, to, generation);
             },
             pybind11::arg("from_index"),
             pybind11::arg("to_index"),
             pybind11::arg("generation") = class_type::ANY_GENERATION)

        .def("rotate_page",
             [](class_type& self, int page, int angle, bool relative, std::int64_t generation) -> void {
               pybind11::gil_scoped_release release;
               self.rotate_page(page, generation, angle, relative);
             },
             pybind11::arg("page"),
             pybind11::arg("angle"),
             pybind11::arg("relative") = true,
             pybind11::arg("generation") = class_type::ANY_GENERATION)

        .def("set_page_box",
             [](class_type& self,
                int page,
                const std::string& box,
                std::array<double, 4> rectangle,
                std::int64_t generation) -> void {
               pybind11::gil_scoped_release release;
               self.set_page_box(page, generation, box, rectangle);
             },
             pybind11::arg("page"),
             pybind11::arg("box"),
             pybind11::arg("rectangle"),
             pybind11::arg("generation") = class_type::ANY_GENERATION)

        .def("transform_page",
             [](class_type& self,
                int page,
                std::array<double, 6> matrix,
                std::int64_t generation) -> void {
               pybind11::gil_scoped_release release;
               self.transform_page(page, generation, matrix);
             },
             pybind11::arg("page"),
             pybind11::arg("matrix"),
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             "Apply the matrix [a, b, c, d, e, f] to the page's content and annotations.")

        .def("scale_page",
             [](class_type& self, int page, double sx, double sy, std::int64_t generation) -> void {
               pybind11::gil_scoped_release release;
               self.scale_page(page, generation, sx, sy);
             },
             pybind11::arg("page"),
             pybind11::arg("sx"),
             pybind11::arg("sy"),
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             "Scale the page: content, annotations and all page boundaries.")

        .def("flatten_page_rotation",
             [](class_type& self, int page, std::int64_t generation) -> void {
               pybind11::gil_scoped_release release;
               self.flatten_page_rotation(page, generation);
             },
             pybind11::arg("page"),
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             "Turn the page's /Rotate into a rotation of its content.")

        .def("duplicate_page",
             [](class_type& self, int page, int at, std::int64_t generation) {
               pybind11::gil_scoped_release release;
               return self.duplicate_page(page, generation, at);
             },
             pybind11::arg("page"),
             pybind11::arg("at") = -1,
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             "Add a copy of a page and return its index and generation.")

        .def("insert_blank_page",
             [](class_type& self, double width, double height, int at) {
               pybind11::gil_scoped_release release;
               return self.insert_blank_page(at, width, height);
             },
             pybind11::arg("width"),
             pybind11::arg("height"),
             pybind11::arg("at") = -1)

        .def("set_info",
             [](class_type& self, std::map<std::string, std::optional<std::string> > entries) -> void {
               pybind11::gil_scoped_release release;
               self.set_info(entries);
             },
             pybind11::arg("entries"),
             "Set entries of /Info; a None value removes the key.")

        .def("set_xmp",
             [](class_type& self, std::optional<pybind11::bytes> packet) -> void {
               std::optional<std::string> data;
               if(packet.has_value())
                 {
                   data = packet.value().cast<std::string>();
                 }

               pybind11::gil_scoped_release release;
               self.set_xmp(data);
             },
             pybind11::arg("packet"),
             "Replace the XMP metadata packet; None removes it.")

        .def("set_page_layout",
             [](class_type& self, const std::string& name) -> void {
               pybind11::gil_scoped_release release;
               self.set_page_layout(name);
             },
             pybind11::arg("name"),
             "Set /PageLayout; an empty name removes it.")

        .def("set_page_mode",
             [](class_type& self, const std::string& name) -> void {
               pybind11::gil_scoped_release release;
               self.set_page_mode(name);
             },
             pybind11::arg("name"),
             "Set /PageMode; an empty name removes it.")

        .def("set_viewer_preferences",
             [](class_type& self, const std::string& dictionary) -> void {
               pybind11::gil_scoped_release release;
               self.set_viewer_preferences(dictionary);
             },
             pybind11::arg("dictionary"),
             "Replace /ViewerPreferences by a dictionary in PDF syntax; empty removes it.")

        .def("set_open_destination",
             [](class_type& self, int page) -> void {
               pybind11::gil_scoped_release release;
               self.set_open_destination(page);
             },
             pybind11::arg("page"),
             "Page shown when the document opens; -1 removes the open action.")

        .def("set_page_label",
             [](class_type& self, int from, int to, const std::string& style, const std::string& prefix, int start) -> void {
               pybind11::gil_scoped_release release;
               self.set_page_label(from, to, style, prefix, start);
             },
             pybind11::arg("from_index"),
             pybind11::arg("to_index"),
             pybind11::arg("style") = "",
             pybind11::arg("prefix") = "",
             pybind11::arg("start") = 1,
             "Label a range of pages.")

        .def("add_named_destination",
             [](class_type& self, const std::string& name, int page) -> void {
               pybind11::gil_scoped_release release;
               self.add_named_destination(name, page);
             },
             pybind11::arg("name"),
             pybind11::arg("page"),
             "Add or replace a named destination.")

        .def("add_outline_entry",
             [](class_type& self, pybind11::dict spec, std::int64_t generation) -> int {
               nlohmann::json value = pyjson::to_json(spec);

               pybind11::gil_scoped_release release;
               return self.add_outline_entry(value, generation);
             },
             pybind11::arg("spec"),
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             "Add a bookmark described by a dict; returns its id.")

        .def("update_outline_item",
             [](class_type& self, int id, pybind11::dict changes) -> void {
               nlohmann::json value = pyjson::to_json(changes);

               pybind11::gil_scoped_release release;
               self.update_outline_item(id, value);
             },
             pybind11::arg("id"),
             pybind11::arg("changes"),
             "Change a bookmark.")

        .def("remove_outline_item",
             [](class_type& self, int id) -> void {
               pybind11::gil_scoped_release release;
               self.remove_outline_item(id);
             },
             pybind11::arg("id"),
             "Remove a bookmark and everything below it.")

        .def("move_outline_item",
             [](class_type& self, int id, int parent, int index) -> void {
               pybind11::gil_scoped_release release;
               self.move_outline_item(id, parent, index);
             },
             pybind11::arg("id"),
             pybind11::arg("parent") = 0,
             pybind11::arg("index") = -1,
             "Move a bookmark below another one, or to the top level.")

        .def("clear_outline",
             [](class_type& self) -> void {
               pybind11::gil_scoped_release release;
               self.clear_outline();
             },
             "Remove the whole outline.")

        .def("add_form_field",
             [](class_type& self, int page, pybind11::dict spec, std::int64_t generation) -> void {
               nlohmann::json value = pyjson::to_json(spec);

               pybind11::gil_scoped_release release;
               self.add_form_field(page, generation, value);
             },
             pybind11::arg("page"),
             pybind11::arg("spec"),
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             "Create a form field on a page.")

        .def("update_form_field",
             [](class_type& self, const std::string& name, pybind11::dict changes) -> void {
               nlohmann::json value = pyjson::to_json(changes);

               pybind11::gil_scoped_release release;
               self.update_form_field(name, value);
             },
             pybind11::arg("name"),
             pybind11::arg("changes"),
             "Change a form field's name, rectangle, flags, tooltip or options.")

        .def("remove_form_field",
             [](class_type& self, const std::string& name) -> void {
               pybind11::gil_scoped_release release;
               self.remove_form_field(name);
             },
             pybind11::arg("name"),
             "Remove a form field and its widgets.")

        .def("insert_image_pages",
             [](class_type& self, int at, const std::vector<docling_editor::image_page>& images) {
               pybind11::gil_scoped_release release;
               return self.insert_image_pages(at, images);
             },
             pybind11::arg("at"),
             pybind11::arg("images"))

        .def("insert_contents_pages",
             [](class_type& self, int at, std::int64_t generation, double width, double height,
                const std::string& font, double label_x, double link_right,
                const std::vector<std::vector<docling_editor::contents_line> >& lines) {
               pybind11::gil_scoped_release release;
               return self.insert_contents_pages(at, generation, width, height, font, label_x,
                                                 link_right, lines);
             },
             pybind11::arg("at"),
             pybind11::arg("generation"),
             pybind11::arg("width"),
             pybind11::arg("height"),
             pybind11::arg("font"),
             pybind11::arg("label_x"),
             pybind11::arg("link_right"),
             pybind11::arg("lines"))

        .def("draw_text",
             [](class_type& self,
                int page,
                const std::string& text,
                double x,
                double y,
                double size,
                const std::string& font,
                std::array<double, 3> color,
                std::int64_t generation) -> void {
               pybind11::gil_scoped_release release;
               self.draw_text(page, generation, text, x, y, size, font, color);
             },
             pybind11::arg("page"),
             pybind11::arg("text"),
             pybind11::arg("x"),
             pybind11::arg("y"),
             pybind11::arg("size") = 12.0,
             pybind11::arg("font") = "Helvetica",
             pybind11::arg("color") = std::array<double, 3>{0.0, 0.0, 0.0},
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             "Draw text on a page in a standard font.")

        .def("set_form_values",
             [](class_type& self, std::map<std::string, std::string> values, bool flatten) -> int {
               pybind11::gil_scoped_release release;
               return self.set_form_values(values, flatten);
             },
             pybind11::arg("values"),
             pybind11::arg("flatten") = false,
             "Set form field values by name; returns the number of fields set.")

        .def("set_need_appearances",
             [](class_type& self, bool value) -> void {
               pybind11::gil_scoped_release release;
               self.set_need_appearances(value);
             },
             pybind11::arg("value"),
             "Set the form's /NeedAppearances flag.")

        .def("flatten_annotations",
             [](class_type& self) -> void {
               pybind11::gil_scoped_release release;
               self.flatten_annotations();
             },
             "Draw annotations and form fields into the page content and remove them.")

        .def("add_annotation",
             [](class_type& self, int page, const std::string& dictionary,
                std::int64_t generation, bool report_annotation) {
               nlohmann::json annotation;
               {
                 pybind11::gil_scoped_release release;
                 annotation = self.add_annotation(page, generation, dictionary, report_annotation);
               }
               return pyjson::from_json(annotation);
             },
             pybind11::arg("page"),
             pybind11::arg("dictionary"),
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             pybind11::arg("report_annotation") = false,
             "Add an annotation given as a dictionary in PDF syntax.")

        .def("add_page_link",
             [](class_type& self, int page, std::array<double, 4> rectangle, int target, std::int64_t generation) -> void {
               pybind11::gil_scoped_release release;
               self.add_page_link(page, generation, rectangle, target);
             },
             pybind11::arg("page"),
             pybind11::arg("rectangle"),
             pybind11::arg("target"),
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             "Add a link to another page of the document.")

        .def("remove_annotations",
             [](class_type& self, int page, std::set<std::string> subtypes, std::int64_t generation) -> int {
               pybind11::gil_scoped_release release;
               return self.remove_annotations(page, generation, subtypes);
             },
             pybind11::arg("page"),
             pybind11::arg("subtypes") = std::set<std::string>{},
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             "Remove annotations of a page by subtype; returns the number removed.")

        .def("add_attachment",
             [](class_type& self,
                const std::string& name,
                pybind11::bytes data,
                const std::string& description,
                const std::string& mime_type) -> void {
               std::string bytes = data.cast<std::string>();

               pybind11::gil_scoped_release release;
               self.add_attachment(name, bytes, description, mime_type);
             },
             pybind11::arg("name"),
             pybind11::arg("data"),
             pybind11::arg("description") = "",
             pybind11::arg("mime_type") = "",
             "Embed a file, replacing an attachment of the same name.")

        .def("remove_attachment",
             [](class_type& self, const std::string& name) -> bool {
               pybind11::gil_scoped_release release;
               return self.remove_attachment(name);
             },
             pybind11::arg("name"),
             "Remove an embedded file; False if there is none of that name.")

        .def("add_javascript",
             [](class_type& self, const std::optional<std::string>& name, const std::string& script) -> void {
               pybind11::gil_scoped_release release;
               self.add_javascript(name, script);
             },
             pybind11::arg("name"),
             pybind11::arg("script"),
             "Register a document-level script.")

        .def("remove_javascript",
             [](class_type& self) -> int {
               pybind11::gil_scoped_release release;
               return self.remove_javascript();
             },
             "Remove document-level scripts and scripted document actions.")

        .def("set_output_options",
             [](class_type& self, bool compress_streams, const std::string& object_streams, bool linearize) -> void {
               pybind11::gil_scoped_release release;
               self.set_output_options(compress_streams, object_streams, linearize);
             },
             pybind11::arg("compress_streams") = true,
             pybind11::arg("object_streams") = "preserve",
             pybind11::arg("linearize") = false,
             "How the file is written.")

        .def("set_encryption",
             [](class_type& self,
                const std::string& user_password,
                const std::optional<std::string>& owner_password,
                int print,
                bool modify,
                bool extract,
                bool annotate,
                bool fill_forms,
                bool accessibility,
                bool assemble,
                const std::string& algorithm) -> void {
               pdflib::pdf_permissions permissions;
               permissions.set_print(print);
               permissions.set_modify(modify);
               permissions.set_extract(extract);
               permissions.set_annotate(annotate);
               permissions.set_fill_forms(fill_forms);
               permissions.set_accessibility(accessibility);
               permissions.set_assemble(assemble);

               pybind11::gil_scoped_release release;
               self.set_encryption(user_password, owner_password, permissions, algorithm);
             },
             pybind11::arg("user_password"),
             pybind11::arg("owner_password") = pybind11::none(),
             pybind11::arg("print") = 0,
             pybind11::arg("modify") = true,
             pybind11::arg("extract") = true,
             pybind11::arg("annotate") = true,
             pybind11::arg("fill_forms") = true,
             pybind11::arg("accessibility") = true,
             pybind11::arg("assemble") = true,
             pybind11::arg("algorithm") = "AES-256",
             "Encrypt output with the requested PDF password algorithm.")

        .def("clear_encryption",
             [](class_type& self) -> void {
               pybind11::gil_scoped_release release;
               self.clear_encryption();
             },
             "Write unencrypted output again.")

        .def("write_to_bytes",
             [](class_type& self) -> pybind11::bytes {
               std::shared_ptr<std::string> data;
               {
                 pybind11::gil_scoped_release release;
                 data = self.write_to_memory();
               }
               return pybind11::bytes(*data);
             });
    }

    // Operations with no pypdf counterpart: bound on the editor only.
    inline void bind_editor_only(pybind11::class_<docling_editor>& cls)
    {
      cls
        .def("render_page",
             [](docling_editor& self, int page_no, double scale) -> pybind11::tuple {
               std::pair<std::shared_ptr<std::vector<std::uint8_t>>, std::array<int, 3>> result;
               {
                 pybind11::gil_scoped_release release;
                 result = self.render_page(page_no, scale);
               }
               const auto& pixels = result.first;
               pybind11::bytes data = pixels ? pybind11::bytes(
                 reinterpret_cast<const char*>(pixels->data()), pixels->size()) : pybind11::bytes("");
               return pybind11::make_tuple(data, result.second);
             },
             pybind11::kw_only(),
             pybind11::arg("page_no"),
             pybind11::arg("scale") = 1.0,
             "Render the current page to RGBA bytes and shape; page_no starts at 1.")

        .def("is_editing", [](docling_editor& self) -> bool {
          pybind11::gil_scoped_release release;
          return self.is_editing();
        },
             "True once the editor works on its own copy of the document.")

        .def("object_json",
             [](docling_editor& self, int id, int depth) -> pybind11::object {
               nlohmann::json result;
               {
                 pybind11::gil_scoped_release release;
                 result = self.object_json(id, depth);
               }
               return pyjson::from_json(result);
             },
             pybind11::arg("id"),
             pybind11::arg("depth") = 3,
             "A bounded view of one PDF object; id 0 is the trailer, -1 the catalog.")

        .def("annotations",
             [](docling_editor& self) -> pybind11::object {
               nlohmann::json annotations;
               {
                 pybind11::gil_scoped_release release;
                 annotations = self.annotations();
               }
               return pyjson::from_json(annotations);
             },
             "Form, language, XMP, outline and structure of the current document state.");
    }

    // pdf_edit_error -> Python exception with args (category, message).
    inline void bind_edit_error(pybind11::module_& m)
    {
      // Kept for the lifetime of the interpreter; a translator cannot capture.
      static pybind11::exception<pdflib::pdf_edit_error>* edit_error_type =
        new pybind11::exception<pdflib::pdf_edit_error>(m, "pdf_edit_error", PyExc_RuntimeError);

      pybind11::register_exception_translator([](std::exception_ptr pointer) {
        try
          {
            if(pointer)
              {
                std::rethrow_exception(pointer);
              }
          }
        catch(const pdflib::pdf_edit_error& exc)
          {
            pybind11::tuple args = pybind11::make_tuple(exc.get_category(), std::string(exc.what()));
            PyErr_SetObject(edit_error_type->ptr(), args.ptr());
          }
      });
    }
    template<typename class_type, typename... source_types>
    void bind_document(pybind11::class_<class_type>& cls)
    {
      bind_inspection(cls);
      (bind_open_source<class_type, source_types>(cls), ...);
      if constexpr(not std::is_same_v<class_type, pdf_reader>)
        {
          bind_editing(cls);
          (bind_append_pages<class_type, source_types>(cls), ...);
          (bind_overlay_page<class_type, source_types>(cls), ...);
        }
    }
  }

  // Called once from the module definition in app/pybind_parse.cpp.
  inline void register_edit_bindings(pybind11::module_& m)
  {
    edit_bindings::bind_edit_error(m);

    pybind11::class_<pdf_reader> reader(m, "pdf_reader",
      "Read-only PDF document; the native half of docling_parse.pdf_reader.PdfReader.");
    pybind11::class_<pdf_writer> writer(m, "pdf_writer",
      "Writable PDF document; the native half of docling_parse.pdf_writer.PdfWriter.");
    pybind11::class_<docling_editor> editor(m, "pdf_editor",
      "Full PDF editor; the native half of docling_parse.pdf_editor.PdfEditor.");

    edit_bindings::bind_document<pdf_reader, pdf_reader, pdf_writer, docling_editor>(reader);
    edit_bindings::bind_document<pdf_writer, pdf_reader, pdf_writer, docling_editor>(writer);
    edit_bindings::bind_document<docling_editor, pdf_reader, pdf_writer, docling_editor>(editor);

    edit_bindings::bind_editor_only(editor);
  }

}

#endif
