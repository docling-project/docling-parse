//-*-C++-*-

#ifndef PDF_FUNCTION_H
#define PDF_FUNCTION_H

namespace pdflib
{

  // A PDF function object (ISO 32000-1, 7.10). Functions map m input values
  // onto n output values. They give a shading its colours -- so a shading can
  // only be painted when its function decodes -- and carry the tint transform
  // of /Separation and /DeviceN colour spaces.
  //
  // Implemented: type 0 (sampled), type 2 (exponential interpolation), type 3
  // (stitching) and type 4 (PostScript calculator). Types 2 and 3 are
  // single-input by definition; types 0 and 4 take as many inputs as their
  // /Domain declares, which is what a multi-colorant tint transform needs.
  //
  // A function that fails to decode reports is_valid() == false together with
  // a reason, so the caller can say *why* nothing was painted.
  class pdf_function
  {
  public:

    pdf_function();

    // Decodes `obj` (a function dictionary or stream). Returns is_valid().
    // `depth` guards against /Functions arrays that reference each other.
    bool set(QPDFObjectHandle obj, int depth = 0);

    bool is_valid() const { return valid_; }
    const std::string& get_reason() const { return reason_; }

    int get_type() const { return type_; }
    int get_num_inputs() const { return static_cast<int>(domain_.size() / 2); }
    int get_num_outputs() const;

    // Evaluates the function. Values are clipped to /Domain on the way in and
    // to /Range on the way out, as required by 7.10.1. Returns false when the
    // function is invalid, was handed the wrong number of inputs, or produced
    // no outputs. The scalar overload is the single-input case.
    bool evaluate(double in, std::vector<double>& out) const;
    bool evaluate(const std::vector<double>& in, std::vector<double>& out) const;

  private:

    // --- PostScript calculator (type 4) program representation ---------------

    enum ps_operator {
      PS_ABS, PS_ADD, PS_ATAN, PS_CEILING, PS_COS, PS_CVI, PS_CVR, PS_DIV,
      PS_EXP, PS_FLOOR, PS_IDIV, PS_LN, PS_LOG, PS_MOD, PS_MUL, PS_NEG,
      PS_ROUND, PS_SIN, PS_SQRT, PS_SUB, PS_TRUNCATE,

      PS_AND, PS_BITSHIFT, PS_EQ, PS_FALSE, PS_GE, PS_GT, PS_LE, PS_LT,
      PS_NE, PS_NOT, PS_OR, PS_TRUE, PS_XOR,

      PS_IF, PS_IFELSE,

      PS_COPY, PS_DUP, PS_EXCH, PS_INDEX, PS_POP, PS_ROLL
    };

    // One element of a type 4 program: a literal, an operator, or a nested
    // `{ ... }` procedure (the operand of `if` / `ifelse`).
    struct ps_node
    {
      enum kind_type { LITERAL, OPERATOR, PROCEDURE } kind = LITERAL;

      double       literal = 0.0;
      bool         literal_is_bool = false;
      ps_operator  op = PS_ABS;

      std::vector<ps_node> procedure;
    };

    // A type 4 stack value. Booleans are tracked separately because `not`,
    // `and`, `or` and `xor` are logical on booleans and bitwise on integers
    // (Table 42).
    struct ps_value
    {
      double num = 0.0;
      bool   is_bool = false;
    };

  private:

    void parse_domain(QPDFObjectHandle dict);
    void parse_range(QPDFObjectHandle dict);

    bool parse_sampled(QPDFObjectHandle obj);
    bool parse_exponential(QPDFObjectHandle dict);
    bool parse_stitching(QPDFObjectHandle obj, int depth);
    bool parse_postscript(QPDFObjectHandle obj);

    bool evaluate_sampled(const std::vector<double>& x, std::vector<double>& out) const;
    bool evaluate_exponential(double x, std::vector<double>& out) const;
    bool evaluate_stitching(double x, std::vector<double>& out) const;
    bool evaluate_postscript(const std::vector<double>& x, std::vector<double>& out) const;

    void clip_to_range(std::vector<double>& out) const;

    bool invalidate(const std::string& reason);

    // One sample of the type 0 table, de-quantised into [Decode] units.
    double sample_value(std::size_t index, int output) const;

    static std::vector<double> to_double_vector(QPDFObjectHandle obj);

    static bool tokenize_postscript(const std::string& program,
                                    std::vector<std::string>& tokens);
    static bool parse_postscript_block(const std::vector<std::string>& tokens,
                                       std::size_t& pos,
                                       std::vector<ps_node>& block);
    static bool to_ps_operator(const std::string& token, ps_operator& op);

    static bool run_postscript(const std::vector<ps_node>& block,
                               std::vector<ps_value>& stack);

    // degrees <-> radians: the type 4 trigonometric operators work in
    // degrees (Table 42)
    const static inline double DEG_TO_RAD = 3.14159265358979323846 / 180.0;

