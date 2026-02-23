//-*-C++-*-

#ifndef QPDF_STREAM_DECODER_H
#define QPDF_STREAM_DECODER_H

#include <exception>

namespace pdflib
{

  class qpdf_stream_decoder:
    public QPDFObjectHandle::ParserCallbacks
  {
  public:

    qpdf_stream_decoder(std::vector<qpdf_instruction>& stream_);
    ~qpdf_stream_decoder();

    void print();

    void decode(QPDFObjectHandle& content);

    // methods used by the QPDFObjectHandle::ParserCallbacks
    ///*virtual*/ void handleObject(QPDFObjectHandle obj) override;

    void handleObject(QPDFObjectHandle obj, size_t offset, size_t len) override;
    
    void contentSize(size_t len) override;

    void handleEOF() override;
    
  private:

    std::vector<qpdf_instruction>& stream;

    std::regex value_pattern_0;
    std::regex value_pattern_1;
    std::regex value_pattern_2;
  };

  qpdf_stream_decoder::qpdf_stream_decoder(std::vector<qpdf_instruction>& stream_):
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
      }

    LOG_S(WARNING) << "finished decoding content-stream!";
  }

  /*
  void qpdf_stream_decoder::handleObject(QPDFObjectHandle obj)
  {
    LOG_S(INFO) << __FUNCTION__;

    qpdf_instruction row;
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

    qpdf_instruction row;
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

    std::smatch match;
    
    // if the row is null, reinterprete it as an empty array. We encountered
    // this usecase for a parameter of the d operator (see Table 56) that is
    // null but in reality should be an empty array.
    if(row.is_null()) 
      {
	row.key = "parameter";
	row.val = "[]";
      }
    else if (std::regex_match(row.val, match, value_pattern_0))
      {
	std::string mvalue = match[1].str();	
	LOG_S(WARNING) << "match-1: " << std::setw(12) << row.key << " | " << row.val << " => new matched value: " << mvalue;

	double value = std::stod(mvalue);	
	
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
	
	double value = std::stod(mvalue);
	
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
	
	double value = std::stod(mvalue);
	
	// Creating a real (floating-point) QPDFObjectHandle
	QPDFObjectHandle new_obj = QPDFObjectHandle::newReal(value);

	row.key = new_obj.getTypeName();
	row.val = new_obj.unparse();
	row.obj = new_obj;
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
