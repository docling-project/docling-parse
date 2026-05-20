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
  inline void release_native_memory()
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
