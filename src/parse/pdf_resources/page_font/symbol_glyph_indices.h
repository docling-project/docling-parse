//-*-C++-*-

#ifndef PDF_PAGE_FONT_SYMBOL_GLYPH_INDICES_H
#define PDF_PAGE_FONT_SYMBOL_GLYPH_INDICES_H

#include <array>
#include <cstddef>
#include <string_view>

namespace pdflib
{

  /**
   * @brief Resolves a glyph index from the standard Adobe Symbol glyph order.
   *
   * Some subsetters discard the descriptive glyph names from Symbol and emit
   * `/gNN` instead, where NN is the original Symbol glyph index. The order is
   * stable for the standard Symbol face and is distinct from character-code
   * encodings. Returning the canonical glyph name lets the existing Adobe
   * Glyph List conversion remain the single Unicode authority.
   *
   * @param glyph_index Original standard-Symbol glyph index.
   * @return Canonical glyph name, or an empty view outside the known order.
   * @par Complexity O(1).
   */
  inline std::string_view standard_symbol_glyph_name(std::size_t glyph_index)
  {
    static constexpr std::array<std::string_view, 200> names = {{
      ".notdef", ".null", "nonmarkingreturn", "space", "exclam", "numbersign", "percent", "ampersand",
      "parenleft", "parenright", "plus", "comma", "period", "slash", "zero", "one",
      "two", "three", "four", "five", "six", "seven", "eight", "nine",
      "colon", "semicolon", "less", "equal", "greater", "question", "bracketleft", "bracketright",
      "underscore", "braceleft", "bar", "braceright", "degree", "bullet", "notequal", "infinity",
      "plusminus", "lessequal", "greaterequal", "mu", "partialdiff", "summation", "product", "pi",
      "integral", "Omega", "logicalnot", "radical", "florin", "approxequal", "Delta", "ellipsis",
      "divide", "lozenge", "fraction", "apple", "minus", "multiply", "equivalence", "arrowdown",
      "arrowleft", "arrowright", "arrowup", "arrowboth", "element", "intersection", "union", "integraltp",
      "integralbt", "Alpha", "Beta", "Gamma", "Epsilon", "Zeta", "Eta", "Theta",
      "Iota", "Kappa", "Lambda", "Mu", "Nu", "Xi", "Omicron", "Pi",
      "Rho", "Sigma", "Tau", "Upsilon", "Phi", "Chi", "Psi", "alpha",
      "beta", "gamma", "delta", "zeta", "eta", "theta", "iota", "kappa",
      "lambda", "nu", "xi", "omicron", "rho", "sigma", "sigma1", "tau",
      "upsilon", "phi", "chi", "psi", "omega", "dotmath", "minute", "second",
      "heart", "club", "diamond", "spade", "proportional", "radicalex", "suchthat", "circleplus",
      "circlemultiply", "congruent", "propersuperset", "reflexsuperset", "propersubset", "reflexsubset", "notsubset", "arrowdbldown",
      "arrowdblleft", "arrowdblright", "arrowdblup", "arrowdblboth", "perpendicular", "notelement", "logicaland", "logicalor",
      "angle", "therefore", "emptyset", "integralex", "aleph", "bracketlefttp", "bracketleftbt", "bracketrighttp",
      "bracketrightbt", "universal", "existential", "asteriskmath", "angleright", "angleleft", "theta1", "omega1",
      "phi1", "epsilon", "gradient", "parenlefttp", "parenleftbt", "parenrighttp", "parenrightbt", "weierstrass",
      "bracelefttp", "braceleftmid", "braceleftbt", "braceex", "bracerighttp", "bracerightmid", "bracerightbt", "Upsilon1",
      "arrowvertex", "arrowhorizex", "parenleftex", "bracketleftex", "parenrightex", "bracketrightex", "copyrightserif", "registerserif",
      "trademarkserif", "copyrightsans", "registersans", "trademarksans", "Ifraktur", "Rfraktur", "similar", "carriagereturn",
      "Euro", "soliduslongoverlaycmb", "uniF87F", "uniF870", "uniF871", "uniF872", "subset", "per"
    }};

    return glyph_index < names.size() ? names[glyph_index] : std::string_view{};
  }

}

#endif
