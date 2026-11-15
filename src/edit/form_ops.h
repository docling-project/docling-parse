//-*-C++-*-

#ifndef PDF_FORM_OPS_H
#define PDF_FORM_OPS_H

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

#include <edit/edit_error.h>

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

  private:

    static std::string kind_of(QPDFFormFieldObjectHelper& field);

    // The names of a widget's normal appearances other than /Off.
    static std::set<std::string> states_of(QPDFAcroFormDocumentHelper& acroform,
                                           QPDFFormFieldObjectHelper& field);

    static std::string with_slash(const std::string& name);
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

  inline std::set<std::string> pdf_form_ops::states_of(QPDFAcroFormDocumentHelper& acroform,
                                                       QPDFFormFieldObjectHelper& field)
  {
    std::set<std::string> states;

    for(QPDFAnnotationObjectHelper& widget : acroform.getAnnotationsForField(field))
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

    // Which pages show a widget of which field.
    std::map<QPDFObjGen, std::set<int> > pages_of;
    int page_index = 0;
    for(QPDFPageObjectHelper& page : QPDFPageDocumentHelper(qpdf).getAllPages())
      {
        for(QPDFFormFieldObjectHelper& field : acroform.getFormFieldsForPage(page))
          {
            pages_of[field.getObjectHandle().getObjGen()].insert(page_index);
          }
        page_index += 1;
      }

    for(QPDFFormFieldObjectHelper& field : acroform.getFormFields())
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

        entry["pages"] = nlohmann::json::array();
        auto itr = pages_of.find(field.getObjectHandle().getObjGen());
        if(itr != pages_of.end())
          {
            for(int page : itr->second)
              {
                entry["pages"].push_back(page);
              }
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

    std::vector<QPDFFormFieldObjectHelper> fields = acroform.getFormFields();

    int updated = 0;

    for(const auto& entry : values)
      {
        const std::string& name = entry.first;
        const std::string& value = entry.second;

        // Fully qualified name first; a partial name only when unambiguous.
        std::vector<QPDFFormFieldObjectHelper> matches;
        for(QPDFFormFieldObjectHelper& field : fields)
          {
            if(field.getFullyQualifiedName() == name)
              {
                matches.push_back(field);
              }
          }
        if(matches.empty())
          {
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
          }
        if(matches.empty())
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT,
                                 "the form has no field named '" + name + "'");
          }

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

}

#endif
