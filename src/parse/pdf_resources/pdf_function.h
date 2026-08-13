//-*-C++-*-

#ifndef PDF_FUNCTION_H
#define PDF_FUNCTION_H

namespace pdflib
{

  // A PDF function (ISO 32000-1, 7.10), shared by shadings and by the tint
  // transforms of Separation / DeviceN colour spaces.
  //
  //   type 0  sampled          one input only (the shading / tint case)
  //   type 2  exponential      one input by definition
  //   type 3  stitching        one input by definition
  //   type 4  PostScript       any arity -- the form tint transforms take
  //
  // Type 4 is a small stack calculator; the interpreter below covers the
  // operator set the spec allows and nothing else.
  class pdf_function
  {
  public:

    bool parse(QPDFObjectHandle obj);

    // Multi-input evaluation; `out` receives every declared output.
    bool eval(const std::vector<double>& in, std::vector<double>& out) const;

    // One-input convenience for shadings.
    bool eval(double t, std::vector<double>& out) const
    {
      return eval(std::vector<double>{t}, out);
    }

  private:

    struct ps_tok
    {
      enum { NUM, OP, PROC } kind = OP;
      double num = 0.0;
      std::string op;
      std::vector<ps_tok> proc;
    };

    static std::vector<double> read_numbers(QPDFObjectHandle obj);
    static double interpolate(double x, double x0, double x1, double y0, double y1);

    static bool ps_tokenize(const std::string& src, size_t& pos, std::vector<ps_tok>& out);
    static bool ps_execute(const std::vector<ps_tok>& prog, std::vector<double>& stack);

    int type = -1;
    std::vector<double> domain;
    std::vector<double> range;

    // type 0
    std::vector<int> size;
    int bits_per_sample = 8;
    std::vector<double> encode;
    std::vector<double> decode;
    std::vector<uint8_t> samples;
    int n_outputs = 0;

    // type 2
    std::vector<double> c0;
    std::vector<double> c1;
    double exponent = 1.0;

    // type 3
    std::vector<pdf_function> functions;
    std::vector<double> bounds;

    // type 4
    std::vector<ps_tok> program;
  };

  inline std::vector<double> pdf_function::read_numbers(QPDFObjectHandle obj)
  {
    std::vector<double> values;
    if(not obj.isArray()) { return values; }
    for(int i = 0; i < obj.getArrayNItems(); i++)
      {
        QPDFObjectHandle item = obj.getArrayItem(i);
        if(item.isNumber()) { values.push_back(item.getNumericValue()); }
      }
    return values;
  }

  inline double pdf_function::interpolate(double x, double x0, double x1,
                                          double y0, double y1)
  {
    if(std::abs(x1 - x0) < 1e-12) { return y0; }
    return y0 + (x - x0) * (y1 - y0) / (x1 - x0);
  }

