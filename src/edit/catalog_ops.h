//-*-C++-*-

#ifndef PDF_CATALOG_OPS_H
#define PDF_CATALOG_OPS_H

#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#define POINTERHOLDER_TRANSITION 0 // eliminate warnings from QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>
#include <qpdf/QPDFNumberTreeObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageLabelDocumentHelper.hh>

#include <edit/edit_error.h>
#include <edit/object_json.h>

namespace pdflib
{
  // Document-level entries of the trailer and the catalog: /Info, XMP, page
  // layout and mode, viewer preferences, the open action, page labels and
  // named destinations. Stateless; the caller owns the QPDF and serializes
  // access to it.
  class pdf_catalog_ops
  {
  public:

    // ---- /Info and XMP (ISO 32000-1, 14.3) ----

    // Set, or with no value remove, entries of the /Info dictionary. Keys are
    // accepted with or without their leading slash.
    static void set_info(QPDF& qpdf,
                         const std::map<std::string, std::optional<std::string> >& entries);

    // Replace the catalog's /Metadata stream by this XMP packet, or remove it.
    static void set_xmp(QPDF& qpdf, const std::optional<std::string>& packet);

    // ---- viewer settings (ISO 32000-1, 7.7.2 and 12.2) ----

    // {"page_layout", "page_mode", "viewer_preferences", "open_destination"}
    static nlohmann::json read_settings(QPDF& qpdf);

    // Empty name removes the entry.
    static void set_page_layout(QPDF& qpdf, const std::string& name);
    static void set_page_mode(QPDF& qpdf, const std::string& name);

    // `dictionary` is the new /ViewerPreferences in PDF syntax; empty removes it.
    static void set_viewer_preferences(QPDF& qpdf, const std::string& dictionary);

    // Open the document at this page, fitted to the window; -1 removes the
    // open action.
    static void set_open_destination(QPDF& qpdf, int page);

    // ---- page labels (ISO 32000-1, 12.4.2) ----

    // The label of every page, as a viewer shows it; "1", "2", ... for a
    // document without /PageLabels.
    static std::vector<std::string> page_labels(
      QPDF& qpdf, std::optional<std::size_t> projected_count = std::nullopt);

    // Label pages from..to (inclusive). `style` is "/D", "/R", "/r", "/A",
    // "/a" or empty for a prefix-only label; `start` is the number of the
    // first page of the range. Pages after `to` keep the labels they had.
    static void set_page_label(QPDF& qpdf,
                               int from,
                               int to,
                               const std::string& style,
                               const std::string& prefix,
                               int start);

    // After a page-tree change, carry existing labels across a splice.
    static void splice_page_labels(QPDF& qpdf, int at, int removed, int inserted);

    // ---- named destinations (ISO 32000-1, 12.3.2.3) ----

    // {name: {"page", "kind", "arguments"}} from the /Dests dictionary and
    // the /Names /Dests name tree.
    static nlohmann::json named_destinations(QPDF& qpdf);

    // Add or replace a name that opens `page` fitted to the window.
    static void add_named_destination(QPDF& qpdf, const std::string& name, int page);

    // A destination array [page /Fit] for a page of `qpdf`.
    static QPDFObjectHandle fit_destination(QPDF& qpdf, int page);

  private:

    static std::string strip_slash(const std::string& key);

    static void set_root_name(QPDF& qpdf,
                              const std::string& key,
                              const std::string& name,
                              const std::set<std::string>& allowed);

    static std::string format_label(QPDFObjectHandle label);
    static void write_page_labels(QPDF& qpdf, std::map<long long, QPDFObjectHandle>& ranges);
    static std::string to_roman(long long number);
    static std::string to_letters(long long number);
  };

  inline std::string pdf_catalog_ops::strip_slash(const std::string& key)
  {
    return (key.size() > 0 and key.front() == '/') ? key.substr(1) : key;
  }