    // A type 0 lookup interpolates between the 2^m samples surrounding the
    // input point, so the input arity has to stay small. Eight is already far
    // beyond the colorant counts /DeviceN uses in practice.
    const static inline std::size_t max_sampled_inputs = 8;

    static double interpolate(double x,
                              double x_min, double x_max,
                              double y_min, double y_max);

  private:

    bool        valid_;
    std::string reason_;

    int type_;

    std::vector<double> domain_;
    std::vector<double> range_;

    // type 0
    std::vector<int>      size_;
    int                   bits_per_sample_;
    std::vector<double>   encode_;
    std::vector<double>   decode_;
    std::vector<uint8_t>  samples_;
    int                   num_outputs_;

    // type 2
    std::vector<double> c0_;
    std::vector<double> c1_;
    double              exponent_;

    // type 3
    std::vector<std::shared_ptr<pdf_function>> functions_;
    std::vector<double>                        bounds_;

    // type 4
    std::vector<ps_node> program_;
  };

  inline pdf_function::pdf_function():
    valid_(false),
    reason_("not decoded"),
    type_(-1),
    domain_({}),
    range_({}),
    size_({}),
    bits_per_sample_(0),
    encode_({}),
    decode_({}),
    samples_({}),
    num_outputs_(0),
    c0_({}),
    c1_({}),
    exponent_(1.0),
    functions_({}),
    bounds_({}),
    program_({})
  {}

  inline int pdf_function::get_num_outputs() const
  {
    if(not range_.empty())
      {
        return static_cast<int>(range_.size() / 2);
      }

    // /Range is optional for types 2 and 3; their output arity follows from
    // /C0 and /C1 (7.10.3) resp. from the sub-functions (7.10.4).
    switch(type_)
      {
      case 2: { return static_cast<int>(std::max(c0_.size(), c1_.size())); }
      case 3:
        {
          if(not functions_.empty() and functions_.front())
            {
              return functions_.front()->get_num_outputs();
            }
          return 0;
        }
      default: { return 0; }
      }
  }

  inline bool pdf_function::invalidate(const std::string& reason)
  {
    valid_ = false;
    reason_ = reason;
    return false;
  }

  inline std::vector<double> pdf_function::to_double_vector(QPDFObjectHandle obj)
  {
    std::vector<double> values;

    if(not obj.isArray())
      {
        return values;
      }

    const int n = obj.getArrayNItems();
    values.reserve(static_cast<std::size_t>(n));

    for(int i = 0; i < n; i++)
      {
        QPDFObjectHandle item = obj.getArrayItem(i);
        values.push_back(item.isNumber() ? item.getNumericValue() : 0.0);
      }

    return values;
  }

  inline double pdf_function::interpolate(double x,
                                          double x_min, double x_max,
                                          double y_min, double y_max)
  {
    if(x_max == x_min)
      {
        return y_min;
      }

    return y_min + (x - x_min) * (y_max - y_min) / (x_max - x_min);
  }

  inline void pdf_function::parse_domain(QPDFObjectHandle dict)
  {
    if(dict.hasKey("/Domain"))
      {
        domain_ = to_double_vector(dict.getKey("/Domain"));
      }
  }

  inline void pdf_function::parse_range(QPDFObjectHandle dict)
  {
    if(dict.hasKey("/Range"))
      {
        range_ = to_double_vector(dict.getKey("/Range"));
      }
  }

  inline bool pdf_function::set(QPDFObjectHandle obj, int depth)
  {
    valid_ = false;
    reason_ = "";

    if(depth > 8)
      {
        return invalidate("function nesting is deeper than 8 levels");
      }

    QPDFObjectHandle dict = obj.isStream() ? obj.getDict() : obj;

    if(not dict.isDictionary())
      {
        return invalidate("function object is neither a dictionary nor a stream");
      }

    if(not dict.hasKey("/FunctionType") or
       not dict.getKey("/FunctionType").isInteger())
      {
        return invalidate("function has no integer /FunctionType");
      }

    type_ = static_cast<int>(dict.getKey("/FunctionType").getIntValue());

    parse_domain(dict);
    parse_range(dict);

    if(domain_.size() < 2)
      {
        return invalidate("function has no valid /Domain");
      }

    bool ok = false;
    switch(type_)
      {
      case 0: { ok = parse_sampled(obj);        } break;
      case 2: { ok = parse_exponential(dict);   } break;
      case 3: { ok = parse_stitching(obj, depth); } break;
      case 4: { ok = parse_postscript(obj);     } break;

      default:
        {
          return invalidate("unsupported /FunctionType " + std::to_string(type_));
        }
      }

    if(not ok)
      {
        return false;
      }

    if(get_num_outputs() <= 0)
      {
        return invalidate("function has no outputs");
      }

    valid_ = true;
    reason_ = "";

    return true;
  }