  inline bool pdf_function::parse(QPDFObjectHandle obj)
  {
    if(not obj.isDictionary() and not obj.isStream()) { return false; }

    QPDFObjectHandle dict = obj.isStream() ? obj.getDict() : obj;

    if(not (dict.hasKey("/FunctionType") and dict.getKey("/FunctionType").isInteger()))
      {
        return false;
      }
    type = static_cast<int>(dict.getKey("/FunctionType").getIntValue());

    if(dict.hasKey("/Domain")) { domain = read_numbers(dict.getKey("/Domain")); }
    if(dict.hasKey("/Range"))  { range  = read_numbers(dict.getKey("/Range")); }

    switch(type)
      {
      case 0:
        {
          if(not obj.isStream() or range.empty()) { return false; }

          n_outputs = static_cast<int>(range.size() / 2);

          for(double v : read_numbers(dict.getKey("/Size")))
            {
              size.push_back(static_cast<int>(v));
            }
          if(size.size() != 1 or size[0] <= 0) { return false; }  // 1-in only

          if(dict.hasKey("/BitsPerSample") and dict.getKey("/BitsPerSample").isInteger())
            {
              bits_per_sample = static_cast<int>(dict.getKey("/BitsPerSample").getIntValue());
            }
          if(dict.hasKey("/Encode")) { encode = read_numbers(dict.getKey("/Encode")); }
          if(dict.hasKey("/Decode")) { decode = read_numbers(dict.getKey("/Decode")); }

          try
            {
              auto buffer = obj.getStreamData();
              if(not buffer or buffer->getSize() == 0) { return false; }
              const uint8_t* raw = reinterpret_cast<const uint8_t*>(buffer->getBuffer());
              samples.assign(raw, raw + buffer->getSize());
            }
          catch(const std::exception& e)
            {
              LOG_S(WARNING) << "pdf_function: sampled stream unreadable: " << e.what();
              return false;
            }
          return true;
        }

      case 2:
        {
          c0 = dict.hasKey("/C0") ? read_numbers(dict.getKey("/C0")) : std::vector<double>{0.0};
          c1 = dict.hasKey("/C1") ? read_numbers(dict.getKey("/C1")) : std::vector<double>{1.0};
          if(dict.hasKey("/N") and dict.getKey("/N").isNumber())
            {
              exponent = dict.getKey("/N").getNumericValue();
            }
          return not c0.empty() and c0.size() == c1.size();
        }

      case 3:
        {
          if(not dict.hasKey("/Functions") or not dict.getKey("/Functions").isArray())
            {
              return false;
            }
          QPDFObjectHandle subs = dict.getKey("/Functions");
          for(int i = 0; i < subs.getArrayNItems(); i++)
            {
              pdf_function sub;
              if(not sub.parse(subs.getArrayItem(i))) { return false; }
              functions.push_back(std::move(sub));
            }
          if(dict.hasKey("/Bounds")) { bounds = read_numbers(dict.getKey("/Bounds")); }
          if(dict.hasKey("/Encode")) { encode = read_numbers(dict.getKey("/Encode")); }
          return not functions.empty();
        }

      case 4:
        {
          if(not obj.isStream() or range.empty()) { return false; }

          std::string src;
          try
            {
              auto buffer = obj.getStreamData();
              if(not buffer or buffer->getSize() == 0) { return false; }
              const char* raw = reinterpret_cast<const char*>(buffer->getBuffer());
              src.assign(raw, buffer->getSize());
            }
          catch(const std::exception& e)
            {
              LOG_S(WARNING) << "pdf_function: calculator stream unreadable: " << e.what();
              return false;
            }

          const size_t open = src.find('{');
          if(open == std::string::npos) { return false; }
          size_t pos = open + 1;
          if(not ps_tokenize(src, pos, program)) { return false; }
          return true;
        }

      default:
        return false;
      }
  }

  // Parses tokens until the matching '}'.
  inline bool pdf_function::ps_tokenize(const std::string& src, size_t& pos,
                                        std::vector<ps_tok>& out)
  {
    while(pos < src.size())
      {
        const char c = src[pos];
        if(std::isspace(static_cast<unsigned char>(c))) { pos++; continue; }

        if(c == '}') { pos++; return true; }

        if(c == '{')
          {
            pos++;
            ps_tok tok;
            tok.kind = ps_tok::PROC;
            if(not ps_tokenize(src, pos, tok.proc)) { return false; }
            out.push_back(std::move(tok));
            continue;
          }

        size_t end = pos;
        while(end < src.size() and
              not std::isspace(static_cast<unsigned char>(src[end])) and
              src[end] != '{' and src[end] != '}')
          {
            end++;
          }
        const std::string word = src.substr(pos, end - pos);
        pos = end;

        ps_tok tok;
        if((word[0] >= '0' and word[0] <= '9') or word[0] == '-' or word[0] == '.')
          {
            try
              {
                tok.kind = ps_tok::NUM;
                tok.num = utils::numeric::locale_safe_stod(word);
              }
            catch(const std::exception&) { return false; }
          }
        else
          {
            tok.kind = ps_tok::OP;
            tok.op = word;
          }
        out.push_back(std::move(tok));
      }
    return false;  // unbalanced braces
  }

