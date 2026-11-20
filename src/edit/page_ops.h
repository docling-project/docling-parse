//-*-C++-*-

#ifndef PDF_PAGE_OPS_H
#define PDF_PAGE_OPS_H

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#define POINTERHOLDER_TRANSITION 0 // eliminate warnings from QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFMatrix.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QUtil.hh>

#include <edit/edit_error.h>

namespace pdflib
{
  // The geometry of one page as the page tree defines it, with inherited
  // attributes resolved (ISO 32000-1, 7.7.3.4) and the box fallbacks of
  // 14.11.2 applied: /CropBox defaults to /MediaBox, the other three to
  // /CropBox. Boxes are [llx, lly, urx, ury] in default user space units.
  class pdf_page_info
  {
  public:

    typedef std::array<double, 4> box_type;

    pdf_page_info();

    // Caller serializes access to the QPDF that owns `page`.
    static pdf_page_info read(QPDFPageObjectHelper page);

    const box_type& get_media_box() const;
    const box_type& get_crop_box() const;
    const box_type& get_bleed_box() const;
    const box_type& get_trim_box() const;
    const box_type& get_art_box() const;

    int get_rotation() const;     // /Rotate, normalized to 0, 90, 180 or 270
    double get_user_unit() const; // /UserUnit, 1.0 when absent

  private:

    static box_type to_box(QPDFObjectHandle handle, const box_type& fallback);

  private:

    box_type media_box;
    box_type crop_box;
    box_type bleed_box;
    box_type trim_box;
    box_type art_box;

    int rotation;
    double user_unit;
  };

  // QPDF mechanics for single-page edits. Stateless; the caller owns the QPDF
  // and serializes access to it.
  class pdf_page_ops
  {
  public:

    // "media" | "crop" | "bleed" | "trim" | "art" -> "/MediaBox" ...
    static std::string box_key(const std::string& name);

    static void set_box(QPDFPageObjectHelper page,
                        const std::string& name,
                        const pdf_page_info::box_type& box);

    // `angle` is a multiple of 90; relative adds to the current /Rotate.
    static void rotate(QPDFPageObjectHelper page, int angle, bool relative);

    // A new, empty page object of the given size that is not yet in the page
    // tree of `qpdf`.
    static QPDFObjectHandle make_blank_page(QPDF& qpdf, double width, double height);

    // Apply the matrix [a b c d e f] to everything the page shows: its
    // content, by concatenating the matrix to the CTM in front of it (ISO
    // 32000-1, 8.3.4), and its annotations, whose rectangles and appearances
    // are not part of the content stream. Transform the page boxes only
    // for scaling or transferring the display rotation into the content.
    static void transform(QPDF& qpdf,
                          QPDFPageObjectHelper page,
                          const std::array<double, 6>& matrix,
                          bool transform_boxes = false);

    // Draw the form XObject `form` (already owned by `qpdf`) on the page,
    // above its content or, with under, below it.
    //
    //   fit=false: at the form's own coordinates transformed by `matrix`;
    //              with the identity the two pages are superimposed as if
    //              printed on the same sheet.
    //   fit=true:  centered in the page's trim box, scaled down or up to
    //              fill it, and upright with respect to the page's /Rotate.
    //              `matrix` must be the identity.
    static void overlay(QPDF& qpdf,
                        QPDFPageObjectHelper page,
                        QPDFObjectHandle form,
                        bool under,
                        bool fit,
                        const std::array<double, 6>& matrix,
                        QPDFObjectHandle expand_to);

    // ---- new content ----

    // Add `content` (content stream operators) on top of what the page
    // shows. The existing content is closed off first, so the new operators
    // start from the default graphics state.
    static void append_content(QPDF& qpdf, QPDFPageObjectHelper page, const std::string& content);

    // A font dictionary for one of the standard 14 fonts that take Latin
    // text (ISO 32000-1, 9.6.2.2), with WinAnsiEncoding. These need no
    // embedded font program: every viewer supplies them.
    static QPDFObjectHandle make_standard_font(QPDF& qpdf, const std::string& base_font);