  // 7.10.2: samples are a packed table of Size[0] x ... x Size[m-1] entries of
  // n BitsPerSample-wide values each, with the first input varying fastest.
  inline bool pdf_function::parse_sampled(QPDFObjectHandle obj)
  {
    if(not obj.isStream())
      {
        return invalidate("sampled function (type 0) is not a stream");
      }

    QPDFObjectHandle dict = obj.getDict();

    const std::size_t m = domain_.size() / 2;

    if(m > max_sampled_inputs)
      {
        return invalidate("sampled function (type 0) with " + std::to_string(m) +
                          " inputs is not supported (at most " +
                          std::to_string(max_sampled_inputs) + ")");
      }

    if(range_.size() < 2)
      {
        return invalidate("sampled function (type 0) has no valid /Range");
      }
    num_outputs_ = static_cast<int>(range_.size() / 2);

    if(not dict.hasKey("/Size"))
      {
        return invalidate("sampled function (type 0) has no /Size");
      }

    for(double value : to_double_vector(dict.getKey("/Size")))
      {
        size_.push_back(static_cast<int>(std::lround(value)));
      }

    if(size_.size() != m)
      {
        return invalidate("sampled function (type 0) has " +
                          std::to_string(size_.size()) + " /Size entries for " +
                          std::to_string(m) + " inputs");
      }

    // The table is Size[0] x ... x Size[m-1] entries. The product is taken
    // from an untrusted file, so it is capped rather than allowed to wrap:
    // 2^26 samples is already two orders of magnitude past anything real.
    static constexpr std::size_t max_samples = std::size_t(1) << 26;

    std::size_t num_samples = 1;
    for(int size : size_)
      {
        if(size < 1)
          {
            return invalidate("sampled function (type 0) has an invalid /Size");
          }

        if(static_cast<std::size_t>(size) > max_samples / num_samples)
          {
            return invalidate("sampled function (type 0) declares more than " +
                              std::to_string(max_samples) + " samples");
          }

        num_samples *= static_cast<std::size_t>(size);
      }

    if(not dict.hasKey("/BitsPerSample") or
       not dict.getKey("/BitsPerSample").isInteger())
      {
        return invalidate("sampled function (type 0) has no /BitsPerSample");
      }
    bits_per_sample_ = static_cast<int>(dict.getKey("/BitsPerSample").getIntValue());

    switch(bits_per_sample_)
      {
      case 1: case 2: case 4: case 8: case 12: case 16: case 24: case 32: break;
      default:
        {
          return invalidate("sampled function (type 0) has an unsupported "
                            "/BitsPerSample " + std::to_string(bits_per_sample_));
        }
      }

    // defaults per Table 38: Encode = [0 Size_0-1 ... 0 Size_(m-1)-1],
    // Decode = Range
    if(dict.hasKey("/Encode"))
      {
        encode_ = to_double_vector(dict.getKey("/Encode"));
      }
    else
      {
        for(int size : size_)
          {
            encode_.push_back(0.0);
            encode_.push_back(static_cast<double>(size - 1));
          }
      }
    decode_ = dict.hasKey("/Decode") ? to_double_vector(dict.getKey("/Decode"))
                                     : range_;

    if(encode_.size() < 2 * m)
      {
        return invalidate("sampled function (type 0) has an invalid /Encode");
      }
    if(decode_.size() < range_.size())
      {
        return invalidate("sampled function (type 0) has an invalid /Decode");
      }

    try
      {
        auto buffer = obj.getStreamData(qpdf_dl_generalized);
        const unsigned char* data = buffer->getBuffer();
        samples_.assign(data, data + buffer->getSize());
      }
    catch(const std::exception& e)
      {
        return invalidate(std::string("sampled function (type 0) stream could not "
                                      "be decoded: ") + e.what());
      }

    const std::size_t needed_bits =
      num_samples * static_cast<std::size_t>(num_outputs_) *
      static_cast<std::size_t>(bits_per_sample_);
    const std::size_t needed_bytes = (needed_bits + 7) / 8;

    if(samples_.size() < needed_bytes)
      {
        return invalidate("sampled function (type 0) stream holds " +
                          std::to_string(samples_.size()) + " bytes but needs " +
                          std::to_string(needed_bytes));
      }

    return true;
  }

  inline bool pdf_function::parse_exponential(QPDFObjectHandle dict)
  {
    if(domain_.size() != 2)
      {
        return invalidate("exponential function (type 2) takes one input, but "
                          "its /Domain declares " +
                          std::to_string(domain_.size() / 2));
      }

    c0_ = dict.hasKey("/C0") ? to_double_vector(dict.getKey("/C0"))
                             : std::vector<double>{0.0};
    c1_ = dict.hasKey("/C1") ? to_double_vector(dict.getKey("/C1"))
                             : std::vector<double>{1.0};

    if(c0_.size() != c1_.size())
      {
        return invalidate("exponential function (type 2) has /C0 and /C1 of "
                          "different sizes");
      }

    exponent_ = 1.0;
    if(dict.hasKey("/N") and dict.getKey("/N").isNumber())
      {
        exponent_ = dict.getKey("/N").getNumericValue();
      }

    return true;
  }

