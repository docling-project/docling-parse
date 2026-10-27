//-*-C++-*-

#ifndef QPDF_PAGE_RESOURCES_H
#define QPDF_PAGE_RESOURCES_H

#include <exception>
#include <set>
#include <string>
#include <vector>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjGen.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

namespace pdflib
{
  namespace qpdf_page_resources
  {
    // Every name operand of a content stream. It collects more than the
    // `Tf` and `Do` operands, which only keeps more entries.
    class name_collector : public QPDFObjectHandle::ParserCallbacks
    {
    public:

      std::set<std::string>& names;

      explicit name_collector(std::set<std::string>& names_):
        names(names_)
      {}

      void handleObject(QPDFObjectHandle obj) override
      {
        if(obj.isName())
          {
            names.insert(obj.getName());
          }
      }

      void handleEOF() override {}
    };

    // Walks every content stream the stream decoder may run for one page
    // and every resource dictionary those streams can resolve a name in.
    //
    // The decoder resolves a name against the innermost resources first and
    // then against the enclosing ones up to the page: in form XObjects, in
    // Type3 glyph procedures (9.6.5), in tiling pattern cells, in soft-mask
    // groups, and in appearance streams, which fall back to the page fonts
    // and XObjects. A /Font or /XObject entry whose name occurs in none of
    // these streams can therefore never be resolved.
    //
    // Each stream and dictionary is visited once. The forms of one figure
    // often share a single /XObject dictionary that names all of them, and
    // qpdf's forEachFormXObject() calls its action once per entry of every
    // visit: on a page with 2301 such forms that is millions of calls.
    class resource_walk
    {
    public:

      std::set<std::string> names;

      // The /Font and /XObject dictionaries seen, each once.
      std::vector<QPDFObjectHandle> groups;

      // False when a stream could not be parsed, so that its names are
      // unknown, or when the streams nest deeper than max_depth.
      bool complete = true;

      void visit_page(QPDFPageObjectHelper& page)
      {
        parse(page.getObjectHandle(), [&page](name_collector& collector)
              {
                page.parseContents(&collector);
              });

        visit_resources(page.getObjectHandle().getKey("/Resources"));
        visit_annotations(page.getObjectHandle());
      }

    private:

      // Every level of nesting -- a form in a form, a Type3 font used in a
      // glyph procedure, a pattern in a pattern -- passes through
      // visit_resources(). The walk recurses, and it also reaches forms that
      // are never drawn, so a crafted chain of them must not exhaust the
      // stack.
      static constexpr int max_depth = 32;
      int depth = 0;

      std::set<QPDFObjGen> seen;

      // Direct objects have no identity, so they are always visited; only
      // indirect ones can be shared or form a cycle.
      bool first_visit(QPDFObjectHandle obj)
      {
        return not obj.isIndirect() or seen.insert(obj.getObjGen()).second;
      }

      template<typename parser_type>
      void parse(QPDFObjectHandle obj, parser_type parser)
      {
        // qpdf reports a bad token as a warning and goes on, so a token it
        // skipped could have been a name.
        QPDF* owner = obj.getOwningQPDF();
        size_t warnings = owner ? owner->numWarnings() : 0;

        try
          {
            name_collector collector(names);
            parser(collector);
          }
        catch(const std::exception&)
          {
            complete = false;
          }

        if(owner and owner->numWarnings() > warnings)
          {
            complete = false;
          }
      }

      void scan(QPDFObjectHandle stream)
      {
        if(not stream.isStream() or not first_visit(stream))
          {
            return;
          }

        parse(stream, [&stream](name_collector& collector)
              {
                stream.parseAsContents(&collector);
              });

        visit_resources(stream.getDict().getKey("/Resources"));
      }