  inline void pdf_catalog_ops::set_info(QPDF& qpdf,
                                        const std::map<std::string, std::optional<std::string> >& entries)
  {
    QPDFObjectHandle trailer = qpdf.getTrailer();

    QPDFObjectHandle info = trailer.getKey("/Info");
    if(not info.isDictionary())
      {
        info = qpdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
        trailer.replaceKey("/Info", info);
      }

    for(const auto& entry : entries)
      {
        std::string name = strip_slash(entry.first);
        if(name.empty())
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT, "a metadata key must not be empty");
          }

        std::string key = "/" + name;
        if(entry.second.has_value())
          {
            info.replaceKey(key, QPDFObjectHandle::newUnicodeString(entry.second.value()));
          }
        else
          {
            info.removeKey(key);
          }
      }
  }

  inline void pdf_catalog_ops::set_xmp(QPDF& qpdf, const std::optional<std::string>& packet)
  {
    QPDFObjectHandle root = qpdf.getRoot();

    if(not packet.has_value())
      {
        root.removeKey("/Metadata");
        return;
      }

    QPDFObjectHandle stream = qpdf.newStream(packet.value());

    QPDFObjectHandle dictionary = stream.getDict();
    dictionary.replaceKey("/Type", QPDFObjectHandle::newName("/Metadata"));
    dictionary.replaceKey("/Subtype", QPDFObjectHandle::newName("/XML"));

    root.replaceKey("/Metadata", stream);
  }

  inline nlohmann::json pdf_catalog_ops::read_settings(QPDF& qpdf)
  {
    QPDFObjectHandle root = qpdf.getRoot();

    nlohmann::json result = nlohmann::json::object();

    QPDFObjectHandle layout = root.getKey("/PageLayout");
    result["page_layout"] = layout.isName() ? nlohmann::json(layout.getName()) : nlohmann::json(nullptr);

    QPDFObjectHandle mode = root.getKey("/PageMode");
    result["page_mode"] = mode.isName() ? nlohmann::json(mode.getName()) : nlohmann::json(nullptr);

    QPDFObjectHandle preferences = root.getKey("/ViewerPreferences");
    result["viewer_preferences"] = preferences.isDictionary() ?
      pdf_object_json::to_json(preferences, 3) : nlohmann::json(nullptr);

    QPDFOutlineDocumentHelper resolver(qpdf);
    result["open_destination"] = pdf_object_json::destination_to_json(
      pdf_object_json::resolve_destination(root.getKey("/OpenAction"), resolver),
      pdf_object_json::page_indices(qpdf));

    return result;
  }

  inline void pdf_catalog_ops::set_root_name(QPDF& qpdf,
                                             const std::string& key,
                                             const std::string& name,
                                             const std::set<std::string>& allowed)
  {
    QPDFObjectHandle root = qpdf.getRoot();

    if(name.empty())
      {
        root.removeKey(key);
        return;
      }

    std::string value = "/" + strip_slash(name);
    if(allowed.count(value) == 0)
      {
        std::string choices;
        for(const std::string& choice : allowed)
          {
            choices += (choices.empty() ? "" : ", ") + choice;
          }
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "'" + name + "' is not a valid " + key + ": expected one of " + choices);
      }

    root.replaceKey(key, QPDFObjectHandle::newName(value));
  }

  inline void pdf_catalog_ops::set_page_layout(QPDF& qpdf, const std::string& name)
  {
    set_root_name(qpdf, "/PageLayout", name,
                  {"/SinglePage", "/OneColumn", "/TwoColumnLeft", "/TwoColumnRight",
                   "/TwoPageLeft", "/TwoPageRight"});
  }

  inline void pdf_catalog_ops::set_page_mode(QPDF& qpdf, const std::string& name)
  {
    set_root_name(qpdf, "/PageMode", name,
                  {"/UseNone", "/UseOutlines", "/UseThumbs", "/FullScreen",
                   "/UseOC", "/UseAttachments"});
  }

  inline void pdf_catalog_ops::set_viewer_preferences(QPDF& qpdf, const std::string& dictionary)
  {
    QPDFObjectHandle root = qpdf.getRoot();

    if(dictionary.empty())
      {
        root.removeKey("/ViewerPreferences");
        return;
      }

    QPDFObjectHandle preferences = QPDFObjectHandle::parse(dictionary, "viewer preferences");
    if(not preferences.isDictionary())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "viewer preferences must be a dictionary");
      }

    root.replaceKey("/ViewerPreferences", preferences);
  }

  inline QPDFObjectHandle pdf_catalog_ops::fit_destination(QPDF& qpdf, int page)
  {
    std::vector<QPDFObjectHandle> pages = qpdf.getAllPages();
    if(page < 0 or page >= static_cast<int>(pages.size()))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "page index " + std::to_string(page) + " is out of range for a document of "
                             + std::to_string(pages.size()) + " pages");
      }

    QPDFObjectHandle destination = QPDFObjectHandle::newArray();
    destination.appendItem(pages.at(page));
    destination.appendItem(QPDFObjectHandle::newName("/Fit"));

    return destination;
  }

  inline void pdf_catalog_ops::set_open_destination(QPDF& qpdf, int page)
  {
    QPDFObjectHandle root = qpdf.getRoot();

    if(page < 0)
      {
        root.removeKey("/OpenAction");
        return;
      }

    root.replaceKey("/OpenAction", fit_destination(qpdf, page));
  }

  inline std::string pdf_catalog_ops::to_roman(long long number)
  {
    static const std::vector<std::pair<long long, const char*> > numerals = {
      {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"},
      {50, "l"}, {40, "xl"}, {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}};

    std::string result;
    for(const auto& numeral : numerals)
      {
        while(number >= numeral.first)
          {
            result += numeral.second;
            number -= numeral.first;
          }
      }

    return result;
  }

  inline std::string pdf_catalog_ops::to_letters(long long number)
  {
    // a..z, then aa..zz, then aaa..zzz (ISO 32000-1, table 159).
    if(number < 1)
      {
        return "";
      }

    char letter = static_cast<char>('a' + ((number - 1) % 26));
    std::size_t repeats = static_cast<std::size_t>((number - 1) / 26 + 1);

    return std::string(repeats, letter);
  }

  inline std::string pdf_catalog_ops::format_label(QPDFObjectHandle label)
  {
    std::string prefix;
    QPDFObjectHandle prefix_object = label.getKey("/P");
    if(prefix_object.isString())
      {
        prefix = prefix_object.getUTF8Value();
      }

    QPDFObjectHandle style = label.getKey("/S");
    if(not style.isName())
      {
        // no numeric portion: the label is the prefix alone
        return prefix;
      }

    long long number = 1;
    QPDFObjectHandle start = label.getKey("/St");
    if(start.isInteger())
      {
        number = start.getIntValue();
      }

    std::string name = style.getName();
    std::string numeric;

    if(name == "/D")
      {
        numeric = std::to_string(number);
      }
    else if(name == "/r" or name == "/R")
      {
        numeric = to_roman(number);
      }
    else if(name == "/a" or name == "/A")
      {
        numeric = to_letters(number);
      }

    if(name == "/R" or name == "/A")
      {
        for(char& character : numeric)
          {
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
          }
      }

    return prefix + numeric;
  }

  inline std::vector<std::string> pdf_catalog_ops::page_labels(
    QPDF& qpdf, std::optional<std::size_t> projected_count)
  {
    std::size_t number_of_pages = projected_count.value_or(qpdf.getAllPages().size());

    std::vector<std::string> labels;
    labels.reserve(number_of_pages);

    QPDFPageLabelDocumentHelper helper(qpdf);
    bool has_labels = helper.hasPageLabels();

    for(std::size_t index = 0; index < number_of_pages; index++)
      {
        // qpdf returns the label dictionary that applies to this page, with
        // /St already advanced to this page's number.
        QPDFObjectHandle label = has_labels ?
          helper.getLabelForPage(static_cast<long long>(index)) : QPDFObjectHandle::newNull();

        if(label.isDictionary())
          {
            labels.push_back(format_label(label));
          }
        else
          {
            labels.push_back(std::to_string(index + 1));
          }
      }

    return labels;
  }

  inline void pdf_catalog_ops::set_page_label(QPDF& qpdf,
                                              int from,
                                              int to,
                                              const std::string& style,
                                              const std::string& prefix,
                                              int start)
  {
    int number_of_pages = static_cast<int>(qpdf.getAllPages().size());

    if(from < 0 or to < from or to >= number_of_pages)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "page label range " + std::to_string(from) + ".." + std::to_string(to)
                             + " is not within a document of " + std::to_string(number_of_pages) + " pages");
      }
    if(start < 1)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "a page label numbering starts at 1 or higher");
      }

    qpdf_page_label_e label_type = pl_none;
    if(style == "/D")      { label_type = pl_digits; }
    else if(style == "/R") { label_type = pl_roman_upper; }
    else if(style == "/r") { label_type = pl_roman_lower; }
    else if(style == "/A") { label_type = pl_alpha_upper; }
    else if(style == "/a") { label_type = pl_alpha_lower; }
    else if(not style.empty())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "'" + style + "' is not a page label style: expected /D, /R, /r, /A or /a");
      }

    // The ranges as they are now, by first page. A document without labels
    // behaves as one decimal range from the first page.
    std::map<long long, QPDFObjectHandle> ranges;

    QPDFObjectHandle root = qpdf.getRoot();
    QPDFObjectHandle existing = root.getKey("/PageLabels");

    QPDFPageLabelDocumentHelper helper(qpdf);
    bool has_labels = helper.hasPageLabels();

    if(has_labels and existing.isDictionary())
      {
        QPDFNumberTreeObjectHelper tree(existing, qpdf);
        for(auto itr = tree.begin(); itr != tree.end(); ++itr)
          {
            ranges[itr->first] = itr->second;
          }
      }
    else
      {
        ranges[0] = QPDFPageLabelDocumentHelper::pageLabelDict(pl_digits, 1, "");
      }

    // What the page after the range is labelled today: it has to say so
    // itself once the range in front of it changes.
    QPDFObjectHandle following = QPDFObjectHandle::newNull();
    if(to + 1 < number_of_pages)
      {
        following = has_labels ?
          helper.getLabelForPage(static_cast<long long>(to) + 1) :
          QPDFPageLabelDocumentHelper::pageLabelDict(pl_digits, to + 2, "");
      }

    for(auto itr = ranges.begin(); itr != ranges.end(); )
      {
        if(itr->first >= from and itr->first <= static_cast<long long>(to) + 1)
          {
            itr = ranges.erase(itr);
          }
        else
          {
            ++itr;
          }
      }

    ranges[from] = QPDFPageLabelDocumentHelper::pageLabelDict(label_type, start, prefix);
    if(following.isDictionary())
      {
        ranges[static_cast<long long>(to) + 1] = following;
      }

    write_page_labels(qpdf, ranges);
  }

  inline void pdf_catalog_ops::splice_page_labels(QPDF& qpdf, int at, int removed, int inserted)
  {
    auto root = qpdf.getRoot();
    auto existing = root.getKey("/PageLabels");
    if(not existing.isDictionary() or removed + inserted == 0)
      {
        return;
      }

    const auto new_count = static_cast<long long>(qpdf.getAllPages().size());
    const auto old_count = new_count + removed - inserted;
    if(at == old_count)
      {
        return; // Appended pages continue the last numbering range.
      }
    if(new_count == 0)
      {
        root.removeKey("/PageLabels");
        return;
      }

    QPDFPageLabelDocumentHelper helper(qpdf);
    QPDFNumberTreeObjectHelper tree(existing, qpdf);
    std::map<long long, QPDFObjectHandle> ranges;
    const long long following = static_cast<long long>(at) + removed;
    for(auto entry = tree.begin(); entry != tree.end(); ++entry)
      {
        const auto index = entry->first;
        if(index >= 0 and index < at)
          {
            ranges[index] = entry->second;
          }
        else if(index > following and index < old_count)
          {
            ranges[index + inserted - removed] = entry->second;
          }
      }

    if(inserted > 0)
      {
        ranges[at] = QPDFPageLabelDocumentHelper::pageLabelDict(pl_digits, at + 1, "");
      }
    if(following < old_count)
      {
        auto label = helper.getLabelForPage(following);
        ranges[at + inserted] = label.isDictionary() ? label
          : QPDFPageLabelDocumentHelper::pageLabelDict(pl_digits, following + 1, "");
      }

    write_page_labels(qpdf, ranges);
  }

  inline void pdf_catalog_ops::write_page_labels(
    QPDF& qpdf, std::map<long long, QPDFObjectHandle>& ranges)
  {
    // The number tree shall have an entry for page 0 (ISO 32000-1, 12.4.2).
    if(ranges.count(0) == 0)
      {
        ranges[0] = QPDFPageLabelDocumentHelper::pageLabelDict(pl_digits, 1, "");
      }

    QPDFObjectHandle numbers = QPDFObjectHandle::newArray();
    for(const auto& range : ranges)
      {
        numbers.appendItem(QPDFObjectHandle::newInteger(range.first));
        numbers.appendItem(range.second);
      }

    QPDFObjectHandle labels = QPDFObjectHandle::newDictionary();
    labels.replaceKey("/Nums", numbers);

    qpdf.getRoot().replaceKey("/PageLabels", labels);
  }

  inline nlohmann::json pdf_catalog_ops::named_destinations(QPDF& qpdf)
  {
    nlohmann::json result = nlohmann::json::object();

    std::map<QPDFObjGen, int> indices = pdf_object_json::page_indices(qpdf);
    QPDFOutlineDocumentHelper resolver(qpdf);
    QPDFObjectHandle root = qpdf.getRoot();

    // PDF 1.1: a dictionary of names in the catalog.
    QPDFObjectHandle dictionary = root.getKey("/Dests");
    if(dictionary.isDictionary())
      {
        for(const std::string& key : dictionary.getKeys())
          {
            nlohmann::json destination =
              pdf_object_json::destination_to_json(
                pdf_object_json::resolve_destination(dictionary.getKey(key), resolver), indices);
            if(not destination.is_null())
              {
                result[strip_slash(key)] = destination;
              }
          }
      }

    // PDF 1.2 and later: a name tree under /Names.
    QPDFObjectHandle names = root.getKey("/Names");
    if(names.isDictionary() and names.getKey("/Dests").isDictionary())
      {
        QPDFNameTreeObjectHelper tree(names.getKey("/Dests"), qpdf);
        for(auto itr = tree.begin(); itr != tree.end(); ++itr)
          {
            nlohmann::json destination =
              pdf_object_json::destination_to_json(
                pdf_object_json::resolve_destination(itr->second, resolver), indices);
            if(not destination.is_null())
              {
                result[itr->first] = destination;
              }
          }
      }

    return result;
  }

  inline void pdf_catalog_ops::add_named_destination(QPDF& qpdf, const std::string& name, int page)
  {
    if(name.empty())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "a named destination needs a name");
      }

    QPDFObjectHandle destination = fit_destination(qpdf, page);

    QPDFObjectHandle root = qpdf.getRoot();

    QPDFObjectHandle names = root.getKey("/Names");
    if(not names.isDictionary())
      {
        names = QPDFObjectHandle::newDictionary();
        root.replaceKey("/Names", names);
      }

    QPDFObjectHandle existing = names.getKey("/Dests");
    if(existing.isDictionary())
      {
        QPDFNameTreeObjectHelper tree(existing, qpdf);
        tree.insert(name, destination);
      }
    else
      {
        QPDFNameTreeObjectHelper tree = QPDFNameTreeObjectHelper::newEmpty(qpdf);
        tree.insert(name, destination);
        names.replaceKey("/Dests", tree.getObjectHandle());
      }
  }

}

#endif