  inline bool pdf_function::parse_stitching(QPDFObjectHandle obj, int depth)
  {
    QPDFObjectHandle dict = obj.isStream() ? obj.getDict() : obj;

    if(domain_.size() != 2)
      {
        return invalidate("stitching function (type 3) takes one input, but "
                          "its /Domain declares " +
                          std::to_string(domain_.size() / 2));
      }

    if(not dict.hasKey("/Functions") or not dict.getKey("/Functions").isArray())
      {
        return invalidate("stitching function (type 3) has no /Functions array");
      }

    QPDFObjectHandle qpdf_functions = dict.getKey("/Functions");
    const int k = qpdf_functions.getArrayNItems();

    if(k < 1)
      {
        return invalidate("stitching function (type 3) has an empty /Functions array");
      }

    for(int i = 0; i < k; i++)
      {
        auto sub = std::make_shared<pdf_function>();
        if(not sub->set(qpdf_functions.getArrayItem(i), depth + 1))
          {
            return invalidate("stitching function (type 3) sub-function " +
                              std::to_string(i) + ": " + sub->get_reason());
          }
        functions_.push_back(sub);
      }

    bounds_ = dict.hasKey("/Bounds") ? to_double_vector(dict.getKey("/Bounds"))
                                     : std::vector<double>{};
    encode_ = dict.hasKey("/Encode") ? to_double_vector(dict.getKey("/Encode"))
                                     : std::vector<double>{};

    if(bounds_.size() != static_cast<std::size_t>(k - 1))
      {
        return invalidate("stitching function (type 3) has " +
                          std::to_string(bounds_.size()) + " /Bounds for " +
                          std::to_string(k) + " functions");
      }

    if(encode_.size() != static_cast<std::size_t>(2 * k))
      {
        return invalidate("stitching function (type 3) has an /Encode of " +
                          std::to_string(encode_.size()) + " entries instead of " +
                          std::to_string(2 * k));
      }

    return true;
  }

  inline bool pdf_function::parse_postscript(QPDFObjectHandle obj)
  {
    if(not obj.isStream())
      {
        return invalidate("PostScript calculator function (type 4) is not a stream");
      }

    if(range_.size() < 2)
      {
        return invalidate("PostScript calculator function (type 4) has no /Range");
      }

    std::string program;
    try
      {
        auto buffer = obj.getStreamData(qpdf_dl_generalized);
        program.assign(reinterpret_cast<const char*>(buffer->getBuffer()),
                       buffer->getSize());
      }
    catch(const std::exception& e)
      {
        return invalidate(std::string("PostScript calculator function (type 4) "
                                      "stream could not be decoded: ") + e.what());
      }

    std::vector<std::string> tokens;
    if(not tokenize_postscript(program, tokens))
      {
        return invalidate("PostScript calculator function (type 4) program could "
                          "not be tokenized");
      }

    // the program is a single outermost `{ ... }` block
    std::size_t pos = 0;
    if(pos >= tokens.size() or tokens[pos] != "{")
      {
        return invalidate("PostScript calculator function (type 4) program does "
                          "not start with '{'");
      }
    pos++;

    program_.clear();
    if(not parse_postscript_block(tokens, pos, program_))
      {
        return invalidate("PostScript calculator function (type 4) program is "
                          "malformed");
      }

    return true;
  }

  inline bool pdf_function::tokenize_postscript(const std::string& program,
                                                std::vector<std::string>& tokens)
  {
    std::string token;

    auto flush = [&]()
      {
        if(not token.empty())
          {
            tokens.push_back(token);
            token.clear();
          }
      };

    for(std::size_t i = 0; i < program.size(); i++)
      {
        const char c = program[i];

        if(c == '%') // comment runs to the end of the line
          {
            flush();
            while(i < program.size() and program[i] != '\n' and program[i] != '\r')
              {
                i++;
              }
            continue;
          }

        if(c == '{' or c == '}')
          {
            flush();
            tokens.push_back(std::string(1, c));
            continue;
          }

        if(std::isspace(static_cast<unsigned char>(c)))
          {
            flush();
            continue;
          }

        token.push_back(c);
      }

    flush();

    return not tokens.empty();
  }

