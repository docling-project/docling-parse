//-*-C++-*-

#ifndef PYBIND_PDF_WRITER_H
#define PYBIND_PDF_WRITER_H

#include <pybind/docling_editor.h>

namespace docling
{
  // The pypdf-shaped writing face of docling_editor. It implements nothing:
  // protected inheritance hides the editor, and the using-declarations
  // publish inspection plus the editing and output operations. Add logic to
  // docling_editor, never here.
  class pdf_writer: protected docling_editor
  {
  public:

    pdf_writer(std::string loglevel = "fatal");

    using docling_editor::ANY_GENERATION;

    using docling_editor::open_file;
    using docling_editor::open_buffer;
    using docling_editor::open_source;
    using docling_editor::take_open_error;
    using docling_editor::create_empty;
    using docling_editor::is_open;
    using docling_editor::close;

    using docling_editor::page_count;
    using docling_editor::generation;
    using docling_editor::page_list_state;
    using docling_editor::validate_page_handle;
    using docling_editor::document_info;
    using docling_editor::validate_strict;
    using docling_editor::page_info;
    using docling_editor::extract_text;
    using docling_editor::page_images;
    using docling_editor::outline;
    using docling_editor::get_source;
    using docling_editor::document_settings;
    using docling_editor::page_labels;
    using docling_editor::named_destinations;
    using docling_editor::outline_tree;
    using docling_editor::form;
    using docling_editor::form_state;
    using docling_editor::page_annotations;
    using docling_editor::attachments;
    using docling_editor::attachment_data;
    using docling_editor::javascript;

    using docling_editor::remove_page;
    using docling_editor::move_page;
    using docling_editor::rotate_page;
    using docling_editor::set_page_box;
    using docling_editor::transform_page;
    using docling_editor::scale_page;
    using docling_editor::overlay_page;
    using docling_editor::flatten_page_rotation;
    using docling_editor::duplicate_page;
    using docling_editor::insert_blank_page;
    using docling_editor::insert_image_pages;
    using docling_editor::insert_contents_pages;
    using docling_editor::draw_text;
    using docling_editor::append_pages;

    using docling_editor::set_info;
    using docling_editor::set_xmp;
    using docling_editor::set_page_layout;
    using docling_editor::set_page_mode;
    using docling_editor::set_viewer_preferences;
    using docling_editor::set_open_destination;
    using docling_editor::set_page_label;
    using docling_editor::add_named_destination;
    using docling_editor::add_outline_item;
    using docling_editor::add_outline_entry;
    using docling_editor::update_outline_item;
    using docling_editor::remove_outline_item;
    using docling_editor::move_outline_item;
    using docling_editor::clear_outline;
    using docling_editor::set_form_values;
    using docling_editor::set_need_appearances;
    using docling_editor::flatten_annotations;
    using docling_editor::add_form_field;
    using docling_editor::update_form_field;
    using docling_editor::remove_form_field;
    using docling_editor::add_annotation;
    using docling_editor::add_page_link;
    using docling_editor::remove_annotations;
    using docling_editor::add_attachment;
    using docling_editor::remove_attachment;
    using docling_editor::add_javascript;
    using docling_editor::remove_javascript;
    using docling_editor::set_output_options;
    using docling_editor::set_encryption;
    using docling_editor::clear_encryption;
    using docling_editor::write_to_memory;
  };

  inline pdf_writer::pdf_writer(std::string loglevel):
    docling_editor(loglevel, false)
  {}

}

#endif
