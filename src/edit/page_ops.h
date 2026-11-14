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
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFMatrix.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

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
    // are not part of the content stream. The page boxes are left alone.
    static void transform(QPDF& qpdf,
                          QPDFPageObjectHelper page,
                          const std::array<double, 6>& matrix);

    // Multiply every page boundary by (sx, sy), so the page changes size
    // together with content transformed by the same scale.
    static void scale_boxes(QPDFPageObjectHelper page, double sx, double sy);

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
                        const std::array<double, 6>& matrix);

    // Replace the page's /Rotate by an equivalent rotation of its content,
    // boxes and annotations, so the page displays the same with /Rotate 0.
    static void flatten_rotation(QPDF& qpdf, QPDFPageObjectHelper page);

  private:

    // Finite entries and a non-zero determinant, or INVALID_EDIT.
    static void check_matrix(const std::array<double, 6>& matrix);
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

    if(not (box[0] < box[2] and box[1] < box[3]))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "page box must have a positive width and height");
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
    if(not (width > 0.0 and height > 0.0))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "a blank page needs a positive width and height");
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
    if(std::abs(determinant) < 1.e-12)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "a transformation matrix must be invertible");
      }
  }

  inline void pdf_page_ops::flatten_rotation(QPDF& qpdf, QPDFPageObjectHelper page)
  {
    // qpdf rewrites the content, the boxes and, given the helper, the
    // annotations and their form fields.
    QPDFAcroFormDocumentHelper acroform(qpdf);
    page.flattenRotation(&acroform);
  }

  inline void pdf_page_ops::transform(QPDF& qpdf,
                                      QPDFPageObjectHelper page,
                                      const std::array<double, 6>& matrix)
  {
    check_matrix(matrix);

    QPDFMatrix cm(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]);

    // q ... Q around the existing content: the matrix applies to all of it
    // and to nothing drawn after it. Same construction as qpdf's
    // QPDFPageObjectHelper::flattenRotation.
    page.addPageContents(qpdf.newStream(std::string("q\n") + cm.unparse() + " cm\n"), true);
    page.addPageContents(qpdf.newStream(std::string("\nQ\n")), false);

    QPDFObjectHandle page_object = page.getObjectHandle();
    QPDFObjectHandle annots = page_object.getKey("/Annots");
    if(annots.isArray() and annots.getArrayNItems() > 0)
      {
        std::vector<QPDFObjectHandle> new_annots;
        std::vector<QPDFObjectHandle> new_fields;
        std::set<QPDFObjGen> old_fields;

        // transformAnnotations returns transformed copies; the page then
        // refers to the copies and the form to their fields, in place of
        // the originals.
        QPDFAcroFormDocumentHelper acroform(qpdf);
        acroform.transformAnnotations(annots, new_annots, new_fields, old_fields, cm);
        acroform.removeFormFields(old_fields);
        for(const QPDFObjectHandle& field : new_fields)
          {
            acroform.addFormField(QPDFFormFieldObjectHelper(field));
          }

        page_object.replaceKey("/Annots", QPDFObjectHandle::newArray(new_annots));
      }
  }

  inline void pdf_page_ops::scale_boxes(QPDFPageObjectHelper page, double sx, double sy)
  {
    if(not (sx > 0.0 and sy > 0.0 and std::isfinite(sx) and std::isfinite(sy)))
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "page scale factors must be positive");
      }

    QPDFObjectHandle page_object = page.getObjectHandle();

    for(const char* key : {"/MediaBox", "/CropBox", "/BleedBox", "/TrimBox", "/ArtBox"})
      {
        // /MediaBox and /CropBox may be inherited: the scaled value is
        // written on this page, so sibling pages keep theirs.
        std::string name(key);
        bool inheritable = (name == "/MediaBox" or name == "/CropBox");

        QPDFObjectHandle box = inheritable ? page.getAttribute(name, false) : page_object.getKey(name);
        if(not box.isRectangle())
          {
            continue;
          }

        QPDFObjectHandle::Rectangle rect = box.getArrayAsRectangle();
        QPDFObjectHandle::Rectangle scaled(rect.llx*sx, rect.lly*sy, rect.urx*sx, rect.ury*sy);

        page_object.replaceKey(name, QPDFObjectHandle::newArray(scaled));
      }
  }

  inline void pdf_page_ops::overlay(QPDF& qpdf,
                                    QPDFPageObjectHelper page,
                                    QPDFObjectHandle form,
                                    bool under,
                                    bool fit,
                                    const std::array<double, 6>& matrix)
  {
    check_matrix(matrix);

    const std::array<double, 6> identity = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    if(fit and matrix != identity)
      {
        throw pdf_edit_error(edit_error::INVALID_EDIT,
                             "an overlay is either fitted to the page or placed with a matrix, not both");
      }

    // The page gets a /Resources dictionary of its own: adding a name to one
    // inherited from /Pages would add it to every sibling page.
    QPDFObjectHandle resources = page.getAttribute("/Resources", true);
    if(not resources.isDictionary())
      {
        resources = QPDFObjectHandle::newDictionary();
        page.getObjectHandle().replaceKey("/Resources", resources);
      }

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
        QPDFMatrix cm(matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]);
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
        page.addPageContents(qpdf.newStream(std::string("q\n")), true);
        page.addPageContents(qpdf.newStream(std::string("\nQ\n") + content), false);
      }
  }

}

#endif