  inline bool pdf_function::to_ps_operator(const std::string& token, ps_operator& op)
  {
    static const std::unordered_map<std::string, ps_operator> table = {
      {"abs", PS_ABS}, {"add", PS_ADD}, {"atan", PS_ATAN}, {"ceiling", PS_CEILING},
      {"cos", PS_COS}, {"cvi", PS_CVI}, {"cvr", PS_CVR}, {"div", PS_DIV},
      {"exp", PS_EXP}, {"floor", PS_FLOOR}, {"idiv", PS_IDIV}, {"ln", PS_LN},
      {"log", PS_LOG}, {"mod", PS_MOD}, {"mul", PS_MUL}, {"neg", PS_NEG},
      {"round", PS_ROUND}, {"sin", PS_SIN}, {"sqrt", PS_SQRT}, {"sub", PS_SUB},
      {"truncate", PS_TRUNCATE},

      {"and", PS_AND}, {"bitshift", PS_BITSHIFT}, {"eq", PS_EQ},
      {"false", PS_FALSE}, {"ge", PS_GE}, {"gt", PS_GT}, {"le", PS_LE},
      {"lt", PS_LT}, {"ne", PS_NE}, {"not", PS_NOT}, {"or", PS_OR},
      {"true", PS_TRUE}, {"xor", PS_XOR},

      {"if", PS_IF}, {"ifelse", PS_IFELSE},

      {"copy", PS_COPY}, {"dup", PS_DUP}, {"exch", PS_EXCH}, {"index", PS_INDEX},
      {"pop", PS_POP}, {"roll", PS_ROLL}
    };

    auto itr = table.find(token);
    if(itr == table.end())
      {
        return false;
      }

    op = itr->second;
    return true;
  }

  // Parses tokens up to the matching '}' into `block`. `pos` points just past
  // the opening '{' on entry and just past the closing '}' on return.
  inline bool pdf_function::parse_postscript_block(const std::vector<std::string>& tokens,
                                                   std::size_t& pos,
                                                   std::vector<ps_node>& block)
  {
    while(pos < tokens.size())
      {
        const std::string& token = tokens[pos];

        if(token == "}")
          {
            pos++;
            return true;
          }

        if(token == "{")
          {
            pos++;

            ps_node node;
            node.kind = ps_node::PROCEDURE;
            if(not parse_postscript_block(tokens, pos, node.procedure))
              {
                return false;
              }

            block.push_back(std::move(node));
            continue;
          }

        pos++;

        ps_operator op;
        if(to_ps_operator(token, op))
          {
            ps_node node;

            if(op == PS_TRUE or op == PS_FALSE)
              {
                node.kind = ps_node::LITERAL;
                node.literal = (op == PS_TRUE) ? 1.0 : 0.0;
                node.literal_is_bool = true;
              }
            else
              {
                node.kind = ps_node::OPERATOR;
                node.op = op;
              }

            block.push_back(std::move(node));
            continue;
          }

        try
          {
            std::size_t consumed = 0;
            const double value = std::stod(token, &consumed);

            if(consumed != token.size())
              {
                return false;
              }

            ps_node node;
            node.kind = ps_node::LITERAL;
            node.literal = value;
            block.push_back(std::move(node));
          }
        catch(const std::exception&)
          {
            return false;
          }
      }

    return false; // ran out of tokens before the closing '}'
  }

