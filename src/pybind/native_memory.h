//-*-C++-*-

#ifndef PYBIND_NATIVE_MEMORY_H
#define PYBIND_NATIVE_MEMORY_H

#if defined(__GLIBC__)
#include <malloc.h>
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace docling
{
  /*
  void heapStats()
  {
    struct mstats ms = mstats();
    std::cout << "total: " << ms.bytes_total
              << ", used: " << ms.bytes_used
              << ", free: " << ms.bytes_free << '\n';
  }
  */
  
  inline void release_native_memory(int processed_pages=0)
  {
#if defined(__GLIBC__)
    malloc_trim(0);
#elif defined(__APPLE__)
    malloc_zone_pressure_relief(nullptr, 0);
#elif defined(_WIN32)
    HeapCompact(GetProcessHeap(), 0);
#endif
  }

}

#endif
