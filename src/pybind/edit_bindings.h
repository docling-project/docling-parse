//-*-C++-*-

#ifndef PYBIND_EDIT_BINDINGS_H
#define PYBIND_EDIT_BINDINGS_H

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
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
             [](class_type& self, const std::string& filename, std::optional<std::string> password) -> bool {
               pybind11::gil_scoped_release release;
               return self.open_file(filename, password);
             },
             pybind11::arg("filename"),
             pybind11::arg("password") = pybind11::none(),
             "Open a PDF file. Returns False on failure; see take_open_error().")

        .def("open_bytes",
             [](class_type& self, pybind11::bytes data, std::optional<std::string> password) -> bool {
               auto buffer = std::make_shared<std::string>(data.cast<std::string>());

               pybind11::gil_scoped_release release;
               return self.open_buffer(buffer, password);
             },
             pybind11::arg("data"),
             pybind11::arg("password") = pybind11::none(),
             "Open a PDF held in a bytes object. Returns False on failure; see take_open_error().")

        .def("take_open_error",
             [](class_type& self) -> std::map<std::string, std::string> {
               return self.take_open_error();
             },
             "Return and clear why the last open failed: {'category': ..., 'message': ...}.")

        .def("is_open", [](class_type& self) -> bool { return self.is_open(); })

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
             [](class_type& self) -> std::int64_t { return self.generation(); },
             "Changes whenever existing page indices may have changed meaning.")

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

        .def("extract_text",
             [](class_type& self, int page, std::int64_t generation) -> std::string {
               pybind11::gil_scoped_release release;
               return self.extract_text(page, generation);
             },
             pybind11::arg("page"),
             pybind11::arg("generation") = class_type::ANY_GENERATION)

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

        .def("outline",
             [](class_type& self) -> pybind11::object {
               nlohmann::json outline;
               {
                 pybind11::gil_scoped_release release;
                 outline = self.outline();
               }
               return pyjson::from_json(outline);
             });
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
                 std::int64_t other_generation) -> int {
                pybind11::gil_scoped_release release;
                return self.append_pages(other.get_source(other_generation), pages, at);
              },
              pybind11::arg("other"),
              pybind11::arg("pages") = pybind11::none(),
              pybind11::arg("at") = -1,
              pybind11::arg("other_generation") = other_type::ANY_GENERATION,
              "Copy pages of another document into this one. Returns the index of the first new page.");
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
                 std::int64_t other_generation) -> void {
                pybind11::gil_scoped_release release;
                self.overlay_page(page,
                                  generation,
                                  other.get_source(other_generation),
                                  other_page,
                                  under,
                                  fit,
                                  matrix);
              },
              pybind11::arg("page"),
              pybind11::arg("other"),
              pybind11::arg("other_page"),
              pybind11::arg("under") = false,
              pybind11::arg("fit") = false,
              pybind11::arg("matrix") = std::array<double, 6>{1.0, 0.0, 0.0, 1.0, 0.0, 0.0},
              pybind11::arg("generation") = class_type::ANY_GENERATION,
              pybind11::arg("other_generation") = other_type::ANY_GENERATION,
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
             [](class_type& self, int page, std::int64_t generation) -> void {
               pybind11::gil_scoped_release release;
               self.remove_page(page, generation);
             },
             pybind11::arg("page"),
             pybind11::arg("generation") = class_type::ANY_GENERATION)

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
             [](class_type& self, int page, int at, std::int64_t generation) -> int {
               pybind11::gil_scoped_release release;
               return self.duplicate_page(page, generation, at);
             },
             pybind11::arg("page"),
             pybind11::arg("at") = -1,
             pybind11::arg("generation") = class_type::ANY_GENERATION,
             "Add a copy of a page. Returns the index of the copy.")

        .def("insert_blank_page",
             [](class_type& self, double width, double height, int at) -> int {
               pybind11::gil_scoped_release release;
               return self.insert_blank_page(at, width, height);
             },
             pybind11::arg("width"),
             pybind11::arg("height"),
             pybind11::arg("at") = -1)

        .def("set_encryption",
             [](class_type& self,
                const std::string& user_password,
                const std::string& owner_password) -> void {
               pybind11::gil_scoped_release release;
               self.set_encryption(user_password, owner_password);
             },
             pybind11::arg("user_password"),
             pybind11::arg("owner_password") = "",
             "Encrypt everything written from now on with AES-256.")

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
             })

        .def("write_to_file",
             [](class_type& self, const std::string& filename) -> void {
               pybind11::gil_scoped_release release;
               self.write_to_file(filename);
             },
             pybind11::arg("filename"));
    }

    // Operations with no pypdf counterpart: bound on the editor only.
    inline void bind_editor_only(pybind11::class_<docling_editor>& cls)
    {
      cls
        .def("is_editing", [](docling_editor& self) -> bool { return self.is_editing(); },
             "True once the editor works on its own copy of the document.")

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

    edit_bindings::bind_inspection(reader);
    edit_bindings::bind_inspection(writer);
    edit_bindings::bind_inspection(editor);

    edit_bindings::bind_open_source<pdf_reader, pdf_reader>(reader);
    edit_bindings::bind_open_source<pdf_reader, pdf_writer>(reader);
    edit_bindings::bind_open_source<pdf_reader, docling_editor>(reader);

    edit_bindings::bind_open_source<pdf_writer, pdf_reader>(writer);
    edit_bindings::bind_open_source<pdf_writer, pdf_writer>(writer);
    edit_bindings::bind_open_source<pdf_writer, docling_editor>(writer);

    edit_bindings::bind_open_source<docling_editor, pdf_reader>(editor);
    edit_bindings::bind_open_source<docling_editor, pdf_writer>(editor);
    edit_bindings::bind_open_source<docling_editor, docling_editor>(editor);

    edit_bindings::bind_editing(writer);
    edit_bindings::bind_editing(editor);

    edit_bindings::bind_append_pages<pdf_writer, pdf_reader>(writer);
    edit_bindings::bind_append_pages<pdf_writer, pdf_writer>(writer);
    edit_bindings::bind_append_pages<pdf_writer, docling_editor>(writer);

    edit_bindings::bind_append_pages<docling_editor, pdf_reader>(editor);
    edit_bindings::bind_append_pages<docling_editor, pdf_writer>(editor);
    edit_bindings::bind_append_pages<docling_editor, docling_editor>(editor);

    edit_bindings::bind_overlay_page<pdf_writer, pdf_reader>(writer);
    edit_bindings::bind_overlay_page<pdf_writer, pdf_writer>(writer);
    edit_bindings::bind_overlay_page<pdf_writer, docling_editor>(writer);

    edit_bindings::bind_overlay_page<docling_editor, pdf_reader>(editor);
    edit_bindings::bind_overlay_page<docling_editor, pdf_writer>(editor);
    edit_bindings::bind_overlay_page<docling_editor, docling_editor>(editor);

    edit_bindings::bind_editor_only(editor);
  }

}

#endif
