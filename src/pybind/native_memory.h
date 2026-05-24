//-*-C++-*-

#ifndef PYBIND_NATIVE_MEMORY_H
#define PYBIND_NATIVE_MEMORY_H

#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/task.h>
#include <malloc/malloc.h>
#include <errno.h>


#if defined(__GLIBC__)
#include <malloc.h>
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace docling
{
  /** heapStats()
   * print the output from the mstats() function: total heap bytes,
   * used bytes, and free bytes.
   */
  void heapStats()
  {
    struct mstats ms = mstats();
    std::cout << "total: " << ms.bytes_total
              << ", used: " << ms.bytes_used
              << ", free: " << ms.bytes_free << '\n';
  }

  inline void release_native_memory(int processed_pages)
  {
    std::cout << "heap before: \n";
    heapStats();
      
    std::cout << "release_native_memory after " << processed_pages
              << " processed pages on ";

#if defined(__GLIBC__)
    std::cout << "linux\n";
    malloc_trim(0);
#elif defined(__APPLE__)
    std::cout << "apple\n";
    malloc_zone_pressure_relief(nullptr, 0);
#elif defined(_WIN32)
    std::cout << "windows\n";
    HeapCompact(GetProcessHeap(), 0);
#endif

    std::cout << "heap after: \n";
    heapStats();    
  }

}

#endif
