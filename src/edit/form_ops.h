//-*-C++-*-

#ifndef PDF_FORM_OPS_H
#define PDF_FORM_OPS_H

#include <array>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#define POINTERHOLDER_TRANSITION 0 // eliminate warnings from QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QUtil.hh>

#include <edit/edit_error.h>
#include <edit/page_ops.h>

namespace pdflib
{
  // Interactive forms (AcroForm; ISO 32000-1, 12.7): reading the fields,
  // setting their values, and flattening them into page content.
  class pdf_form_ops
  {
  public:

    // {"has_form", "has_xfa", "need_appearances", "fields": [{"name",
    // "partial_name", "alternate_name", "type", "kind", "value",
    // "default_value", "flags", "read_only", "choices", "states",
    // "pages"}, ...]}
    //
    // "kind" is text, checkbox, radio, pushbutton, choice, signature or
    // other. "states" are the appearance states a checkbox or radio group
    // can take besides /Off. "pages" are the pages showing a widget of the
    // field.
    static nlohmann::json read(QPDF& qpdf);

    // Set field values by fully qualified name; a partial name is accepted
    // when it identifies exactly one field. Text and choice fields take the
    // value as text. A checkbox or radio group takes one of its states
    // ("/Yes", or "Yes"), or "/Off"; for a checkbox, "true"/"false" select
    // its on state and "/Off". Appearance streams are regenerated. Returns
    // the number of fields set; an unknown name is an error.
    static int set_values(QPDF& qpdf, const std::map<std::string, std::string>& values);

    static void set_need_appearances(QPDF& qpdf, bool value);

    // Draw every visible annotation, form widgets included, into its page's
    // content and remove the annotations: the values stay visible, the
    // interactive form is gone.
    static void flatten(QPDF& qpdf);

    // Create a field on `page`. Entries of `spec`:
    //
    //   "kind"       text, checkbox, radio, choice or signature (required)
    //   "name"       the field's name, unique in the form, without "."
    //   "rect"       [left, bottom, right, top] of the widget; not for radio
    //   "value"      text and choice: the initial text
    //   "checked"    checkbox: true or false
    //   "options"    choice: the entries to pick from;
    //                radio: [{"state": name, "rect": [...]}, ...], one
    //                button per entry
    //   "selected"   radio: the state that starts selected, or null
    //   "multiline"  text: true for a field of several lines
    //   "editable"   choice: true lets the user type a value of their own
    //   "font_size"  text and choice: in points, 12 by default
    //   "read_only", "required"   booleans
    //   "tooltip"    the description a viewer shows for the field, or null
    //
    // Text is drawn in Helvetica; a field's appearance is built here, so it
    // shows in viewers that do not draw forms themselves.
    static void add_field(QPDF& qpdf, QPDFPageObjectHelper page, const nlohmann::json& spec);

    // Change a field found by name as in set_values. Entries of `changes`,
    // all optional: "name" (the new partial name), "rect" (fields with one
    // widget), "read_only", "required", "tooltip" (text or null) and
    // "options" (choice fields).
    static void update_field(QPDF& qpdf, const std::string& name, const nlohmann::json& changes);

    // Remove a field and its widgets from the form and from their pages.
    static void remove_field(QPDF& qpdf, const std::string& name);

  private:

    static std::string kind_of(QPDFFormFieldObjectHelper& field);

    // qpdf may expose unnamed widgets as terminal fields. Resolve them to
    // their nearest named owner, preserving named fields in nested forms.
    static QPDFFormFieldObjectHelper field_of(QPDFFormFieldObjectHelper field);
    static std::vector<QPDFFormFieldObjectHelper> fields_of(QPDFAcroFormDocumentHelper& acroform);
    static std::vector<QPDFAnnotationObjectHelper> widgets_of(QPDFAcroFormDocumentHelper& acroform,
                                                              QPDFFormFieldObjectHelper& field);

    // The names of a widget's normal appearances other than /Off.
    static std::set<std::string> states_of(QPDFAcroFormDocumentHelper& acroform,
                                           QPDFFormFieldObjectHelper& field);

    static std::string with_slash(const std::string& name);

    // The one field `name` stands for: its fully qualified name, or a
    // partial name that only one field has.
    static QPDFFormFieldObjectHelper find_field(std::vector<QPDFFormFieldObjectHelper>& fields,
                                                const std::string& name);

    // The form's default font, /Helv: Helvetica in WinAnsiEncoding, present
    // in the form's default resources. Creates the form if there is none.
    static QPDFObjectHandle ensure_form_font(QPDF& qpdf, QPDFAcroFormDocumentHelper& acroform);

    static void check_new_name(QPDFAcroFormDocumentHelper& acroform,
                               const std::string& name,
                               const std::string& qualified_name);

    static std::array<double, 4> rect_of(const nlohmann::json& value);
    static bool flag_of(const nlohmann::json& spec, const char* key, bool fallback);

    // A widget annotation on `page` over `rect`, not yet a field.
    static QPDFObjectHandle make_widget(QPDFPageObjectHelper page, const std::array<double, 4>& rect);

    // A form XObject of width x height showing `content`.
    static QPDFObjectHandle make_appearance(QPDF& qpdf,
                                            double width,
                                            double height,
                                            const std::string& content,
                                            QPDFObjectHandle font);

