//-*-C++-*-

#ifndef PDF_STREAM_ENUMS_H
#define PDF_STREAM_ENUMS_H

namespace pdflib
{
  namespace pdf_operator
  {
    enum operator_class {

      rest,

      GENERAL_GRAPHICS_STATE,

      COLOR_SCHEME,

      PATH_CONSTRUCTION,
      PATH_PAINTING
    };

    // Table 51 – Operator Categories [ p 119 ]
    enum operator_name {

      // General graphics state
      w, J, j, M, d, ri, i, gs,

      // Special graphics state
      q, Q, cm,

      // XObjects
      Do,

      // color-scheme
      CS, cs, SC, SCN, sc, scn, G, g, RG, rg, K, k,

      // text objects
      BT, ET,

      // Text state
      Tc, Tw, Tz, TL, Tf, Tr, Ts,

      // Text positioning
      Td, TD, Tm, TStar,

      // Text showing
      Tj, TJ, accent, double_accent,

      // Path construction
      m, l, c, v, y, h, re,

      // Path painting
      s, S, f, F, fStar, B, BStar, b, bStar, n,

      // Clipping paths
      W, WStar,

      // Marked content
      MP, DP, BMC, BDC, EMC,

      // dummy
      null
    };

    operator_class to_class(operator_name name)
    {
      switch(name)
        {
          // General graphics state [Table 57 – Graphics State Operators]
        case w:
        case J:
        case j:
        case M:
        case d:
        case ri:
        case i:
        case gs:
          {
            return GENERAL_GRAPHICS_STATE;
          }

          // color-scheme
        case CS:
        case cs:
        case SC:
        case SCN:
        case sc:
        case scn:
        case G:
        case g:
        case RG:
        case rg:
        case K:
        case k:
          {
            return COLOR_SCHEME;
          }

          // lines
        case m:
        case l:
        case c:
        case v:
        case y:
        case h:
        case re:
          {
            return PATH_CONSTRUCTION;
          }

        case s:
        case S:
        case f:
        case F:
        case fStar:
        case B:
        case BStar:
        case b:
        case bStar:
        case n:
          {
            return PATH_PAINTING;
          }

        default:
          return rest;
        }
    }


    operator_name to_name(std::string name)
    {
      // General graphics state
      if(name=="w") { return w; }
      else if(name=="J") { return J; }
      else if(name=="j") { return j; }
      else if(name=="M") { return M; }
      else if(name=="d") { return d; }
      else if(name=="ri") { return ri; }
      else if(name=="i") { return i; }
      else if(name=="gs") { return gs; }

      // Special graphics state
      else if(name=="q") { return q; }
      else if(name=="Q") { return Q; }
      else if(name=="cm") { return cm; }

      // color-scheme
      else if(name=="CS") { return CS; }
      else if(name=="cs") { return cs; }

      else if(name=="SC") { return SC; }
      else if(name=="SCN") { return SCN; }
      else if(name=="sc") { return sc; }
      else if(name=="scn") { return scn; }

      else if(name=="G") { return G; }
      else if(name=="g") { return g; }

      else if(name=="RG") { return RG; }
      else if(name=="rg") { return rg; }

      else if(name=="K") { return K; }
      else if(name=="k") { return k; }

      // XObjects
      else if(name=="Do") { return Do; }

      // text objects
      else if(name=="BT") { return BT; }
      else if(name=="ET") { return ET; }

      // Text state
      else if(name=="Tc") { return Tc; }
      else if(name=="Tf") { return Tf; }
      else if(name=="TL") { return TL; }
      else if(name=="Ts") { return Ts; }
      else if(name=="Tw") { return Tw; }
      else if(name=="Tr") { return Tr; }
      else if(name=="Tz") { return Tz; }

      // Text positioning
      else if(name=="Td") { return Td; }
      else if(name=="TD") { return TD; }
      else if(name=="Tm") { return Tm; }
      else if(name=="T*") { return TStar; }

      // Text showing
      else if(name=="Tj") { return Tj; }
      else if(name=="TJ") { return TJ; }
      else if(name=="'")  { return accent; }
      else if(name=="\"") { return double_accent; }

      // path construction
      else if(name=="m")  { return m; }
      else if(name=="l")  { return l; }
      else if(name=="c")  { return c; }
      else if(name=="v")  { return v; }
      else if(name=="y")  { return y; }
      else if(name=="h")  { return h; }
      else if(name=="re") { return re; }

      // path painting
      else if(name=="s")  { return s; }
      else if(name=="S")  { return S; }
      else if(name=="f")  { return f; }
      else if(name=="F")  { return F; }
      else if(name=="f*") { return fStar; }
      else if(name=="B")  { return B; }
      else if(name=="B*") { return BStar; }
      else if(name=="b")  { return b; }
      else if(name=="b*") { return bStar; }
      else if(name=="n")  { return n; }

      // Clipping paths
      else if(name=="W")  { return W; }
      else if(name=="W*")  { return WStar; }

      // Marked content
      else if(name=="MP")  { return MP; }
      else if(name=="DP")  { return DP; }
      else if(name=="BMC")  { return BMC; }
      else if(name=="BDC")  { return BDC; }
      else if(name=="EMC")  { return EMC; }

      else
        {
          LOG_S(ERROR) << "unsupported operator with name: " << name;
          return null;
        }
    }

    std::string to_string(operator_name name)
    {
      switch(name)
        {
          // General graphics state [Table 57 – Graphics State Operators]
        case w: return "w";
        case J: return "J";
        case j: return "j";
        case M: return "M";
        case d: return "d";
        case ri: return "ri";
        case i: return "i";
        case gs: return "gs";

          // hierarchy

        case q: return "q";
        case Q: return "Q";
        case cm: return "cm";

          // color-scheme
	case CS: return "CS";
	case cs: return "cs";
	case SC: return "SC";
	case SCN: return "SCN";

	case sc: return "sc";
	case scn: return "scn";

	case RG: return "RG";
	case rg: return "rg";
        case g: return "g";
        case G: return "G";

	case K: return "K";
	case k: return "k";

          // trafo's
        case Do: return "Do";

          // text
        case BT: return "BT";
        case ET: return "ET";

        case Tc: return "Tc";

        case Td: return "Td";
        case TD: return "TD";

        case Tf: return "Tf";

        case Tj: return "Tj";
        case TJ: return "TJ";

        case TL: return "TL";

        case Tm: return "Tm";

        case Ts: return "Ts";
        case TStar: return "T*";

        case Tw: return "Tw";
        case Tz: return "Tz";
        case Tr: return "Tr";

        case accent:        return "'";
        case double_accent: return "\"";

          // lines
        case m: return "m";
        case l: return "l";
        case c: return "c";
        case v: return "v";
        case y: return "y";
        case h: return "h";
        case re: return "re";

        case s: return "s";
        case S: return "S";
        case f: return "f";
        case F: return "F";
        case fStar: return "f*";
        case B: return "B";
        case BStar: return "B*";
        case b: return "b";
        case bStar: return "b*";
        case n: return "n";

	case W: return "W";
	case WStar: return "W*";

        default:
          {
            return "null";
          }
        }
    }
  }
}

#endif
