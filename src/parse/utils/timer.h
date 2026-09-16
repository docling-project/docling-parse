//-*-C++-*-

#ifndef PDF_UTILS_TIMER_H
#define PDF_UTILS_TIMER_H

#include <chrono>
#include <string>

namespace pdflib
{
  // defined in parse/utils/pdf_timings.h, which is included after this header
  class pdf_timings;
}

namespace utils
{
  enum time_unit {NANO_SEC, MICRO_SEC, MILLI_SEC, SEC};

  // Adds the elapsed time to a pdf_timings entry when it leaves scope, so a
  // function with several early returns is still accounted exactly once.
  // Declared here and defined below pdf_timings, which timer.h precedes.
  class scoped_timer
  {
  public:

    scoped_timer(pdflib::pdf_timings& timings, const std::string& key);
    ~scoped_timer();

    scoped_timer(const scoped_timer&) = delete;
    scoped_timer& operator=(const scoped_timer&) = delete;

  private:

    pdflib::pdf_timings& timings_;
    std::string key_;
    std::chrono::steady_clock::time_point beg_;
  };

  class timer
  {

  public:

    timer();
    ~timer();

    void reset();

    double get_time(time_unit unit=SEC);

  private:

    std::chrono::steady_clock::time_point beg;
  };

  timer::timer():
    beg(std::chrono::steady_clock::now())    
  {}
  
  timer::~timer()
  {}
  
  void timer::reset()  
  {
    beg = std::chrono::steady_clock::now();
  }

  double timer::get_time(time_unit unit)
  {
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    
    switch(unit)
      {
      case NANO_SEC:
	{
	  return std::chrono::duration_cast<std::chrono::nanoseconds>(end - beg).count();
	}
	break;
	
      case MICRO_SEC:
	{
	  return std::chrono::duration_cast<std::chrono::microseconds>(end - beg).count();
	}
	break;
	
      case MILLI_SEC:
	{
	  return std::chrono::duration_cast<std::chrono::microseconds>(end - beg).count()/1000.0;
	}
	break;
	
      case SEC:
	{
	  return std::chrono::duration_cast<std::chrono::microseconds>(end - beg).count()/1000000.0;
	}
	break;
	
      default:
	{
	  LOG_S(ERROR) << "could not the right time-unit type";
	  return 0.0;
	}
      }	
  }
  
}

#endif