    // Content stream fragments, in a width x height box with its origin at
    // the lower left corner.
    static std::string box_path(double width, double height);
    static std::string check_path(double width, double height);
    static std::string circle_path(double center_x, double center_y, double radius);

    // The normal appearance of a text or choice field: a frame, plus the
    // marked-content section qpdf fills with the field's value.
    static void set_text_appearance(QPDF& qpdf,
                                    QPDFObjectHandle widget,
                                    const std::array<double, 4>& rect,
                                    QPDFObjectHandle font);

    static void append_widget(QPDFPageObjectHelper page, QPDFObjectHandle widget);

    static constexpr long long FLAG_READ_ONLY = 1LL << 0;
    static constexpr long long FLAG_REQUIRED = 1LL << 1;
    static constexpr long long FLAG_MULTILINE = 1LL << 12;
    static constexpr long long FLAG_NO_TOGGLE_TO_OFF = 1LL << 14;
    static constexpr long long FLAG_RADIO = 1LL << 15;
    static constexpr long long FLAG_COMBO = 1LL << 17;
    static constexpr long long FLAG_EDIT = 1LL << 18;
  };

  inline std::string pdf_form_ops::with_slash(const std::string& name)
  {
    return (name.size() > 0 and name.front() == '/') ? name : "/" + name;
  }

  inline std::string pdf_form_ops::kind_of(QPDFFormFieldObjectHelper& field)
  {
    if(field.isText())        { return "text"; }
    if(field.isCheckbox())    { return "checkbox"; }
    if(field.isRadioButton()) { return "radio"; }
    if(field.isPushbutton())  { return "pushbutton"; }
    if(field.isChoice())      { return "choice"; }
    if(field.getFieldType() == "/Sig") { return "signature"; }

    return "other";
  }

  inline QPDFFormFieldObjectHelper pdf_form_ops::field_of(QPDFFormFieldObjectHelper field)
  {
    std::set<QPDFObjGen> seen;
    for(int depth = 0; depth < 100; depth++)
      {
        QPDFObjectHandle object = field.getObjectHandle();
        if(object.hasKey("/T") or not seen.insert(object.getObjGen()).second)
          {
            break;
          }
        QPDFObjectHandle parent = object.getKey("/Parent");
        if(not parent.isDictionary())
          {
            break;
          }
        field = QPDFFormFieldObjectHelper(parent);
      }
    return field;
  }

  inline std::vector<QPDFFormFieldObjectHelper> pdf_form_ops::fields_of(QPDFAcroFormDocumentHelper& acroform)
  {
    std::vector<QPDFFormFieldObjectHelper> fields;
    std::set<QPDFObjGen> seen;
    for(QPDFFormFieldObjectHelper field : acroform.getFormFields())
      {
        field = field_of(field);
        if(seen.insert(field.getObjectHandle().getObjGen()).second)
          {
            fields.push_back(field);
          }
      }
    return fields;
  }

  inline std::vector<QPDFAnnotationObjectHelper> pdf_form_ops::widgets_of(QPDFAcroFormDocumentHelper& acroform,
                                                                         QPDFFormFieldObjectHelper& field)
  {
    std::vector<QPDFAnnotationObjectHelper> widgets;
    std::set<QPDFObjGen> seen;
    QPDFObjGen owner = field.getObjectHandle().getObjGen();
    for(QPDFFormFieldObjectHelper candidate : acroform.getFormFields())
      {
        if(field_of(candidate).getObjectHandle().getObjGen() != owner)
          {
            continue;
          }
        for(QPDFAnnotationObjectHelper& widget : acroform.getAnnotationsForField(candidate))
          {
            if(seen.insert(widget.getObjectHandle().getObjGen()).second)
              {
                widgets.push_back(widget);
              }
          }
      }
    return widgets;
  }

  inline std::set<std::string> pdf_form_ops::states_of(QPDFAcroFormDocumentHelper& acroform,
                                                       QPDFFormFieldObjectHelper& field)
  {
    std::set<std::string> states;

    for(QPDFAnnotationObjectHelper& widget : widgets_of(acroform, field))
      {
        QPDFObjectHandle appearances = widget.getAppearanceDictionary();
        if(not appearances.isDictionary())
          {
            continue;
          }

        QPDFObjectHandle normal = appearances.getKey("/N");
        if(not normal.isDictionary())
          {
            continue;
          }

        for(const std::string& state : normal.getKeys())
          {
            if(state != "/Off")
              {
                states.insert(state);
              }
          }
      }

    return states;
  }

