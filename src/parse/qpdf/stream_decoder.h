//-*-C++-*-

#ifndef QPDF_STREAM_DECODER_H
#define QPDF_STREAM_DECODER_H

#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include <zlib.h>

#include <parse/qpdf/qpdf_compat.h>

namespace pdflib
{

  class qpdf_stream_decoder:
    public QPDFObjectHandle::ParserCallbacks
  {
  public:

    qpdf_stream_decoder(std::vector<qpdf_stream_instruction>& stream_);
    ~qpdf_stream_decoder();

    void print();

    void decode(QPDFObjectHandle& content);

    // methods used by the QPDFObjectHandle::ParserCallbacks
    ///*virtual*/ void handleObject(QPDFObjectHandle obj) override;

    void handleObject(QPDFObjectHandle obj, size_t offset, size_t len) override;
    
    void contentSize(size_t len) override;

    void handleEOF() override;
    
  private:

    void        decode_damaged_stream(QPDFObjectHandle& content);
    std::string inflate_prefix(QPDFObjectHandle& content);

    std::vector<qpdf_stream_instruction>& stream;

    std::regex value_pattern_0;
    std::regex value_pattern_1;
    std::regex value_pattern_2;
  };

  qpdf_stream_decoder::qpdf_stream_decoder(std::vector<qpdf_stream_instruction>& stream_):
    QPDFObjectHandle::ParserCallbacks(),
    stream(stream_),

    value_pattern_0(R"(^(\d\.\d+)(\-\d+)$)"),
    value_pattern_1(R"(^((\-)?\d+\.\d*)(\-)(\d*)$)"), // need to deal with numbers that have random `-` in the middle 
    value_pattern_2(R"(^(\-)+((\-)\d+(\.)?(\d*))(\-)?(\d*)$)") // need to deal with numbers that have multiple `-` as start 
  {}

  qpdf_stream_decoder::~qpdf_stream_decoder()
  {}

  void qpdf_stream_decoder::print()
  {
    for(auto row:stream)
      {
        LOG_S(INFO) << std::setw(12) << row.key << " | " << row.val;
      }
  }

  void qpdf_stream_decoder::decode(QPDFObjectHandle& content)
  {
    LOG_S(INFO) << "start decoding content-stream: " << content.getTypeName() << " -> " << content.unparse();

    stream.clear();

    try
      {
        QPDFObjectHandle::parseContentStream(content, this);
      }
    catch(std::exception& e)
      {
        LOG_S(ERROR) << "QPDF encountered error (" << e.what() << ") during decoding";

        // QPDF runs the whole stream through its filters before it hands
        // the first token to the callbacks, so a zlib error part-way
        // through a damaged stream leaves nothing at all, although every
        // operator in front of the error inflated fine. A tokenizer error
        // instead arrives after the tokens before it, and those are kept.
        if(stream.empty())
          {
            decode_damaged_stream(content);
          }
      }

    LOG_S(WARNING) << "finished decoding content-stream!";
  }

  // Keep what inflated before the damage: the stream is repaired in place
  // with those bytes (unfiltered) and tokenized again. QPDF itself only
  // tolerates the end-of-stream checksum mismatch (Pl_Flate), not an
  // error in the middle of the data; other readers keep the prefix.
  void qpdf_stream_decoder::decode_damaged_stream(QPDFObjectHandle& content)
  {
    if(not content.isStream())
      {
        return;
      }

    std::string prefix = inflate_prefix(content);

    // The data stops at an arbitrary byte, possibly inside a token: cut
    // back to the last whitespace so the tokenizer sees whole tokens only.
    std::string::size_type cut = prefix.find_last_of(std::string(" \t\r\n\f\0", 6));
    if(cut==std::string::npos)
      {
        return;
      }
    prefix.erase(cut);

    if(prefix.empty())
      {
        return;
      }

    LOG_S(WARNING) << "damaged content stream: keeping " << prefix.size()
                   << " bytes decoded in front of the error";

    try
      {
        content.replaceStreamData(prefix,
                                  QPDFObjectHandle::newNull(),
                                  QPDFObjectHandle::newNull());

        QPDFObjectHandle::parseContentStream(content, this);
      }
    catch(std::exception& e)
      {
        LOG_S(ERROR) << "QPDF encountered error (" << e.what()
                     << ") while decoding the recovered content";
      }
  }