  inline bool pdf_function::run_postscript(const std::vector<ps_node>& block,
                                           std::vector<ps_value>& stack)
  {
    // guards a program that pushes without bound (the PDF is untrusted input)
    static constexpr std::size_t max_stack = 1000;

    auto need = [&](std::size_t n) { return stack.size() >= n; };

    auto pop = [&]() -> ps_value
      {
        ps_value value = stack.back();
        stack.pop_back();
        return value;
      };

    auto push = [&](double num, bool is_bool = false)
      {
        stack.push_back(ps_value{num, is_bool});
      };

    for(std::size_t i = 0; i < block.size(); i++)
      {
        const ps_node& node = block[i];

        if(node.kind == ps_node::LITERAL)
          {
            if(stack.size() >= max_stack) { return false; }
            push(node.literal, node.literal_is_bool);
            continue;
          }

        // A procedure is only ever an operand of `if` / `ifelse`, which
        // consume it from the node list rather than from the value stack.
        if(node.kind == ps_node::PROCEDURE)
          {
            const bool is_ifelse = (i + 2 < block.size() and
                                    block[i + 1].kind == ps_node::PROCEDURE and
                                    block[i + 2].kind == ps_node::OPERATOR and
                                    block[i + 2].op == PS_IFELSE);
            const bool is_if = (i + 1 < block.size() and
                                block[i + 1].kind == ps_node::OPERATOR and
                                block[i + 1].op == PS_IF);

            if(is_ifelse)
              {
                if(not need(1)) { return false; }
                const ps_value cond = pop();

                const std::vector<ps_node>& chosen =
                  (cond.num != 0.0) ? node.procedure : block[i + 1].procedure;

                if(not run_postscript(chosen, stack)) { return false; }

                i += 2;
                continue;
              }

            if(is_if)
              {
                if(not need(1)) { return false; }
                const ps_value cond = pop();

                if(cond.num != 0.0 and not run_postscript(node.procedure, stack))
                  {
                    return false;
                  }

                i += 1;
                continue;
              }

            return false; // a procedure that is not an if/ifelse operand
          }

        switch(node.op)
          {
          case PS_ABS:
            { if(not need(1)) { return false; } double a = pop().num; push(std::abs(a)); } break;
          case PS_ADD:
            { if(not need(2)) { return false; } double b = pop().num, a = pop().num; push(a + b); } break;
          case PS_ATAN:
            {
              if(not need(2)) { return false; }
              double den = pop().num, num = pop().num;
              double deg = std::atan2(num, den) / DEG_TO_RAD;
              if(deg < 0.0) { deg += 360.0; } // PDF wants [0, 360)
              push(deg);
            }
            break;
          case PS_CEILING:
            { if(not need(1)) { return false; } double a = pop().num; push(std::ceil(a)); } break;
          case PS_COS:
            { if(not need(1)) { return false; } double a = pop().num; push(std::cos(a * DEG_TO_RAD)); } break;
          case PS_CVI:
            { if(not need(1)) { return false; } double a = pop().num; push(std::trunc(a)); } break;
          case PS_CVR:
            { if(not need(1)) { return false; } } break;
          case PS_DIV:
            {
              if(not need(2)) { return false; }
              double b = pop().num, a = pop().num;
              if(b == 0.0) { return false; }
              push(a / b);
            }
            break;
          case PS_EXP:
            { if(not need(2)) { return false; } double b = pop().num, a = pop().num; push(std::pow(a, b)); } break;
          case PS_FLOOR:
            { if(not need(1)) { return false; } double a = pop().num; push(std::floor(a)); } break;
          case PS_IDIV:
            {
              if(not need(2)) { return false; }
              long b = static_cast<long>(pop().num), a = static_cast<long>(pop().num);
              if(b == 0) { return false; }
              push(static_cast<double>(a / b));
            }
            break;
          case PS_LN:
            {
              if(not need(1)) { return false; }
              double a = pop().num;
              if(a <= 0.0) { return false; }
              push(std::log(a));
            }
            break;
          case PS_LOG:
            {
              if(not need(1)) { return false; }
              double a = pop().num;
              if(a <= 0.0) { return false; }
              push(std::log10(a));
            }
            break;
          case PS_MOD:
            {
              if(not need(2)) { return false; }
              long b = static_cast<long>(pop().num), a = static_cast<long>(pop().num);
              if(b == 0) { return false; }
              push(static_cast<double>(a % b));
            }
            break;
          case PS_MUL:
            { if(not need(2)) { return false; } double b = pop().num, a = pop().num; push(a * b); } break;
          case PS_NEG:
            { if(not need(1)) { return false; } double a = pop().num; push(-a); } break;
          case PS_ROUND:
            { if(not need(1)) { return false; } double a = pop().num; push(std::round(a)); } break;
          case PS_SIN:
            { if(not need(1)) { return false; } double a = pop().num; push(std::sin(a * DEG_TO_RAD)); } break;
          case PS_SQRT:
            {
              if(not need(1)) { return false; }
              double a = pop().num;
              if(a < 0.0) { return false; }
              push(std::sqrt(a));
            }
            break;
          case PS_SUB:
            { if(not need(2)) { return false; } double b = pop().num, a = pop().num; push(a - b); } break;
          case PS_TRUNCATE:
            { if(not need(1)) { return false; } double a = pop().num; push(std::trunc(a)); } break;

          case PS_AND:
            {
              if(not need(2)) { return false; }
              ps_value b = pop(), a = pop();
              if(a.is_bool or b.is_bool)
                {
                  push((a.num != 0.0 and b.num != 0.0) ? 1.0 : 0.0, true);
                }
              else
                {
                  push(static_cast<double>(static_cast<long>(a.num) & static_cast<long>(b.num)));
                }
            }
            break;
          case PS_OR:
            {
              if(not need(2)) { return false; }
              ps_value b = pop(), a = pop();
              if(a.is_bool or b.is_bool)
                {
                  push((a.num != 0.0 or b.num != 0.0) ? 1.0 : 0.0, true);
                }
              else
                {
                  push(static_cast<double>(static_cast<long>(a.num) | static_cast<long>(b.num)));
                }
            }
            break;
          case PS_XOR:
            {
              if(not need(2)) { return false; }
              ps_value b = pop(), a = pop();
              if(a.is_bool or b.is_bool)
                {
                  push(((a.num != 0.0) != (b.num != 0.0)) ? 1.0 : 0.0, true);
                }
              else
                {
                  push(static_cast<double>(static_cast<long>(a.num) ^ static_cast<long>(b.num)));
                }
            }
            break;
          case PS_NOT:
            {
              if(not need(1)) { return false; }
              ps_value a = pop();
              if(a.is_bool)
                {
                  push((a.num == 0.0) ? 1.0 : 0.0, true);
                }
              else
                {
                  push(static_cast<double>(~static_cast<long>(a.num)));
                }
            }
            break;
          case PS_BITSHIFT:
            {
              if(not need(2)) { return false; }
              long shift = static_cast<long>(pop().num);
              long a = static_cast<long>(pop().num);
              if(shift >= 0)
                {
                  push(static_cast<double>(a << std::min(shift, 63L)));
                }
              else
                {
                  push(static_cast<double>(a >> std::min(-shift, 63L)));
                }
            }
            break;

          case PS_EQ:
            { if(not need(2)) { return false; } double b = pop().num, a = pop().num; push(a == b ? 1.0 : 0.0, true); } break;
          case PS_NE:
            { if(not need(2)) { return false; } double b = pop().num, a = pop().num; push(a != b ? 1.0 : 0.0, true); } break;
          case PS_GE:
            { if(not need(2)) { return false; } double b = pop().num, a = pop().num; push(a >= b ? 1.0 : 0.0, true); } break;
          case PS_GT:
            { if(not need(2)) { return false; } double b = pop().num, a = pop().num; push(a >  b ? 1.0 : 0.0, true); } break;
          case PS_LE:
            { if(not need(2)) { return false; } double b = pop().num, a = pop().num; push(a <= b ? 1.0 : 0.0, true); } break;
          case PS_LT:
            { if(not need(2)) { return false; } double b = pop().num, a = pop().num; push(a <  b ? 1.0 : 0.0, true); } break;

          case PS_POP:
            { if(not need(1)) { return false; } pop(); } break;
          case PS_DUP:
            {
              if(not need(1) or stack.size() >= max_stack) { return false; }
              stack.push_back(stack.back());
            }
            break;
          case PS_EXCH:
            {
              if(not need(2)) { return false; }
              std::swap(stack[stack.size() - 1], stack[stack.size() - 2]);
            }
            break;
          case PS_COPY:
            {
              if(not need(1)) { return false; }
              const long n = static_cast<long>(pop().num);
              if(n < 0 or not need(static_cast<std::size_t>(n))) { return false; }
              if(stack.size() + n > max_stack) { return false; }

              const std::size_t base = stack.size() - static_cast<std::size_t>(n);
              for(long k = 0; k < n; k++)
                {
                  stack.push_back(stack[base + static_cast<std::size_t>(k)]);
                }
            }
            break;
          case PS_INDEX:
            {
              if(not need(1)) { return false; }
              const long n = static_cast<long>(pop().num);
              if(n < 0 or not need(static_cast<std::size_t>(n) + 1)) { return false; }
              stack.push_back(stack[stack.size() - 1 - static_cast<std::size_t>(n)]);
            }
            break;
          case PS_ROLL:
            {
              if(not need(2)) { return false; }
              const long j = static_cast<long>(pop().num);
              const long n = static_cast<long>(pop().num);
              if(n < 0 or not need(static_cast<std::size_t>(n))) { return false; }
              if(n == 0) { break; }

              const std::size_t base = stack.size() - static_cast<std::size_t>(n);
              long shift = j % n;
              if(shift < 0) { shift += n; }

              std::rotate(stack.begin() + base,
                          stack.end() - shift,
                          stack.end());
            }
            break;

          case PS_IF:
          case PS_IFELSE:
            {
              // reached without the preceding procedure node(s)
              return false;
            }

          default:
            {
              return false;
            }
          }
      }

    return true;
  }