  inline nlohmann::json pdf_form_ops::read(QPDF& qpdf)
  {
    nlohmann::json result = nlohmann::json::object();
    result["fields"] = nlohmann::json::array();

    QPDFAcroFormDocumentHelper acroform(qpdf);

    result["has_form"] = acroform.hasAcroForm();
    result["has_xfa"] = false;
    result["need_appearances"] = false;

    if(not acroform.hasAcroForm())
      {
        return result;
      }

    QPDFObjectHandle form = qpdf.getRoot().getKey("/AcroForm");
    result["has_xfa"] = (form.isDictionary() and form.hasKey("/XFA"));
    result["need_appearances"] = acroform.getNeedAppearances();

    // Index widgets themselves: qpdf's page lookup may return a top-level
    // field while its field lookup returns a terminal child.
    std::map<QPDFObjGen, std::set<int> > pages_of;
    int page_index = 0;
    for(QPDFPageObjectHelper& page : QPDFPageDocumentHelper(qpdf).getAllPages())
      {
        for(QPDFAnnotationObjectHelper& widget : acroform.getWidgetAnnotationsForPage(page))
          {
            pages_of[widget.getObjectHandle().getObjGen()].insert(page_index);
          }
        page_index += 1;
      }

    for(QPDFFormFieldObjectHelper& field : fields_of(acroform))
      {
        nlohmann::json entry = nlohmann::json::object();

        entry["name"] = field.getFullyQualifiedName();
        entry["partial_name"] = field.getPartialName();
        entry["alternate_name"] = field.getAlternativeName();
        entry["type"] = field.getFieldType();
        entry["kind"] = kind_of(field);

        // A button's value is a name (/Yes, /Off); text and choice values
        // are strings.
        QPDFObjectHandle value = field.getValue();
        entry["value"] = value.isName() ? value.getName() : field.getValueAsString();

        QPDFObjectHandle default_value = field.getDefaultValue();
        entry["default_value"] = default_value.isName() ?
          default_value.getName() : field.getDefaultValueAsString();

        int flags = field.getFlags();
        entry["flags"] = flags;
        entry["read_only"] = ((flags & ff_all_read_only) != 0);

        entry["choices"] = field.isChoice() ? nlohmann::json(field.getChoices()) : nlohmann::json::array();

        entry["states"] = nlohmann::json::array();
        if(field.isCheckbox() or field.isRadioButton())
          {
            for(const std::string& state : states_of(acroform, field))
              {
                entry["states"].push_back(state);
              }
          }

        // where each widget of the field sits, [left, bottom, right, top]
        entry["rects"] = nlohmann::json::array();
        std::set<int> field_pages;
        for(QPDFAnnotationObjectHelper& widget : widgets_of(acroform, field))
          {
            QPDFObjectHandle::Rectangle rect = widget.getRect();
            entry["rects"].push_back({rect.llx, rect.lly, rect.urx, rect.ury});
            auto itr = pages_of.find(widget.getObjectHandle().getObjGen());
            if(itr != pages_of.end())
              {
                field_pages.insert(itr->second.begin(), itr->second.end());
              }
          }

        entry["pages"] = nlohmann::json::array();
        for(int page : field_pages)
          {
            entry["pages"].push_back(page);
          }

        result["fields"].push_back(entry);
      }

    return result;
  }

