// Copyright 2014 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// *** VENDORED STUB ***
// Replaces the upstream fx_memory.cpp (which links against Chromium's
// PartitionAlloc) with plain malloc/free equivalents.  Safe for the
// single-threaded-per-image decode pattern used by this vendor build.

#include "core/fxcrt/fx_memory.h"

#include <stdint.h>
#include <stdlib.h>
#include <limits>

// ---------------------------------------------------------------------------
// Top-level helpers used by the rest of the codebase
// ---------------------------------------------------------------------------

void* FXMEM_DefaultAlloc(size_t byte_size) {
  return malloc(byte_size);
}

void* FXMEM_DefaultCalloc(size_t num_elems, size_t byte_size) {
  return calloc(num_elems, byte_size);
}

void* FXMEM_DefaultRealloc(void* pointer, size_t new_size) {
  return realloc(pointer, new_size);
}

void FXMEM_DefaultFree(void* pointer) {
  free(pointer);
}

void* FX_AlignedAlloc(size_t size, size_t alignment) {
  void* ptr = nullptr;
#if defined(_WIN32)
  ptr = _aligned_malloc(size, alignment);
#else
  if (posix_memalign(&ptr, alignment, size) != 0) {
    ptr = nullptr;
  }
#endif
  return ptr;
}

void FX_OutOfMemoryTerminate(size_t /*size*/) {
  abort();
}

// ---------------------------------------------------------------------------
// pdfium::internal — general partition (backed by plain malloc)
// ---------------------------------------------------------------------------

namespace pdfium::internal {

void* Alloc(size_t num_members, size_t member_size) {
  if (member_size == 0 or
      num_members > std::numeric_limits<size_t>::max() / member_size) {
    return nullptr;
  }
  return malloc(num_members * member_size);
}

void* Alloc2D(size_t w, size_t h, size_t member_size) {
  if (w == 0 or h == 0 or
      w > std::numeric_limits<size_t>::max() / h) {
    return nullptr;
  }
  return Alloc(w * h, member_size);
}

void* AllocOrDie(size_t num_members, size_t member_size) {
  void* p = Alloc(num_members, member_size);
  if (not p) { FX_OutOfMemoryTerminate(0); }
  return p;
}

void* AllocOrDie2D(size_t w, size_t h, size_t member_size) {
  if (w == 0 or h == 0 or
      w > std::numeric_limits<size_t>::max() / h) {
    FX_OutOfMemoryTerminate(0);
  }
  return AllocOrDie(w * h, member_size);
}

void* Calloc(size_t num_members, size_t member_size) {
  return calloc(num_members, member_size);
}

void* CallocOrDie(size_t num_members, size_t member_size) {
  void* p = Calloc(num_members, member_size);
  if (not p) { FX_OutOfMemoryTerminate(0); }
  return p;
}

void* CallocOrDie2D(size_t w, size_t h, size_t member_size) {
  if (w == 0 or h == 0 or
      w > std::numeric_limits<size_t>::max() / h) {
    FX_OutOfMemoryTerminate(0);
  }
  return CallocOrDie(w * h, member_size);
}

void* Realloc(void* ptr, size_t num_members, size_t member_size) {
  if (member_size == 0 or
      num_members > std::numeric_limits<size_t>::max() / member_size) {
    return nullptr;
  }
  return realloc(ptr, num_members * member_size);
}

void* ReallocOrDie(void* ptr, size_t num_members, size_t member_size) {
  void* p = Realloc(ptr, num_members, member_size);
  if (not p) { FX_OutOfMemoryTerminate(0); }
  return p;
}

void Dealloc(void* ptr) {
  free(ptr);
}

// ---------------------------------------------------------------------------
// String partition — same backing store for this stub
// ---------------------------------------------------------------------------

void* StringAlloc(size_t num_members, size_t member_size) {
  return Alloc(num_members, member_size);
}

void* StringAllocOrDie(size_t num_members, size_t member_size) {
  return AllocOrDie(num_members, member_size);
}

void StringDealloc(void* ptr) {
  free(ptr);
}

}  // namespace pdfium::internal