      void visit_annotations(QPDFObjectHandle page)
      {
        QPDFObjectHandle annots = page.getKey("/Annots");
        if(not annots.isArray())
          {
            return;
          }

        for(auto annot : annots.getArrayAsVector())
          {
            QPDFObjectHandle ap = annot.isDictionary() ? annot.getKey("/AP") : QPDFObjectHandle::newNull();
            if(not ap.isDictionary())
              {
                continue;
              }

            for(const char* key : {"/N", "/R", "/D"})
              {
                QPDFObjectHandle appearance = ap.getKey(key);
                if(appearance.isStream())
                  {
                    scan(appearance);
                  }
                else if(appearance.isDictionary())
                  {
                    // one stream per appearance state, e.g. /On and /Off
                    for(auto const& state : appearance.getDictAsMap())
                      {
                        scan(state.second);
                      }
                  }
              }
          }
      }

      void visit_resources(QPDFObjectHandle resources)
      {
        if(not complete or not resources.isDictionary() or not first_visit(resources))
          {
            return;
          }

        if(depth >= max_depth)
          {
            complete = false;
            return;
          }

        depth += 1;
        visit_groups(resources);
        depth -= 1;
      }

      void visit_groups(QPDFObjectHandle resources)
      {
        QPDFObjectHandle fonts = resources.getKey("/Font");
        if(fonts.isDictionary() and first_visit(fonts))
          {
            groups.push_back(fonts);
            for(auto const& font : fonts.getDictAsMap())
              {
                visit_type3_font(font.second);
              }
          }

        QPDFObjectHandle xobjects = resources.getKey("/XObject");
        if(xobjects.isDictionary() and first_visit(xobjects))
          {
            groups.push_back(xobjects);
            for(auto const& xobject : xobjects.getDictAsMap())
              {
                if(xobject.second.isFormXObject())
                  {
                    scan(xobject.second);
                  }
              }
          }

        QPDFObjectHandle patterns = resources.getKey("/Pattern");
        if(patterns.isDictionary() and first_visit(patterns))
          {
            for(auto const& pattern : patterns.getDictAsMap())
              {
                // a tiling pattern is a stream, a shading pattern is not
                scan(pattern.second);
              }
          }

        QPDFObjectHandle states = resources.getKey("/ExtGState");
        if(states.isDictionary() and first_visit(states))
          {
            for(auto const& state : states.getDictAsMap())
              {
                QPDFObjectHandle mask = state.second.isDictionary() ? state.second.getKey("/SMask") : QPDFObjectHandle::newNull();
                if(mask.isDictionary())
                  {
                    scan(mask.getKey("/G"));
                  }
              }
          }
      }

      void visit_type3_font(QPDFObjectHandle font)
      {
        if(not font.isDictionary()
           or not font.getKey("/Subtype").isNameAndEquals("/Type3")
           or not first_visit(font))
          {
            return;
          }

        QPDFObjectHandle procs = font.getKey("/CharProcs");
        if(procs.isDictionary())
          {
            for(auto const& proc : procs.getDictAsMap())
              {
                scan(proc.second);
              }
          }

        visit_resources(font.getKey("/Resources"));
      }
    };

    // Removes the fonts and XObjects that a page copied out of its document
    // can never use (docling-parse#424). A producer such as LibreOffice
    // points every page at one /Resources dictionary that names every image
    // of the document, so without this each one-page buffer carries the
    // compressed streams of all of them.
    //
    // The dictionaries are changed in place, so the page must belong to a
    // document of its own. The streams they named stay in the document but
    // are no longer reachable, and QPDFWriter does not write them. The page
    // is left as it is when the walk is not complete or fails.
    inline bool remove_unreferenced_resources(QPDFPageObjectHelper& page)
    {
      resource_walk walk;
      try
        {
          walk.visit_page(page);
        }
      catch(const std::exception&)
        {
          // a damaged object qpdf could not resolve
          return false;
        }

      if(not walk.complete)
        {
          return false;
        }

      bool removed = false;
      for(auto& group : walk.groups)
        {
          for(auto const& key : group.getKeys())
            {
              if(walk.names.count(key) == 0)
                {
                  group.removeKey(key);
                  removed = true;
                }
            }
        }

      return removed;
    }
  }
}

#endif