    // Register `resource` (an indirect object) in the page's own resources
    // under a new name of the given kind ("/Font", "/XObject") and return
    // that name. A resource already registered is reused.
    static std::string add_resource(QPDFPageObjectHelper page,
                                    const std::string& kind,
                                    const std::string& prefix,
                                    QPDFObjectHandle resource);

    static QPDFObjectHandle own_resources(QPDFPageObjectHelper page);

    // Draw `text` (UTF-8) with its baseline starting at (x, y), in `font`
    // (from make_standard_font) at `size` points, filled with `color`
    // (red, green, blue in 0..1). A line feed starts a new line 1.2 x size
    // below. Text outside WinAnsiEncoding is an UNSUPPORTED error.
    static void draw_text(QPDF& qpdf,
                          QPDFPageObjectHelper page,
                          QPDFObjectHandle font,
                          const std::string& text,
                          double x,
                          double y,
                          double size,
                          const std::array<double, 3>& color);

    // The text as a PDF literal string in WinAnsiEncoding, parentheses
    // included.
    static std::string win_ansi_literal(const std::string& text);

    // A page of page_width x page_height points showing one image in the
    // rectangle `placement` (x, y, width, height). `data` is either a JPEG
    // file (is_jpeg) or width x height x components 8-bit samples, row by
    // row; `colorspace` is "gray", "rgb" or "cmyk". `alpha`, when not empty,
    // is width x height 8-bit opacity samples. `invert` marks samples stored
    // inverted, as Adobe applications write CMYK JPEGs. `rotate` becomes the
    // page's /Rotate. The page is not yet in the page tree.
    static QPDFObjectHandle make_image_page(QPDF& qpdf,
                                            int width,
                                            int height,
                                            const std::string& colorspace,
                                            const std::string& data,
                                            bool is_jpeg,
                                            bool invert,
                                            const std::string& alpha,
                                            double page_width,
                                            double page_height,
                                            const std::array<double, 4>& placement,
                                            int rotate);

    // A number as written in a content stream.
    static std::string number(double value);

    // Replace the page's /Rotate by an equivalent rotation of its content,
    // boxes and annotations, so the page displays the same with /Rotate 0.
    static void flatten_rotation(QPDF& qpdf, QPDFPageObjectHelper page);

  private:

    // Finite entries and a non-zero determinant, or INVALID_EDIT.
    static void check_matrix(const std::array<double, 6>& matrix);
    static QPDFObjectHandle transform_rectangle(QPDFObjectHandle rectangle, const QPDFMatrix& matrix);
    static QPDFObjectHandle transform_points(QPDFObjectHandle points, const QPDFMatrix& matrix);
    static QPDFObjectHandle transform_appearance(QPDFObjectHandle appearance, const QPDFMatrix& matrix);
    static QPDFObjectHandle transform_annotation(QPDFObjectHandle annotation, const QPDFMatrix& matrix);
  };

  inline pdf_page_info::pdf_page_info():
    media_box({0.0, 0.0, 0.0, 0.0}),
    crop_box({0.0, 0.0, 0.0, 0.0}),
    bleed_box({0.0, 0.0, 0.0, 0.0}),
    trim_box({0.0, 0.0, 0.0, 0.0}),
    art_box({0.0, 0.0, 0.0, 0.0}),
    rotation(0),
    user_unit(1.0)
  {}

  inline pdf_page_info::box_type pdf_page_info::to_box(QPDFObjectHandle handle,
                                                       const box_type& fallback)
  {
    if(not handle.isRectangle())
      {
        return fallback;
      }

    QPDFObjectHandle::Rectangle rect = handle.getArrayAsRectangle();

    // A rectangle may be given by any two diagonally opposite corners
    // (ISO 32000-1, 7.9.5): report it normalized.
    return {std::min(rect.llx, rect.urx),
            std::min(rect.lly, rect.ury),
            std::max(rect.llx, rect.urx),
            std::max(rect.lly, rect.ury)};
  }