  inline bool pdf_function::ps_execute(const std::vector<ps_tok>& prog,
                                       std::vector<double>& st)
  {
    // Procedure operands for if/ifelse are tracked positionally: an executed
    // PROC token pushes a marker onto this side stack, consumed by if/ifelse.
    std::vector<const std::vector<ps_tok>*> procs;

    auto pop = [&](double& v) -> bool
    {
      if(st.empty()) { return false; }
      v = st.back(); st.pop_back(); return true;
    };

    constexpr double pi = 3.14159265358979323846;

    for(const auto& tok : prog)
      {
        if(tok.kind == ps_tok::NUM) { st.push_back(tok.num); continue; }
        if(tok.kind == ps_tok::PROC) { procs.push_back(&tok.proc); continue; }

        const std::string& op = tok.op;
        double a = 0.0, b = 0.0;

        if(op == "add")      { if(not pop(b) or not pop(a)) { return false; } st.push_back(a + b); }
        else if(op == "sub") { if(not pop(b) or not pop(a)) { return false; } st.push_back(a - b); }
        else if(op == "mul") { if(not pop(b) or not pop(a)) { return false; } st.push_back(a * b); }
        else if(op == "div") { if(not pop(b) or not pop(a)) { return false; } st.push_back(b == 0.0 ? 0.0 : a / b); }
        else if(op == "idiv"){ if(not pop(b) or not pop(a)) { return false; } st.push_back(b == 0.0 ? 0.0 : std::trunc(a / b)); }
        else if(op == "mod") { if(not pop(b) or not pop(a)) { return false; } st.push_back(b == 0.0 ? 0.0 : std::fmod(a, b)); }
        else if(op == "neg") { if(not pop(a)) { return false; } st.push_back(-a); }
        else if(op == "abs") { if(not pop(a)) { return false; } st.push_back(std::abs(a)); }
        else if(op == "sqrt"){ if(not pop(a)) { return false; } st.push_back(a > 0.0 ? std::sqrt(a) : 0.0); }
        else if(op == "sin") { if(not pop(a)) { return false; } st.push_back(std::sin(a * pi / 180.0)); }
        else if(op == "cos") { if(not pop(a)) { return false; } st.push_back(std::cos(a * pi / 180.0)); }
        else if(op == "atan")
          {
            if(not pop(b) or not pop(a)) { return false; }
            double deg = std::atan2(a, b) * 180.0 / pi;
            if(deg < 0.0) { deg += 360.0; }
            st.push_back(deg);
          }
        else if(op == "exp") { if(not pop(b) or not pop(a)) { return false; } st.push_back(std::pow(a, b)); }
        else if(op == "ln")  { if(not pop(a)) { return false; } st.push_back(a > 0.0 ? std::log(a) : 0.0); }
        else if(op == "log") { if(not pop(a)) { return false; } st.push_back(a > 0.0 ? std::log10(a) : 0.0); }
        else if(op == "cvi") { if(not pop(a)) { return false; } st.push_back(std::trunc(a)); }
        else if(op == "cvr") { /* every value is already real */ }
        else if(op == "floor")    { if(not pop(a)) { return false; } st.push_back(std::floor(a)); }
        else if(op == "ceiling")  { if(not pop(a)) { return false; } st.push_back(std::ceil(a)); }
        else if(op == "round")    { if(not pop(a)) { return false; } st.push_back(std::round(a)); }
        else if(op == "truncate") { if(not pop(a)) { return false; } st.push_back(std::trunc(a)); }
        else if(op == "dup")  { if(st.empty()) { return false; } st.push_back(st.back()); }
        else if(op == "pop")  { if(not pop(a)) { return false; } }
        else if(op == "exch") { if(not pop(b) or not pop(a)) { return false; } st.push_back(b); st.push_back(a); }
        else if(op == "copy")
          {
            if(not pop(a)) { return false; }
            const int n = static_cast<int>(a);
            if(n < 0 or static_cast<size_t>(n) > st.size()) { return false; }
            const size_t base = st.size() - n;
            for(int i = 0; i < n; i++) { st.push_back(st[base + i]); }
          }
        else if(op == "index")
          {
            if(not pop(a)) { return false; }
            const int n = static_cast<int>(a);
            if(n < 0 or static_cast<size_t>(n) >= st.size()) { return false; }
            st.push_back(st[st.size() - 1 - n]);
          }
        else if(op == "roll")
          {
            if(not pop(b) or not pop(a)) { return false; }
            const int n = static_cast<int>(a);
            int j = static_cast<int>(b);
            if(n < 0 or static_cast<size_t>(n) > st.size()) { return false; }
            if(n > 0 and j != 0)
              {
                j = ((j % n) + n) % n;
                std::rotate(st.end() - n, st.end() - j, st.end());
              }
          }
        else if(op == "eq")  { if(not pop(b) or not pop(a)) { return false; } st.push_back(a == b ? 1.0 : 0.0); }
        else if(op == "ne")  { if(not pop(b) or not pop(a)) { return false; } st.push_back(a != b ? 1.0 : 0.0); }
        else if(op == "gt")  { if(not pop(b) or not pop(a)) { return false; } st.push_back(a >  b ? 1.0 : 0.0); }
        else if(op == "ge")  { if(not pop(b) or not pop(a)) { return false; } st.push_back(a >= b ? 1.0 : 0.0); }
        else if(op == "lt")  { if(not pop(b) or not pop(a)) { return false; } st.push_back(a <  b ? 1.0 : 0.0); }
        else if(op == "le")  { if(not pop(b) or not pop(a)) { return false; } st.push_back(a <= b ? 1.0 : 0.0); }
        else if(op == "and") { if(not pop(b) or not pop(a)) { return false; } st.push_back((a != 0.0 and b != 0.0) ? 1.0 : 0.0); }
        else if(op == "or")  { if(not pop(b) or not pop(a)) { return false; } st.push_back((a != 0.0 or  b != 0.0) ? 1.0 : 0.0); }
        else if(op == "xor") { if(not pop(b) or not pop(a)) { return false; } st.push_back(((a != 0.0) != (b != 0.0)) ? 1.0 : 0.0); }
        else if(op == "not") { if(not pop(a)) { return false; } st.push_back(a == 0.0 ? 1.0 : 0.0); }
        else if(op == "bitshift")
          {
            if(not pop(b) or not pop(a)) { return false; }
            const long ai = static_cast<long>(a);
            const int  s  = static_cast<int>(b);
            st.push_back(static_cast<double>(s >= 0 ? (ai << s) : (ai >> -s)));
          }
        else if(op == "true")  { st.push_back(1.0); }
        else if(op == "false") { st.push_back(0.0); }
        else if(op == "if")
          {
            if(procs.empty() or not pop(a)) { return false; }
            const std::vector<ps_tok>* body = procs.back(); procs.pop_back();
            if(a != 0.0)
              {
                if(not ps_execute(*body, st)) { return false; }
              }
          }
        else if(op == "ifelse")
          {
            if(procs.size() < 2 or not pop(a)) { return false; }
            const std::vector<ps_tok>* else_body = procs.back(); procs.pop_back();
            const std::vector<ps_tok>* then_body = procs.back(); procs.pop_back();
            if(not ps_execute(a != 0.0 ? *then_body : *else_body, st)) { return false; }
          }
        else
          {
            LOG_S(WARNING) << "pdf_function: unsupported calculator operator '" << op << "'";
            return false;
          }
      }
    return true;
  }