  inline double pdf_function::sample_value(std::size_t index, int output) const
  {
    const std::size_t bit_offset =
      (index * static_cast<std::size_t>(num_outputs_) + static_cast<std::size_t>(output)) *
      static_cast<std::size_t>(bits_per_sample_);

    uint64_t raw = 0;
    for(int bit = 0; bit < bits_per_sample_; bit++)
      {
        const std::size_t pos = bit_offset + static_cast<std::size_t>(bit);
        const std::size_t byte = pos >> 3;

        if(byte >= samples_.size())
          {
            break;
          }

        const int shift = 7 - static_cast<int>(pos & 7);
        raw = (raw << 1) | static_cast<uint64_t>((samples_[byte] >> shift) & 1);
      }

    const double max_raw = std::pow(2.0, bits_per_sample_) - 1.0;

    return interpolate(static_cast<double>(raw), 0.0, max_raw,
                       decode_[2 * static_cast<std::size_t>(output) + 0],
                       decode_[2 * static_cast<std::size_t>(output) + 1]);
  }

  inline bool pdf_function::evaluate_sampled(const std::vector<double>& x,
                                             std::vector<double>& out) const
  {
    // 7.10.2: per input, encode into sample-index space and clip to the table,
    // then interpolate multilinearly between the surrounding samples.
    const std::size_t m = size_.size();

    std::vector<std::size_t> lower(m, 0);
    std::vector<double>      frac(m, 0.0);

    for(std::size_t j = 0; j < m; j++)
      {
        double e = interpolate(x[j], domain_[2 * j + 0], domain_[2 * j + 1],
                               encode_[2 * j + 0], encode_[2 * j + 1]);
        e = std::min(static_cast<double>(size_[j] - 1), std::max(0.0, e));

        lower[j] = static_cast<std::size_t>(std::floor(e));
        frac[j] = e - static_cast<double>(lower[j]);
      }

    out.assign(static_cast<std::size_t>(num_outputs_), 0.0);

    const std::size_t corners = std::size_t(1) << m;
    for(std::size_t corner = 0; corner < corners; corner++)
      {
        double weight = 1.0;
        std::size_t index = 0;
        std::size_t stride = 1;

        for(std::size_t j = 0; j < m; j++)
          {
            const bool upper = ((corner >> j) & 1) != 0;

            weight *= upper ? frac[j] : (1.0 - frac[j]);
            if(weight == 0.0)
              {
                break;
              }

            const std::size_t sample =
              upper ? std::min(lower[j] + 1,
                               static_cast<std::size_t>(size_[j] - 1))
                    : lower[j];

            index += sample * stride;
            stride *= static_cast<std::size_t>(size_[j]);
          }

        if(weight == 0.0)
          {
            continue;
          }

        for(int j = 0; j < num_outputs_; j++)
          {
            out[static_cast<std::size_t>(j)] +=
              weight * sample_value(index, j);
          }
      }

    return true;
  }