  inline int pdf_form_ops::set_values(QPDF& qpdf, const std::map<std::string, std::string>& values)
  {
    QPDFAcroFormDocumentHelper acroform(qpdf);

    if(not acroform.hasAcroForm())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "the document has no form");
      }

    std::vector<QPDFFormFieldObjectHelper> fields = fields_of(acroform);

    int updated = 0;

    for(const auto& entry : values)
      {
        const std::string& name = entry.first;
        const std::string& value = entry.second;

        std::vector<QPDFFormFieldObjectHelper> matches = {find_field(fields, name)};

        for(QPDFFormFieldObjectHelper& field : matches)
          {
            if(field.isCheckbox() or field.isRadioButton())
              {
                std::set<std::string> states = states_of(acroform, field);

                std::string state = with_slash(value);
                if(field.isCheckbox() and (value == "true" or value == "True"))
                  {
                    // a checkbox has one on state, whatever it is called
                    state = states.empty() ? "/Yes" : *states.begin();
                  }
                else if(field.isCheckbox() and (value == "false" or value == "False" or value.empty()))
                  {
                    state = "/Off";
                  }

                // A widget without appearance streams declares no states;
                // there is then nothing to check the name against.
                if(state != "/Off" and not states.empty() and states.count(state) == 0)
                  {
                    std::string choices = "/Off";
                    for(const std::string& choice : states)
                      {
                        choices += ", " + choice;
                      }
                    throw pdf_edit_error(edit_error::INVALID_EDIT,
                                         "'" + value + "' is not a state of form field '" + name
                                         + "': expected one of " + choices);
                  }

                // qpdf sets /V and each widget's /AS for a name value.
                field.setV(QPDFObjectHandle::newName(state), true);
              }
            else if(field.isPushbutton() or field.getFieldType() == "/Sig")
              {
                throw pdf_edit_error(edit_error::INVALID_EDIT,
                                     "form field '" + name + "' has no value that can be set");
              }
            else
              {
                field.setV(value, true);
              }

            updated += 1;
          }
      }

    // setV flagged the form as needing appearances; build them now, so the
    // values show in viewers that do not regenerate appearances themselves.
    acroform.generateAppearancesIfNeeded();

    return updated;
  }

  inline void pdf_form_ops::set_need_appearances(QPDF& qpdf, bool value)
  {
    QPDFAcroFormDocumentHelper acroform(qpdf);

    if(not acroform.hasAcroForm())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "the document has no form");
      }

    acroform.setNeedAppearances(value);
  }

  inline void pdf_form_ops::flatten(QPDF& qpdf)
  {
    {
      QPDFAcroFormDocumentHelper acroform(qpdf);
      if(acroform.hasAcroForm())
        {
          acroform.generateAppearancesIfNeeded();
        }
    }

    QPDFPageDocumentHelper(qpdf).flattenAnnotations();
  }

  inline QPDFFormFieldObjectHelper pdf_form_ops::find_field(std::vector<QPDFFormFieldObjectHelper>& fields,
                                                            const std::string& name)
  {
    // Fully qualified name first; a partial name only when unambiguous.
    for(QPDFFormFieldObjectHelper& field : fields)
      {
        if(field.getFullyQualifiedName() == name)
          {
            return field;
          }
      }

    std::vector<QPDFFormFieldObjectHelper> matches;
    for(QPDFFormFieldObjectHelper& field : fields)
      {
        if(field.getPartialName() == name)
          {
            matches.push_back(field);
          }
      }

    if(matches.size() > 1)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "form field name '" + name + "' is ambiguous: use the fully qualified name");
      }
    if(matches.empty())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "the form has no field named '" + name + "'");
      }

    return matches.front();
  }

  inline QPDFObjectHandle pdf_form_ops::ensure_form_font(QPDF& qpdf, QPDFAcroFormDocumentHelper& acroform)
  {
    QPDFObjectHandle root = qpdf.getRoot();

    QPDFObjectHandle form = root.getKey("/AcroForm");
    if(not form.isDictionary())
      {
        form = QPDFObjectHandle::newDictionary();
        form.replaceKey("/Fields", QPDFObjectHandle::newArray());
        form = qpdf.makeIndirectObject(form);
        root.replaceKey("/AcroForm", form);

        // the helper looked at the document before it had a form
        acroform.invalidateCache();
      }

    // /DA and /DR are what a field without its own falls back to, and what
    // a viewer uses when it redraws a field (ISO 32000-1, table 218).
    if(not form.getKey("/DA").isString())
      {
        form.replaceKey("/DA", QPDFObjectHandle::newString("/Helv 12 Tf 0 g"));
      }

    QPDFObjectHandle resources = form.getKey("/DR");
    if(not resources.isDictionary())
      {
        resources = QPDFObjectHandle::newDictionary();
        form.replaceKey("/DR", resources);
      }

    QPDFObjectHandle fonts = resources.getKey("/Font");
    if(not fonts.isDictionary())
      {
        fonts = QPDFObjectHandle::newDictionary();
        resources.replaceKey("/Font", fonts);
      }

    QPDFObjectHandle font = fonts.getKey("/Helv");
    if(not font.isDictionary())
      {
        font = pdf_page_ops::make_standard_font(qpdf, "Helvetica");
        fonts.replaceKey("/Helv", font);
      }

    return font;
  }

  inline void pdf_form_ops::check_new_name(QPDFAcroFormDocumentHelper& acroform,
                                           const std::string& name,
                                           const std::string& qualified_name)
  {
    if(name.empty())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "a form field needs a name");
      }

    // A period separates the levels of a fully qualified name, so a partial
    // name shall not contain one (ISO 32000-1, 12.7.3.2).
    if(name.find('.') != std::string::npos)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "a form field name must not contain a period: '" + name + "'");
      }

    if(not acroform.getFieldsWithQualifiedName(qualified_name).empty())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "the form already has a field named '" + qualified_name + "'");
      }
  }

  inline std::array<double, 4> pdf_form_ops::rect_of(const nlohmann::json& value)
  {
    bool valid = (value.is_array() and value.size() == 4);
    for(std::size_t index = 0; valid and index < 4; index++)
      {
        valid = value[index].is_number();
      }

    std::array<double, 4> rect = {0.0, 0.0, 0.0, 0.0};
    if(valid)
      {
        rect = {value[0].get<double>(), value[1].get<double>(),
                value[2].get<double>(), value[3].get<double>()};
        valid = (std::isfinite(rect[0]) and std::isfinite(rect[1]) and
                 rect[0] < rect[2] and rect[1] < rect[3]);
      }

    if(not valid)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "a field rectangle is [left, bottom, right, top] with a positive width and height");
      }

    return rect;
  }

  inline bool pdf_form_ops::flag_of(const nlohmann::json& spec, const char* key, bool fallback)
  {
    auto itr = spec.find(key);
    if(itr == spec.end() or itr->is_null())
      {
        return fallback;
      }
    if(not itr->is_boolean())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             std::string("form field '") + key + "' must be true or false");
      }

    return itr->get<bool>();
  }

  inline QPDFObjectHandle pdf_form_ops::make_widget(QPDFPageObjectHelper page,
                                                    const std::array<double, 4>& rect)
  {
    QPDFObjectHandle widget = QPDFObjectHandle::newDictionary();
    widget.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    widget.replaceKey("/Subtype", QPDFObjectHandle::newName("/Widget"));
    widget.replaceKey("/Rect",
                      QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(rect[0], rect[1], rect[2], rect[3])));
    widget.replaceKey("/P", page.getObjectHandle());
    widget.replaceKey("/F", QPDFObjectHandle::newInteger(an_print));

    return widget;
  }

  inline QPDFObjectHandle pdf_form_ops::make_appearance(QPDF& qpdf,
                                                        double width,
                                                        double height,
                                                        const std::string& content,
                                                        QPDFObjectHandle font)
  {
    QPDFObjectHandle stream = qpdf.newStream(content);

    QPDFObjectHandle dictionary = stream.getDict();
    dictionary.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dictionary.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
    dictionary.replaceKey("/BBox",
                          QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(0.0, 0.0, width, height)));

    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    if(font.isDictionary())
      {
        QPDFObjectHandle fonts = QPDFObjectHandle::newDictionary();
        fonts.replaceKey("/Helv", font);
        resources.replaceKey("/Font", fonts);
      }
    dictionary.replaceKey("/Resources", resources);

    return stream;
  }

  inline std::string pdf_form_ops::box_path(double width, double height)
  {
    // a one-point black frame, drawn half a point inside the box so the
    // stroke is not clipped by it
    return "0 G 1 w 0.5 0.5 " + pdf_page_ops::number(width - 1.0) + " "
      + pdf_page_ops::number(height - 1.0) + " re S\n";
  }

  inline std::string pdf_form_ops::check_path(double width, double height)
  {
    std::string path = "0 G " + pdf_page_ops::number(std::max(1.0, 0.1 * std::min(width, height))) + " w 1 J 1 j\n";
    path += pdf_page_ops::number(0.22 * width) + " " + pdf_page_ops::number(0.50 * height) + " m\n";
    path += pdf_page_ops::number(0.42 * width) + " " + pdf_page_ops::number(0.26 * height) + " l\n";
    path += pdf_page_ops::number(0.80 * width) + " " + pdf_page_ops::number(0.76 * height) + " l S\n";

    return path;
  }

  inline std::string pdf_form_ops::circle_path(double center_x, double center_y, double radius)
  {
    // four cubic Bezier quarters; the control points sit 0.5523 of the
    // radius from each end
    const double k = 0.5523 * radius;

    auto point = [](double x, double y) {
      return pdf_page_ops::number(x) + " " + pdf_page_ops::number(y);
    };

    std::string path = point(center_x + radius, center_y) + " m\n";
    path += point(center_x + radius, center_y + k) + " " + point(center_x + k, center_y + radius) + " "
      + point(center_x, center_y + radius) + " c\n";
    path += point(center_x - k, center_y + radius) + " " + point(center_x - radius, center_y + k) + " "
      + point(center_x - radius, center_y) + " c\n";
    path += point(center_x - radius, center_y - k) + " " + point(center_x - k, center_y - radius) + " "
      + point(center_x, center_y - radius) + " c\n";
    path += point(center_x + k, center_y - radius) + " " + point(center_x + radius, center_y - k) + " "
      + point(center_x + radius, center_y) + " c\n";

    return path;
  }

  inline void pdf_form_ops::set_text_appearance(QPDF& qpdf,
                                                QPDFObjectHandle widget,
                                                const std::array<double, 4>& rect,
                                                QPDFObjectHandle font)
  {
    double width = rect[2] - rect[0];
    double height = rect[3] - rect[1];

    // qpdf writes the value between /Tx BMC and EMC and keeps the rest.
    std::string content = "q\n" + box_path(width, height) + "Q\n/Tx BMC\nEMC\n";

    QPDFObjectHandle appearances = QPDFObjectHandle::newDictionary();
    appearances.replaceKey("/N", make_appearance(qpdf, width, height, content, font));
    widget.replaceKey("/AP", appearances);

    QPDFAnnotationObjectHelper annotation(widget);
    QPDFFormFieldObjectHelper(widget).generateAppearance(annotation);
  }

  inline void pdf_form_ops::append_widget(QPDFPageObjectHelper page, QPDFObjectHandle widget)
  {
    QPDFObjectHandle page_object = page.getObjectHandle();

    QPDFObjectHandle annotations = page_object.getKey("/Annots");
    if(not annotations.isArray())
      {
        annotations = QPDFObjectHandle::newArray();
        page_object.replaceKey("/Annots", annotations);
      }

    annotations.appendItem(widget);
  }

  inline void pdf_form_ops::add_field(QPDF& qpdf, QPDFPageObjectHelper page, const nlohmann::json& spec)
  {
    if(not (spec.is_object() and spec.contains("kind") and spec["kind"].is_string()))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "a form field needs a kind");
      }
    if(not (spec.contains("name") and spec["name"].is_string()))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "a form field needs a name");
      }

    std::string kind = spec["kind"].get<std::string>();
    std::string name = spec["name"].get<std::string>();

    QPDFAcroFormDocumentHelper acroform(qpdf);
    check_new_name(acroform, name, name);

    long long flags = 0;
    if(flag_of(spec, "read_only", false)) { flags |= FLAG_READ_ONLY; }
    if(flag_of(spec, "required", false))  { flags |= FLAG_REQUIRED; }

    double font_size = 12.0;
    if(spec.contains("font_size") and not spec["font_size"].is_null())
      {
        if(not spec["font_size"].is_number() or not (spec["font_size"].get<double>() > 0.0))
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT, "a form field font size must be positive");
          }
        font_size = spec["font_size"].get<double>();
      }

    // Everything the spec can get wrong is checked before the document is
    // touched; `field` is the object that goes into the form's /Fields.
    QPDFObjectHandle field = QPDFObjectHandle::newNull();
    std::vector<QPDFObjectHandle> widgets;
    std::vector<std::array<double, 4> > text_rects;

    if(kind == "text" or kind == "choice")
      {
        std::array<double, 4> rect = rect_of(spec.value("rect", nlohmann::json()));

        std::string value;
        if(spec.contains("value") and not spec["value"].is_null())
          {
            if(not spec["value"].is_string())
              {
                throw pdf_edit_error(edit_error::INVALID_EDIT, "a form field value must be text");
              }
            value = spec["value"].get<std::string>();
          }

        field = make_widget(page, rect);
        field.replaceKey("/FT", QPDFObjectHandle::newName(kind == "text" ? "/Tx" : "/Ch"));
        field.replaceKey("/DA", QPDFObjectHandle::newString("/Helv " + pdf_page_ops::number(font_size) + " Tf 0 g"));

        if(kind == "text")
          {
            if(flag_of(spec, "multiline", false)) { flags |= FLAG_MULTILINE; }
          }
        else
          {
            const nlohmann::json options = spec.value("options", nlohmann::json());
            if(not (options.is_array() and options.size() > 0))
              {
                throw pdf_edit_error(edit_error::INVALID_EDIT,
                                     "a choice field needs a list of options");
              }

            bool editable = flag_of(spec, "editable", false);

            // a drop-down list: the kind of choice field whose appearance
            // is one line of text
            flags |= FLAG_COMBO;
            if(editable) { flags |= FLAG_EDIT; }

            bool value_is_option = false;
            QPDFObjectHandle entries = QPDFObjectHandle::newArray();
            for(const nlohmann::json& option : options)
              {
                if(not option.is_string())
                  {
                    throw pdf_edit_error(edit_error::INVALID_EDIT, "choice field options must be text");
                  }
                value_is_option = value_is_option or (option.get<std::string>() == value);
                entries.appendItem(QPDFObjectHandle::newUnicodeString(option.get<std::string>()));
              }

            if(not value.empty() and not value_is_option and not editable)
              {
                throw pdf_edit_error(edit_error::INVALID_EDIT,
                                     "'" + value + "' is not one of the options of choice field '" + name + "'");
              }

            field.replaceKey("/Opt", entries);
          }

        if(not value.empty())
          {
            field.replaceKey("/V", QPDFObjectHandle::newUnicodeString(value));
          }

        widgets.push_back(field);
        text_rects.push_back(rect);
      }
    else if(kind == "checkbox")
      {
        std::array<double, 4> rect = rect_of(spec.value("rect", nlohmann::json()));
        bool checked = flag_of(spec, "checked", false);

        double width = rect[2] - rect[0];
        double height = rect[3] - rect[1];

        QPDFObjectHandle states = QPDFObjectHandle::newDictionary();
        states.replaceKey("/Yes", make_appearance(qpdf, width, height,
                                                  "q\n" + box_path(width, height) + check_path(width, height) + "Q\n",
                                                  QPDFObjectHandle::newNull()));
        states.replaceKey("/Off", make_appearance(qpdf, width, height,
                                                  "q\n" + box_path(width, height) + "Q\n",
                                                  QPDFObjectHandle::newNull()));

        QPDFObjectHandle appearances = QPDFObjectHandle::newDictionary();
        appearances.replaceKey("/N", states);

        field = make_widget(page, rect);
        field.replaceKey("/FT", QPDFObjectHandle::newName("/Btn"));
        field.replaceKey("/V", QPDFObjectHandle::newName(checked ? "/Yes" : "/Off"));
        field.replaceKey("/AS", QPDFObjectHandle::newName(checked ? "/Yes" : "/Off"));
        field.replaceKey("/AP", appearances);

        widgets.push_back(field);
      }
    else if(kind == "radio")
      {
        const nlohmann::json options = spec.value("options", nlohmann::json());
        if(not (options.is_array() and options.size() > 0))
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT,
                                 "a radio group needs a list of buttons, each with a state and a rect");
          }

        std::string selected = "/Off";
        if(spec.contains("selected") and not spec["selected"].is_null())
          {
            if(not spec["selected"].is_string())
              {
                throw pdf_edit_error(edit_error::INVALID_EDIT, "the selected radio button is a state name");
              }
            selected = with_slash(spec["selected"].get<std::string>());
          }

        // the group is the field; its buttons are widgets below it
        field = QPDFObjectHandle::newDictionary();
        field.replaceKey("/FT", QPDFObjectHandle::newName("/Btn"));
        flags |= FLAG_RADIO | FLAG_NO_TOGGLE_TO_OFF;

        std::set<std::string> states_seen;
        for(const nlohmann::json& option : options)
          {
            if(not (option.is_object() and option.contains("state") and option["state"].is_string()))
              {
                throw pdf_edit_error(edit_error::INVALID_EDIT, "a radio button needs a state name");
              }

            std::string state = with_slash(option["state"].get<std::string>());
            if(state == "/Off" or state == "/" or not states_seen.insert(state).second)
              {
                throw pdf_edit_error(edit_error::INVALID_EDIT,
                                     "radio button states must be distinct names other than Off");
              }

            std::array<double, 4> rect = rect_of(option.value("rect", nlohmann::json()));

            double width = rect[2] - rect[0];
            double height = rect[3] - rect[1];
            double radius = 0.5 * std::min(width, height) - 1.0;

            std::string ring = "0 G 1 w\n" + circle_path(0.5 * width, 0.5 * height, std::max(radius, 1.0)) + "S\n";
            std::string dot = "0 g\n" + circle_path(0.5 * width, 0.5 * height, std::max(0.5 * radius, 0.5)) + "f\n";

            QPDFObjectHandle button_states = QPDFObjectHandle::newDictionary();
            button_states.replaceKey(state, make_appearance(qpdf, width, height, "q\n" + ring + dot + "Q\n",
                                                           QPDFObjectHandle::newNull()));
            button_states.replaceKey("/Off", make_appearance(qpdf, width, height, "q\n" + ring + "Q\n",
                                                            QPDFObjectHandle::newNull()));

            QPDFObjectHandle appearances = QPDFObjectHandle::newDictionary();
            appearances.replaceKey("/N", button_states);

            QPDFObjectHandle widget = make_widget(page, rect);
            widget.replaceKey("/AS", QPDFObjectHandle::newName(state == selected ? state : "/Off"));
            widget.replaceKey("/AP", appearances);

            widgets.push_back(widget);
          }

        if(selected != "/Off" and states_seen.count(selected) == 0)
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT,
                                 "'" + selected + "' is not a button of radio group '" + name + "'");
          }

        field.replaceKey("/V", QPDFObjectHandle::newName(selected));
      }
    else if(kind == "signature")
      {
        std::array<double, 4> rect = rect_of(spec.value("rect", nlohmann::json()));

        double width = rect[2] - rect[0];
        double height = rect[3] - rect[1];

        // an empty signature field: a frame where a signing application
        // puts the signature
        QPDFObjectHandle appearances = QPDFObjectHandle::newDictionary();
        appearances.replaceKey("/N", make_appearance(qpdf, width, height,
                                                     "q\n" + box_path(width, height) + "Q\n",
                                                     QPDFObjectHandle::newNull()));

        field = make_widget(page, rect);
        field.replaceKey("/FT", QPDFObjectHandle::newName("/Sig"));
        field.replaceKey("/AP", appearances);

        widgets.push_back(field);
      }
    else
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "'" + kind + "' is not a kind of form field: expected text, checkbox, radio, "
                             "choice or signature");
      }

    // ---- from here on the document changes ----

    QPDFObjectHandle font = ensure_form_font(qpdf, acroform);

    field.replaceKey("/T", QPDFObjectHandle::newUnicodeString(name));
    if(flags != 0)
      {
        field.replaceKey("/Ff", QPDFObjectHandle::newInteger(flags));
      }
    if(spec.contains("tooltip") and spec["tooltip"].is_string())
      {
        field.replaceKey("/TU", QPDFObjectHandle::newUnicodeString(spec["tooltip"].get<std::string>()));
      }

    field = qpdf.makeIndirectObject(field);

    if(kind == "radio")
      {
        QPDFObjectHandle kids = QPDFObjectHandle::newArray();
        for(QPDFObjectHandle& widget : widgets)
          {
            widget.replaceKey("/Parent", field);
            widget = qpdf.makeIndirectObject(widget);
            kids.appendItem(widget);
          }
        field.replaceKey("/Kids", kids);
      }
    else
      {
        // field and widget are one dictionary
        widgets[0] = field;
      }

    for(QPDFObjectHandle& widget : widgets)
      {
        append_widget(page, widget);
      }

    acroform.addFormField(QPDFFormFieldObjectHelper(field));

    for(std::size_t index = 0; index < text_rects.size(); index++)
      {
        set_text_appearance(qpdf, widgets[index], text_rects[index], font);
      }
  }

  inline void pdf_form_ops::update_field(QPDF& qpdf, const std::string& name, const nlohmann::json& changes)
  {
    if(not changes.is_object())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "form field changes must be a dictionary");
      }

    QPDFAcroFormDocumentHelper acroform(qpdf);
    if(not acroform.hasAcroForm())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "the document has no form");
      }

    std::vector<QPDFFormFieldObjectHelper> fields = fields_of(acroform);
    QPDFFormFieldObjectHelper field = find_field(fields, name);
    QPDFObjectHandle field_object = field.getObjectHandle();

    // ---- validate everything first ----

    std::string new_name;
    if(changes.contains("name"))
      {
        if(not changes["name"].is_string())
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT, "a form field name must be text");
          }
        new_name = changes["name"].get<std::string>();

        // the new fully qualified name: same parents, new last part
        std::string qualified = field.getFullyQualifiedName();
        std::string partial = field.getPartialName();
        std::string prefix = (qualified.size() >= partial.size()) ?
          qualified.substr(0, qualified.size() - partial.size()) : "";

        if(new_name != partial)
          {
            check_new_name(acroform, new_name, prefix + new_name);
          }
      }

    std::vector<QPDFAnnotationObjectHelper> widgets = widgets_of(acroform, field);

    std::array<double, 4> rect = {0.0, 0.0, 0.0, 0.0};
    if(changes.contains("rect"))
      {
        rect = rect_of(changes["rect"]);
        if(widgets.size() != 1)
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT,
                                 "form field '" + name + "' has " + std::to_string(widgets.size())
                                 + " widgets: only a field with one widget can be given a new rectangle");
          }
      }

    QPDFObjectHandle options = QPDFObjectHandle::newNull();
    if(changes.contains("options"))
      {
        if(not field.isChoice())
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT,
                                 "form field '" + name + "' is not a choice field and has no options");
          }
        if(not (changes["options"].is_array() and changes["options"].size() > 0))
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT, "a choice field needs a list of options");
          }

        options = QPDFObjectHandle::newArray();
        for(const nlohmann::json& option : changes["options"])
          {
            if(not option.is_string())
              {
                throw pdf_edit_error(edit_error::INVALID_EDIT, "choice field options must be text");
              }
            options.appendItem(QPDFObjectHandle::newUnicodeString(option.get<std::string>()));
          }
      }

    long long flags = field.getFlags();
    bool flags_changed = false;
    for(const auto& flag : {std::make_pair("read_only", FLAG_READ_ONLY), std::make_pair("required", FLAG_REQUIRED)})
      {
        if(changes.contains(flag.first) and not changes[flag.first].is_null())
          {
            flags = flag_of(changes, flag.first, false) ? (flags | flag.second) : (flags & ~flag.second);
            flags_changed = true;
          }
      }

    if(changes.contains("tooltip") and not (changes["tooltip"].is_null() or changes["tooltip"].is_string()))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "a form field tooltip must be text");
      }

    // ---- apply ----

    if(flags_changed)
      {
        field_object.replaceKey("/Ff", QPDFObjectHandle::newInteger(flags));
      }

    if(changes.contains("tooltip"))
      {
        if(changes["tooltip"].is_null())
          {
            field_object.removeKey("/TU");
          }
        else
          {
            field_object.replaceKey("/TU", QPDFObjectHandle::newUnicodeString(changes["tooltip"].get<std::string>()));
          }
      }

    if(options.isArray())
      {
        field_object.replaceKey("/Opt", options);
      }

    if(changes.contains("rect"))
      {
        QPDFObjectHandle widget = widgets[0].getObjectHandle();
        widget.replaceKey("/Rect",
                          QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(rect[0], rect[1], rect[2], rect[3])));

        // A viewer scales an appearance to the rectangle, which is right
        // for a check mark and wrong for text: text is laid out again.
        if(field.isText() or field.isChoice())
          {
            set_text_appearance(qpdf, widget, rect, ensure_form_font(qpdf, acroform));
          }
      }

    if(changes.contains("name") and new_name != field.getPartialName())
      {
        acroform.setFormFieldName(field, new_name);
      }
  }

  inline void pdf_form_ops::remove_field(QPDF& qpdf, const std::string& name)
  {
    QPDFAcroFormDocumentHelper acroform(qpdf);
    if(not acroform.hasAcroForm())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "the document has no form");
      }

    std::vector<QPDFFormFieldObjectHelper> fields = fields_of(acroform);
    QPDFFormFieldObjectHelper field = find_field(fields, name);
    QPDFObjectHandle field_object = field.getObjectHandle();

    std::set<QPDFObjGen> widget_ids;
    for(QPDFAnnotationObjectHelper& widget : widgets_of(acroform, field))
      {
        widget_ids.insert(widget.getObjectHandle().getObjGen());
      }

    // the widgets leave the pages that show them
    for(QPDFPageObjectHelper& page : QPDFPageDocumentHelper(qpdf).getAllPages())
      {
        QPDFObjectHandle page_object = page.getObjectHandle();
        QPDFObjectHandle annotations = page_object.getKey("/Annots");
        if(not annotations.isArray())
          {
            continue;
          }

        std::vector<QPDFObjectHandle> kept;
        bool changed = false;
        for(QPDFObjectHandle& annotation : annotations.getArrayAsVector())
          {
            if(annotation.isIndirect() and widget_ids.count(annotation.getObjGen()) > 0)
              {
                changed = true;
              }
            else
              {
                kept.push_back(annotation);
              }
          }

        if(not changed)
          {
            continue;
          }

        if(kept.empty()) { page_object.removeKey("/Annots"); }
        else             { page_object.replaceKey("/Annots", QPDFObjectHandle::newArray(kept)); }
      }

    // the field leaves the form: from its parent's /Kids when it has a
    // parent, from the form's /Fields otherwise
    QPDFObjectHandle parent = field_object.getKey("/Parent");
    QPDFObjectHandle kids = parent.isDictionary() ? parent.getKey("/Kids") : QPDFObjectHandle::newNull();
    if(kids.isArray())
      {
        for(int index = kids.getArrayNItems() - 1; index >= 0; index--)
          {
            QPDFObjectHandle kid = kids.getArrayItem(index);
            if(kid.isIndirect() and kid.getObjGen() == field_object.getObjGen())
              {
                kids.eraseItem(index);
              }
          }
        acroform.invalidateCache();
      }
    else
      {
        acroform.removeFormFields({field_object.getObjGen()});
      }
  }

}

#endif