  inline bool pdf_function::eval(const std::vector<double>& in,
                                 std::vector<double>& out) const
  {
    if(in.empty()) { return false; }

    // Clamp inputs to /Domain.
    std::vector<double> args = in;
    for(size_t i = 0; i < args.size(); i++)
      {
        const double d0 = (domain.size() >= 2 * (i + 1)) ? domain[2 * i] : 0.0;
        const double d1 = (domain.size() >= 2 * (i + 1)) ? domain[2 * i + 1] : 1.0;
        args[i] = std::max(d0, std::min(d1, args[i]));
      }

    switch(type)
      {
      case 0:
        {
          if(size.empty() or n_outputs <= 0 or samples.empty()) { return false; }

          const double d0 = (domain.size() >= 2) ? domain[0] : 0.0;
          const double d1 = (domain.size() >= 2) ? domain[1] : 1.0;
          const double e0 = (encode.size() >= 2) ? encode[0] : 0.0;
          const double e1 = (encode.size() >= 2) ? encode[1] : (size[0] - 1);

          double pos = interpolate(args[0], d0, d1, e0, e1);
          pos = std::max(0.0, std::min(static_cast<double>(size[0] - 1), pos));
          const int idx = static_cast<int>(std::lround(pos));

          const double max_value = std::pow(2.0, bits_per_sample) - 1.0;
          out.assign(static_cast<size_t>(n_outputs), 0.0);

          for(int j = 0; j < n_outputs; j++)
            {
              const size_t bit_offset =
                (static_cast<size_t>(idx) * n_outputs + j) * bits_per_sample;
              uint32_t raw = 0;
              if(bits_per_sample == 8)
                {
                  const size_t byte = bit_offset / 8;
                  if(byte >= samples.size()) { return false; }
                  raw = samples[byte];
                }
              else if(bits_per_sample == 16)
                {
                  const size_t byte = bit_offset / 8;
                  if(byte + 1 >= samples.size()) { return false; }
                  raw = (static_cast<uint32_t>(samples[byte]) << 8) | samples[byte + 1];
                }
              else
                {
                  for(int bit = 0; bit < bits_per_sample; bit++)
                    {
                      const size_t abs_bit = bit_offset + bit;
                      if(abs_bit / 8 >= samples.size()) { return false; }
                      raw = (raw << 1) | ((samples[abs_bit / 8] >> (7 - (abs_bit % 8))) & 1u);
                    }
                }

              const double dmin = (decode.size() >= static_cast<size_t>(2 * j + 2))
                                    ? decode[2 * j] : range[2 * j];
              const double dmax = (decode.size() >= static_cast<size_t>(2 * j + 2))
                                    ? decode[2 * j + 1] : range[2 * j + 1];
              out[j] = interpolate(static_cast<double>(raw), 0.0, max_value, dmin, dmax);
            }
          return true;
        }

      case 2:
        {
          const double d0 = (domain.size() >= 2) ? domain[0] : 0.0;
          const double d1 = (domain.size() >= 2) ? domain[1] : 1.0;
          const double span = (d1 - d0) == 0.0 ? 1.0 : (d1 - d0);
          const double f = std::pow((args[0] - d0) / span, exponent);
          out.assign(c0.size(), 0.0);
          for(size_t j = 0; j < c0.size(); j++)
            {
              out[j] = c0[j] + f * (c1[j] - c0[j]);
            }
          return true;
        }

      case 3:
        {
          const double d0 = (domain.size() >= 2) ? domain[0] : 0.0;
          const double d1 = (domain.size() >= 2) ? domain[1] : 1.0;
          const double t = args[0];

          size_t k = 0;
          while(k < bounds.size() and t >= bounds[k]) { k++; }
          if(k >= functions.size()) { k = functions.size() - 1; }

          const double low  = (k == 0) ? d0 : bounds[k - 1];
          const double high = (k == bounds.size()) ? d1 : bounds[k];
          const double e0 = (encode.size() >= 2 * (k + 1)) ? encode[2 * k] : 0.0;
          const double e1 = (encode.size() >= 2 * (k + 1)) ? encode[2 * k + 1] : 1.0;

          return functions[k].eval(interpolate(t, low, high, e0, e1), out);
        }

      case 4:
        {
          std::vector<double> stack = args;
          if(not ps_execute(program, stack)) { return false; }

          const size_t n = range.size() / 2;
          if(stack.size() < n) { return false; }

          out.assign(n, 0.0);
          for(size_t j = 0; j < n; j++)
            {
              double v = stack[stack.size() - n + j];
              v = std::max(range[2 * j], std::min(range[2 * j + 1], v));
              out[j] = v;
            }
          return true;
        }

      default:
        return false;
      }
  }

}

#endif