  inline bool pdf_function::evaluate_exponential(double x, std::vector<double>& out) const
  {
    const double factor = (exponent_ == 1.0) ? x : std::pow(x, exponent_);

    out.resize(c0_.size());
    for(std::size_t j = 0; j < c0_.size(); j++)
      {
        out[j] = c0_[j] + factor * (c1_[j] - c0_[j]);
      }

    return true;
  }

  inline bool pdf_function::evaluate_stitching(double x, std::vector<double>& out) const
  {
    // 7.10.4: find the subdomain holding x, then map x into the sub-function's
    // encoded range.
    const std::size_t k = functions_.size();

    std::size_t i = 0;
    while(i < bounds_.size() and x >= bounds_[i])
      {
        i++;
      }
    i = std::min(i, k - 1);

    const double low  = (i == 0)     ? domain_[0] : bounds_[i - 1];
    const double high = (i == k - 1) ? domain_[1] : bounds_[i];

    const double encoded = interpolate(x, low, high,
                                       encode_[2 * i + 0], encode_[2 * i + 1]);

    return functions_[i]->evaluate(encoded, out);
  }

  inline bool pdf_function::evaluate_postscript(const std::vector<double>& x,
                                                std::vector<double>& out) const
  {
    const std::size_t n = range_.size() / 2;

    // 7.10.5: the m inputs are on the stack when the program starts, the
    // first one bottom-most
    std::vector<ps_value> stack;
    stack.reserve(x.size());
    for(double value : x)
      {
        stack.push_back(ps_value{value, false});
      }

    if(not run_postscript(program_, stack))
      {
        return false;
      }

    if(stack.size() < n)
      {
        return false;
      }

    // the n outputs are the topmost n stack entries, bottom-most first
    out.resize(n);
    for(std::size_t j = 0; j < n; j++)
      {
        out[j] = stack[stack.size() - n + j].num;
      }

    return true;
  }

  inline void pdf_function::clip_to_range(std::vector<double>& out) const
  {
    if(range_.empty())
      {
        return;
      }

    const std::size_t n = std::min(out.size(), range_.size() / 2);
    for(std::size_t j = 0; j < n; j++)
      {
        out[j] = std::min(range_[2 * j + 1], std::max(range_[2 * j + 0], out[j]));
      }
  }

  inline bool pdf_function::evaluate(double in, std::vector<double>& out) const
  {
    return evaluate(std::vector<double>{in}, out);
  }

  inline bool pdf_function::evaluate(const std::vector<double>& in,
                                     std::vector<double>& out) const
  {
    if(not valid_)
      {
        return false;
      }

    const std::size_t m = domain_.size() / 2;

    if(in.size() != m)
      {
        return false;
      }

    std::vector<double> x(m, 0.0);
    for(std::size_t j = 0; j < m; j++)
      {
        x[j] = std::min(domain_[2 * j + 1],
                        std::max(domain_[2 * j + 0], in[j]));
      }

    bool ok = false;
    switch(type_)
      {
      // types 2 and 3 are single-input by definition (7.10.3, 7.10.4)
      case 0: { ok = evaluate_sampled(x, out);        } break;
      case 2: { ok = evaluate_exponential(x[0], out); } break;
      case 3: { ok = evaluate_stitching(x[0], out);   } break;
      case 4: { ok = evaluate_postscript(x, out);     } break;

      default: { return false; }
      }

    if(not ok or out.empty())
      {
        return false;
      }

    clip_to_range(out);

    return true;
  }

}

#endif
