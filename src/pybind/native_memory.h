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
    malloc_printf("total: %zu, used: %zu, free: %zu\n",
                  ms.bytes_total,
                  ms.bytes_used,
                  ms.bytes_free);
  }

  inline void release_native_memory()
  {
    std::cout << "heap before: \n";
    heapStats();
      
    std::cout << "release_native_memory on ";

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