  inline pdf_page_info pdf_page_info::read(QPDFPageObjectHelper page)
  {
    pdf_page_info result;

    // copy_if_shared=false: reading must not modify the document.
    result.media_box = to_box(page.getAttribute("/MediaBox", false), result.media_box);
    result.crop_box = to_box(page.getAttribute("/CropBox", false), result.media_box);

    QPDFObjectHandle page_object = page.getObjectHandle();

    result.bleed_box = to_box(page_object.getKey("/BleedBox"), result.crop_box);
    result.trim_box = to_box(page_object.getKey("/TrimBox"), result.crop_box);
    result.art_box = to_box(page_object.getKey("/ArtBox"), result.crop_box);

    QPDFObjectHandle rotate = page.getAttribute("/Rotate", false);
    if(rotate.isInteger())
      {
        int angle = rotate.getIntValueAsInt() % 360;
        result.rotation = (angle < 0) ? angle + 360 : angle;
      }

    QPDFObjectHandle user_unit = page_object.getKey("/UserUnit");
    if(user_unit.isNumber())
      {
        result.user_unit = user_unit.getNumericValue();
      }

    return result;
  }

  inline const pdf_page_info::box_type& pdf_page_info::get_media_box() const { return media_box; }

  inline const pdf_page_info::box_type& pdf_page_info::get_crop_box() const { return crop_box; }

  inline const pdf_page_info::box_type& pdf_page_info::get_bleed_box() const { return bleed_box; }

  inline const pdf_page_info::box_type& pdf_page_info::get_trim_box() const { return trim_box; }

  inline const pdf_page_info::box_type& pdf_page_info::get_art_box() const { return art_box; }

  inline int pdf_page_info::get_rotation() const { return rotation; }

  inline double pdf_page_info::get_user_unit() const { return user_unit; }

  inline std::string pdf_page_ops::box_key(const std::string& name)
  {
    if(name == "media") { return "/MediaBox"; }
    if(name == "crop")  { return "/CropBox"; }
    if(name == "bleed") { return "/BleedBox"; }
    if(name == "trim")  { return "/TrimBox"; }
    if(name == "art")   { return "/ArtBox"; }

    throw pdf_edit_error(edit_error::INVALID_EDIT,
                         "unknown page box '" + name + "': expected media, crop, bleed, trim or art");
  }