  // The bytes zlib produces for a plain /FlateDecode stream up to the point
  // where it gives up. Empty for any other filter chain, for a predictor,
  // or when nothing came out.
  std::string qpdf_stream_decoder::inflate_prefix(QPDFObjectHandle& content)
  {
    QPDFObjectHandle dict = content.getDict();

    QPDFObjectHandle filter = dict.getKey("/Filter");
    if(filter.isArray() and filter.getArrayNItems()==1)
      {
        filter = filter.getArrayItem(0);
      }

    if(not filter.isName() or filter.getName()!="/FlateDecode")
      {
        LOG_S(WARNING) << "damaged content stream: no partial decode for filter "
                       << filter.unparse();
        return "";
      }

    QPDFObjectHandle parms = dict.getKey("/DecodeParms");
    if(parms.isArray() and parms.getArrayNItems()==1)
      {
        parms = parms.getArrayItem(0);
      }

    if(parms.isDictionary() and parms.hasKey("/Predictor") and
       parms.getKey("/Predictor").isInteger() and
       parms.getKey("/Predictor").getIntValue()>1)
      {
        LOG_S(WARNING) << "damaged content stream: no partial decode with a predictor";
        return "";
      }

    std::shared_ptr<Buffer> raw = to_shared_ptr(content.getRawStreamData());
    if(not raw or raw->getSize()==0)
      {
        return "";
      }

    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));

    if(inflateInit(&zs)!=Z_OK)
      {
        return "";
      }

    zs.next_in  = raw->getBuffer();
    zs.avail_in = static_cast<uInt>(raw->getSize());

    std::string out;
    std::vector<unsigned char> chunk(1<<16);

    int err = Z_OK;
    while(err==Z_OK)
      {
        zs.next_out  = chunk.data();
        zs.avail_out = static_cast<uInt>(chunk.size());

        err = inflate(&zs, Z_NO_FLUSH);

        // inflate has written what it decoded before an error too
        out.append(reinterpret_cast<const char*>(chunk.data()), chunk.size()-zs.avail_out);

        if(err==Z_OK and zs.avail_in==0 and zs.avail_out>0)
          {
            break; // the input ran out before the end-of-stream marker
          }
      }

    if(err!=Z_STREAM_END)
      {
        LOG_S(WARNING) << "damaged content stream: inflate stopped after "
                       << out.size() << " bytes (" << (zs.msg? zs.msg : zError(err)) << ")";
      }

    inflateEnd(&zs);

    return out;
  }

  /*
  void qpdf_stream_decoder::handleObject(QPDFObjectHandle obj)
  {
    LOG_S(INFO) << __FUNCTION__;

    qpdf_stream_instruction row;
    {
      row.key = obj.getTypeName();
      row.val = obj.unparse();
      row.obj = obj;

      //LOG_S(INFO) << std::setw(12) << row.key << " | " << row.val;
    }

    stream.push_back(row);
  }
  */

  void qpdf_stream_decoder::handleObject(QPDFObjectHandle obj, size_t offset, size_t len)
  {
    //LOG_S(INFO) << __FUNCTION__ << "\t offset: " << offset << ", len: " << len;

    qpdf_stream_instruction row;
    {
      row.key = obj.getTypeName();
      row.val = obj.unparse();
      row.obj = obj;

      // LOG_S(INFO) << std::setw(12) << row.key << " | " << row.val;
    }

    /*
    if(row.is_null() or row.is_array() or row.is_dict())
      {
	LOG_S(WARNING) << "null: " << row.is_null() << ", array: " << row.is_array() << ", dict: " << row.is_dict();
      }
    */

    // if the row is null, reinterprete it as an empty array. We encountered
    // this usecase for a parameter of the d operator (see Table 56) that is
    // null but in reality should be an empty array.
    if(row.is_null())
      {
	row.key = "parameter";
	row.val = "[]";
      }
    // All three regex patterns match malformed numbers containing '-' at
    // position > 0 (e.g. "1.23-45", "--123.4").  Skip the expensive regex
    // calls for the vast majority of tokens that don't have this.
    else if (row.val.find('-', 1) != std::string::npos)
      {
	std::smatch match;

	if (std::regex_match(row.val, match, value_pattern_0))
	  {
	    std::string mvalue = match[1].str();
	    LOG_S(WARNING) << "match-1: " << std::setw(12) << row.key << " | " << row.val << " => new matched value: " << mvalue;

	    double value = utils::numeric::locale_safe_stod(mvalue);

	    // Creating a real (floating-point) QPDFObjectHandle
	    QPDFObjectHandle new_obj = QPDFObjectHandle::newReal(value);

	    row.key = new_obj.getTypeName();
	    row.val = new_obj.unparse();
	    row.obj = new_obj;
	  }
	else if (std::regex_match(row.val, match, value_pattern_1))
	  {
	    std::string mvalue = match[1].str() + match[4].str();
	    LOG_S(WARNING) << "match-2: " << std::setw(12) << row.key << " | " << row.val << " => new matched value: " << mvalue;

	    double value = utils::numeric::locale_safe_stod(mvalue);

	    // Creating a real (floating-point) QPDFObjectHandle
	    QPDFObjectHandle new_obj = QPDFObjectHandle::newReal(value);

	    row.key = new_obj.getTypeName();
	    row.val = new_obj.unparse();
	    row.obj = new_obj;
	  }
	else if (std::regex_match(row.val, match, value_pattern_2))
	  {
	    std::string mvalue = match[3].str() + match[7].str();
	    LOG_S(WARNING) << "match-3: " << std::setw(12) << row.key << " | " << row.val << " => new matched value: " << mvalue;

	    double value = utils::numeric::locale_safe_stod(mvalue);

	    // Creating a real (floating-point) QPDFObjectHandle
	    QPDFObjectHandle new_obj = QPDFObjectHandle::newReal(value);

	    row.key = new_obj.getTypeName();
	    row.val = new_obj.unparse();
	    row.obj = new_obj;
	  }
      }
    
    stream.push_back(row);
  }

  void qpdf_stream_decoder::contentSize(size_t len)
  {
    //LOG_S(INFO) << __FUNCTION__ << ": " << len;
  }

  void qpdf_stream_decoder::handleEOF()
  {
    //LOG_S(INFO) << __FUNCTION__;
  }

}

#endif