  inline void pdf_page_ops::set_box(QPDFPageObjectHelper page,
                                    const std::string& name,
                                    const pdf_page_info::box_type& box)
  {
    std::string key = box_key(name);

    if(not (std::isfinite(box[0]) and std::isfinite(box[1]) and
            std::isfinite(box[2]) and std::isfinite(box[3]) and
            box[0] < box[2] and box[1] < box[3]))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "page box must be finite and have a positive width and height");
      }

    // Written on the page itself, never on a shared /Pages node: an inherited
    // box stays untouched for the sibling pages.
    QPDFObjectHandle::Rectangle rect(box[0], box[1], box[2], box[3]);
    page.getObjectHandle().replaceKey(key, QPDFObjectHandle::newArray(rect));
  }

  inline void pdf_page_ops::rotate(QPDFPageObjectHelper page, int angle, bool relative)
  {
    // /Rotate shall be a multiple of 90 (ISO 32000-1, table 30).
    if((angle % 90) != 0)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "page rotation must be a multiple of 90 degrees");
      }

    page.rotatePage(angle, relative);
  }

  inline QPDFObjectHandle pdf_page_ops::make_blank_page(QPDF& qpdf, double width, double height)
  {
    if(not (std::isfinite(width) and std::isfinite(height) and width > 0.0 and height > 0.0))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "a blank page needs a finite positive width and height");
      }

    // /Contents is optional (ISO 32000-1, table 30), but an empty content
    // stream is the form other producers write for a blank page, so it is the
    // form readers are certain to have seen.
    QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
    page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    page.replaceKey("/MediaBox",
                    QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(0.0, 0.0, width, height)));
    page.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
    page.replaceKey("/Contents", qpdf.newStream(std::string("")));

    return qpdf.makeIndirectObject(page);
  }

  inline void pdf_page_ops::check_matrix(const std::array<double, 6>& matrix)
  {
    for(double value : matrix)
      {
        if(not std::isfinite(value))
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT,
                                 "a transformation matrix must have finite entries");
          }
      }

    // A singular matrix collapses the page onto a line or a point.
    double determinant = matrix[0]*matrix[3] - matrix[1]*matrix[2];
    if(not std::isfinite(determinant) or std::abs(determinant) < 1.e-12)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "a transformation matrix must be invertible");
      }
  }

  inline QPDFObjectHandle
  pdf_page_ops::transform_rectangle(QPDFObjectHandle rectangle, const QPDFMatrix& matrix)
  {
    auto rect = matrix.transformRectangle(rectangle.getArrayAsRectangle());
    if(not (std::isfinite(rect.llx) and std::isfinite(rect.lly) and
            std::isfinite(rect.urx) and std::isfinite(rect.ury)))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "a transformed rectangle must be finite");
      }
    return QPDFObjectHandle::newFromRectangle(rect);
  }

  inline QPDFObjectHandle
  pdf_page_ops::transform_points(QPDFObjectHandle points, const QPDFMatrix& matrix)
  {
    if(not points.isArray() or points.getArrayNItems() % 2 != 0)
      {
        return points;
      }
    auto result = QPDFObjectHandle::newArray();
    for(int index = 0; index < points.getArrayNItems(); index += 2)
      {
        auto x = points.getArrayItem(index);
        auto y = points.getArrayItem(index + 1);
        if(not (x.isNumber() and y.isNumber()))
          {
            return points;
          }
        double tx, ty;
        matrix.transform(x.getNumericValue(), y.getNumericValue(), tx, ty);
        result.appendItem(QPDFObjectHandle::newReal(number(tx)));
        result.appendItem(QPDFObjectHandle::newReal(number(ty)));
      }
    return result;
  }

  inline QPDFObjectHandle
  pdf_page_ops::transform_appearance(QPDFObjectHandle appearance, const QPDFMatrix& matrix)
  {
    // An appearance stream may be shared by other widgets. Copy the stream
    // and its dictionaries while preserving the identities of the widgets.
    auto transform_stream = [&](QPDFObjectHandle stream) {
      auto transformed = stream.copyStream();
      auto dict = transformed.getDict();
      auto original = dict.getKey("/Matrix");
      auto cm = original.isArray() ? QPDFMatrix(original.getArrayAsMatrix()) : QPDFMatrix();
      cm.concat(matrix);
      for(double value : {cm.a, cm.b, cm.c, cm.d, cm.e, cm.f})
        {
          if(not std::isfinite(value))
            {
              throw pdf_edit_error(edit_error::INVALID_EDIT, "a transformed appearance must be finite");
            }
        }
      dict.replaceKey("/Matrix", QPDFObjectHandle::newFromMatrix(cm));
      return transformed;
    };

    auto result = QPDFObjectHandle::newDictionary();
    for(const auto& [key, value] : appearance.getDictAsMap())
      {
        auto entry = value;
        if(entry.isStream())
          {
            entry = transform_stream(entry);
          }
        else if(entry.isDictionary())
          {
            auto states = QPDFObjectHandle::newDictionary();
            for(const auto& [state, stream] : entry.getDictAsMap())
              {
                auto appearance_stream = stream;
                states.replaceKey(state, appearance_stream.isStream()
                                          ? transform_stream(appearance_stream) : appearance_stream);
              }
            entry = states;
          }
        result.replaceKey(key, entry);
      }
    return result;
  }

  inline QPDFObjectHandle
  pdf_page_ops::transform_annotation(QPDFObjectHandle annotation, const QPDFMatrix& matrix)
  {
    auto changes = QPDFObjectHandle::newDictionary();
    auto rect = annotation.getKey("/Rect");
    if(rect.isRectangle())
      {
        changes.replaceKey("/Rect", transform_rectangle(rect, matrix));
      }
    for(const char* key : {"/QuadPoints", "/Vertices", "/L", "/CL"})
      {
        auto points = annotation.getKey(key);
        if(points.isArray())
          {
            changes.replaceKey(key, transform_points(points, matrix));
          }
      }
    for(const char* key : {"/InkList", "/Path"})
      {
        auto paths = annotation.getKey(key);
        if(paths.isArray())
          {
            auto transformed = QPDFObjectHandle::newArray();
            for(auto points : paths.getArrayAsVector())
              {
                transformed.appendItem(transform_points(points, matrix));
              }
            changes.replaceKey(key, transformed);
          }
      }
    auto appearance = annotation.getKey("/AP");
    if(appearance.isDictionary())
      {
        changes.replaceKey("/AP", transform_appearance(appearance, matrix));
      }
    return changes;
  }

  inline void pdf_page_ops::flatten_rotation(QPDF& qpdf, QPDFPageObjectHelper page)
  {
    auto rotate = page.getAttribute("/Rotate", false);
    int angle = rotate.isInteger() ? static_cast<int>((rotate.getIntValue() % 360 + 360) % 360) : 0;
    auto mediabox = page.getAttribute("/MediaBox", false);
    if(angle == 0 or angle % 90 != 0 or not mediabox.isRectangle())
      {
        return;
      }

    auto rect = mediabox.getArrayAsRectangle();
    const double x = rect.llx + rect.urx;
    const double y = rect.lly + rect.ury;
    // Use the same coordinate changes that qpdf applies to page content.
    if(angle == 90)
      {
        transform(qpdf, page, {0, -1, 1, 0, 0, x}, true);
      }
    else if(angle == 180)
      {
        transform(qpdf, page, {-1, 0, 0, -1, x, y}, true);
      }
    else
      {
        transform(qpdf, page, {0, 1, -1, 0, y, 0}, true);
      }

    auto page_object = page.getObjectHandle();
    page_object.removeKey("/Rotate");
    if(not page.getAttribute("/Rotate", false).isNull())
      {
        page_object.replaceKey("/Rotate", QPDFObjectHandle::newInteger(0));
      }
  }

  inline void pdf_page_ops::transform(QPDF& qpdf,
                                      QPDFPageObjectHelper page,
                                      const std::array<double, 6>& matrix,
                                      bool transform_boxes)
  {
    check_matrix(matrix);
    QPDFMatrix cm(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]);
    auto page_object = page.getObjectHandle();

    // Prepare the replacements before changing any live page objects.
    auto boxes = QPDFObjectHandle::newDictionary();
    if(transform_boxes)
      {
        for(const std::string name : {"/MediaBox", "/CropBox", "/BleedBox", "/TrimBox", "/ArtBox"})
          {
            bool inheritable = (name == "/MediaBox" or name == "/CropBox");
            auto box = inheritable ? page.getAttribute(name, false) : page_object.getKey(name);
            if(box.isRectangle())
              {
                boxes.replaceKey(name, transform_rectangle(box, cm));
              }
          }
      }

    std::vector<std::pair<QPDFObjectHandle, QPDFObjectHandle> > annotations;
    auto annots = page_object.getKey("/Annots");
    if(annots.isArray())
      {
        for(auto annotation : annots.getArrayAsVector())
          {
            if(annotation.isDictionary())
              {
                annotations.emplace_back(annotation, transform_annotation(annotation, cm));
              }
          }
      }

    auto prefix = qpdf.newStream(std::string("q\n") + cm.unparse() + " cm\n");
    auto suffix = qpdf.newStream(std::string("\nQ\n"));
    page.addPageContents(prefix, true);
    page.addPageContents(suffix, false);
    for(const auto& [key, value] : boxes.getDictAsMap())
      {
        page_object.replaceKey(key, value);
      }
    for(auto& [annotation, changes] : annotations)
      {
        for(const auto& [key, value] : changes.getDictAsMap())
          {
            annotation.replaceKey(key, value);
          }
      }
  }

  inline void pdf_page_ops::overlay(QPDF& qpdf,
                                    QPDFPageObjectHelper page,
                                    QPDFObjectHandle form,
                                    bool under,
                                    bool fit,
                                    const std::array<double, 6>& matrix,
                                    QPDFObjectHandle expand_to)
  {
    check_matrix(matrix);

    const std::array<double, 6> identity = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    if(fit and matrix != identity)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "an overlay is either fitted to the page or placed with a matrix, not both");
      }

    QPDFMatrix cm(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]);
    QPDFObjectHandle expanded_box = QPDFObjectHandle::newNull();
    if(not expand_to.isNull())
      {
        auto bounds = transform_rectangle(expand_to, cm).getArrayAsRectangle();
        auto original = page.getMediaBox().getArrayAsRectangle();
        expanded_box = QPDFObjectHandle::newFromRectangle({
          std::min(original.llx, bounds.llx), std::min(original.lly, bounds.lly),
          std::max(original.urx, bounds.urx), std::max(original.ury, bounds.ury)});
      }

    // The page gets a /Resources dictionary of its own: adding a name to one
    // inherited from /Pages would add it to every sibling page.
    QPDFObjectHandle resources = own_resources(page);

    int min_suffix = 1;
    std::string name = resources.getUniqueResourceName("/Fx", min_suffix);

    std::string content;
    if(fit)
      {
        QPDFObjectHandle trim_box = page.getTrimBox(false, false);
        if(not trim_box.isRectangle())
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT,
                                 "the page has no usable boundary to fit an overlay into");
          }

        content = page.placeFormXObject(form,
                                        name,
                                        trim_box.getArrayAsRectangle(),
                                        true,  // invert_transformations
                                        true,  // allow_shrink
                                        true); // allow_expand
        if(content.empty())
          {
            // qpdf's answer to a form without a usable /BBox
            throw pdf_edit_error(edit_error::INVALID_EDIT,
                                 "the overlaid page has no usable boundary");
          }
      }
    else
      {
        content = "q\n" + cm.unparse() + " cm\n" + name + " Do\nQ\n";
      }

    // mergeResources replaces a shared /XObject subdictionary by a copy
    // before the name is added to it.
    resources.mergeResources(QPDFObjectHandle::parse("<< /XObject << >> >>"));
    QPDFObjectHandle xobjects = resources.getKey("/XObject");
    if(not xobjects.isDictionary())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "the page's /XObject resources are not a dictionary");
      }
    xobjects.replaceKey(name, form);

    if(under)
      {
        // Drawn first, in a graphics state of its own.
        page.addPageContents(qpdf.newStream(content), true);
      }
    else
      {
        // The existing content may leave any graphics state behind: close it
        // off before drawing on top.
        append_content(qpdf, page, content);
      }
    if(not expanded_box.isNull())
      {
        page.getObjectHandle().replaceKey("/MediaBox", expanded_box);
      }
  }

  inline std::string pdf_page_ops::number(double value)
  {
    if(not std::isfinite(value))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "a coordinate must be a finite number");
      }

    return QUtil::double_to_string(value, 4, true);
  }

  inline void pdf_page_ops::append_content(QPDF& qpdf,
                                           QPDFPageObjectHelper page,
                                           const std::string& content)
  {
    // q ... Q around what is there, then the new operators: same isolation
    // as an overlay.
    page.addPageContents(qpdf.newStream(std::string("q\n")), true);
    page.addPageContents(qpdf.newStream(std::string("\nQ\n") + content), false);
  }

  inline QPDFObjectHandle pdf_page_ops::make_standard_font(QPDF& qpdf, const std::string& base_font)
  {
    static const std::set<std::string> fonts = {
      "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique",
      "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic",
      "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique"};

    if(fonts.count(base_font) == 0)
      {
        std::string choices;
        for(const std::string& choice : fonts)
          {
            choices += (choices.empty() ? "" : ", ") + choice;
          }
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "'" + base_font + "' is not an available font: expected one of " + choices);
      }

    QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
    font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
    font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/" + base_font));
    font.replaceKey("/Encoding", QPDFObjectHandle::newName("/WinAnsiEncoding"));

    return qpdf.makeIndirectObject(font);
  }

  inline QPDFObjectHandle pdf_page_ops::own_resources(QPDFPageObjectHelper page)
  {
    QPDFObjectHandle resources = page.getAttribute("/Resources", true);
    if(not resources.isDictionary())
      {
        resources = QPDFObjectHandle::newDictionary();
        page.getObjectHandle().replaceKey("/Resources", resources);
      }
    return resources;
  }

  inline std::string pdf_page_ops::add_resource(QPDFPageObjectHelper page,
                                                const std::string& kind,
                                                const std::string& prefix,
                                                QPDFObjectHandle resource)
  {
    QPDFObjectHandle resources = own_resources(page);

    resources.mergeResources(QPDFObjectHandle::parse("<< " + kind + " << >> >>"));
    QPDFObjectHandle entries = resources.getKey(kind);
    if(not entries.isDictionary())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "the page's " + kind + " resources are not a dictionary");
      }

    for(const std::string& name : entries.getKeys())
      {
        QPDFObjectHandle existing = entries.getKey(name);
        if(existing.isIndirect() and existing.getObjGen() == resource.getObjGen())
          {
            return name;
          }
      }

    int min_suffix = 1;
    std::string name = resources.getUniqueResourceName(prefix, min_suffix);
    entries.replaceKey(name, resource);

    return name;
  }

  inline std::string pdf_page_ops::win_ansi_literal(const std::string& text)
  {
    std::string encoded;
    if(not QUtil::utf8_to_win_ansi(text, encoded))
      {
        throw pdf_edit_error(load_error::UNSUPPORTED,
                             "the text has characters outside WinAnsiEncoding (Latin-1 and a few more), "
                             "which is all the built-in fonts can show");
      }

    std::string literal = "(";
    for(char character : encoded)
      {
        // the three characters with a meaning inside a literal string
        if(character == '(' or character == ')' or character == '\\')
          {
            literal += '\\';
          }
        literal += character;
      }
    literal += ")";

    return literal;
  }

  inline void pdf_page_ops::draw_text(QPDF& qpdf,
                                      QPDFPageObjectHelper page,
                                      QPDFObjectHandle font,
                                      const std::string& text,
                                      double x,
                                      double y,
                                      double size,
                                      const std::array<double, 3>& color)
  {
    if(not (size > 0.0 and std::isfinite(size)))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "the font size must be positive");
      }
    for(double component : color)
      {
        if(not (component >= 0.0 and component <= 1.0))
          {
            throw pdf_edit_error(edit_error::INVALID_EDIT,
                                 "a color is three numbers between 0 and 1");
          }
      }

    // Encode every line before touching the page: a line that cannot be
    // shown must leave the page as it was.
    std::vector<std::string> lines;
    std::string::size_type start = 0;
    while(true)
      {
        std::string::size_type end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if(not line.empty() and line.back() == '\r')
          {
            line.pop_back();
          }
        lines.push_back(win_ansi_literal(line));

        if(end == std::string::npos)
          {
            break;
          }
        start = end + 1;
      }

    std::string font_name = add_resource(page, "/Font", "/F", font);

    std::string content = "q\nBT\n";
    content += font_name + " " + number(size) + " Tf\n";
    content += number(color[0]) + " " + number(color[1]) + " " + number(color[2]) + " rg\n";
    content += number(1.2 * size) + " TL\n";
    content += number(x) + " " + number(y) + " Td\n";
    for(std::size_t index = 0; index < lines.size(); index++)
      {
        if(index > 0)
          {
            content += "T*\n";
          }
        content += lines[index] + " Tj\n";
      }
    content += "ET\nQ\n";

    append_content(qpdf, page, content);
  }

  inline QPDFObjectHandle pdf_page_ops::make_image_page(QPDF& qpdf,
                                                        int width,
                                                        int height,
                                                        const std::string& colorspace,
                                                        const std::string& data,
                                                        bool is_jpeg,
                                                        bool invert,
                                                        const std::string& alpha,
                                                        double page_width,
                                                        double page_height,
                                                        const std::array<double, 4>& placement,
                                                        int rotate)
  {
    if((rotate % 90) != 0)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "page rotation must be a multiple of 90 degrees");
      }

    if(width <= 0 or height <= 0)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "an image needs a positive width and height");
      }
    if(not (std::isfinite(page_width) and std::isfinite(page_height) and
            page_width > 0.0 and page_height > 0.0))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "a page needs a finite positive width and height");
      }
    if(not (std::isfinite(placement[0]) and std::isfinite(placement[1]) and
            std::isfinite(placement[2]) and std::isfinite(placement[3]) and
            placement[2] > 0.0 and placement[3] > 0.0))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "an image must have finite placement and a positive width and height");
      }

    std::size_t components = 0;
    std::string device;
    if(colorspace == "gray")      { components = 1; device = "/DeviceGray"; }
    else if(colorspace == "rgb")  { components = 3; device = "/DeviceRGB"; }
    else if(colorspace == "cmyk") { components = 4; device = "/DeviceCMYK"; }
    else
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "an image colorspace is 'gray', 'rgb' or 'cmyk'");
      }

    std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    if(data.empty())
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT, "the image has no data");
      }
    if(not is_jpeg and data.size() != pixels * components)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "the image data does not match its size: expected "
                             + std::to_string(pixels * components) + " bytes, got "
                             + std::to_string(data.size()));
      }
    if(not alpha.empty() and alpha.size() != pixels)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "the image's opacity samples do not match its size");
      }

    QPDFObjectHandle image = qpdf.newStream();
    if(is_jpeg)
      {
        // A JPEG file is the /DCTDecode stream data as it is: stored, not
        // re-encoded.
        image.replaceStreamData(data,
                                QPDFObjectHandle::newName("/DCTDecode"),
                                QPDFObjectHandle::newNull());
      }
    else
      {
        // Raw samples; the writer compresses them.
        image.replaceStreamData(data, QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
      }

    QPDFObjectHandle image_dict = image.getDict();
    image_dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    image_dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
    image_dict.replaceKey("/Width", QPDFObjectHandle::newInteger(width));
    image_dict.replaceKey("/Height", QPDFObjectHandle::newInteger(height));
    image_dict.replaceKey("/ColorSpace", QPDFObjectHandle::newName(device));
    image_dict.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));

    if(invert)
      {
        // one [1 0] pair per component maps each sample to its inverse
        // (ISO 32000-1, 8.9.5.2)
        QPDFObjectHandle decode = QPDFObjectHandle::newArray();
        for(std::size_t component = 0; component < components; component++)
          {
            decode.appendItem(QPDFObjectHandle::newInteger(1));
            decode.appendItem(QPDFObjectHandle::newInteger(0));
          }
        image_dict.replaceKey("/Decode", decode);
      }

    if(not alpha.empty())
      {
        // Opacity is a second, grayscale image (ISO 32000-1, 11.6.5.3).
        QPDFObjectHandle mask = qpdf.newStream();
        mask.replaceStreamData(alpha, QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());

        QPDFObjectHandle mask_dict = mask.getDict();
        mask_dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
        mask_dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
        mask_dict.replaceKey("/Width", QPDFObjectHandle::newInteger(width));
        mask_dict.replaceKey("/Height", QPDFObjectHandle::newInteger(height));
        mask_dict.replaceKey("/ColorSpace", QPDFObjectHandle::newName("/DeviceGray"));
        mask_dict.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));

        image_dict.replaceKey("/SMask", mask);
      }

    // An image fills the unit square; the matrix maps that square onto the
    // placement rectangle (ISO 32000-1, 8.9.4).
    std::string content = "q\n";
    content += number(placement[2]) + " 0 0 " + number(placement[3]) + " "
      + number(placement[0]) + " " + number(placement[1]) + " cm\n";
    content += "/Im0 Do\nQ\n";

    QPDFObjectHandle xobjects = QPDFObjectHandle::newDictionary();
    xobjects.replaceKey("/Im0", image);

    QPDFObjectHandle resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/XObject", xobjects);

    QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
    page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    page.replaceKey("/MediaBox",
                    QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(0.0, 0.0, page_width, page_height)));
    page.replaceKey("/Resources", resources);
    page.replaceKey("/Contents", qpdf.newStream(content));

    int normalized = ((rotate % 360) + 360) % 360;
    if(normalized != 0)
      {
        page.replaceKey("/Rotate", QPDFObjectHandle::newInteger(normalized));
      }

    return qpdf.makeIndirectObject(page);
  }

}

#endif
