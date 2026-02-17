//-*-C++-*-

#ifndef QPDF_COMPAT_H
#define QPDF_COMPAT_H

#include <memory>

// qpdf 10.x uses PointerHolder<T> for stream APIs; qpdf 11+ uses
// std::shared_ptr<T>.  GCC 13+ refuses the implicit conversion from
// PointerHolder to shared_ptr.  This helper bridges the gap so that
// callers can write:
//
//   auto sp = to_shared_ptr(obj.getRawStreamData());
//
// and get a std::shared_ptr<T> regardless of qpdf version.

// Passthrough for qpdf 11+ which already returns std::shared_ptr<T>.
template<typename T>
std::shared_ptr<T> to_shared_ptr(std::shared_ptr<T> sp) { return sp; }

// Overload for qpdf 10.x which returns PointerHolder<T>.
// qpdf 11+ defines POINTERHOLDER_IS_SHARED_POINTER, meaning
// PointerHolder<T> (if it exists) is already derived from
// std::shared_ptr<T> and the passthrough overload above suffices.
// qpdf 10.x does NOT define that macro, and its PointerHolder<T> is
// an independent smart pointer that GCC 13+ refuses to implicitly
// convert to std::shared_ptr<T>.
#if __has_include(<qpdf/PointerHolder.hh>)
#include <qpdf/PointerHolder.hh>

#if !defined(POINTERHOLDER_IS_SHARED_POINTER)
template<typename T>
std::shared_ptr<T> to_shared_ptr(PointerHolder<T> ph)
{
  T* raw = ph.getPointer();
  if(!raw)
    return nullptr;
  // prevent_delete captured by value keeps PointerHolder alive,
  // preventing premature deallocation while shared_ptr exists
  return std::shared_ptr<T>(raw, [prevent_delete = std::move(ph)](T*) {});
}
#endif // !POINTERHOLDER_IS_SHARED_POINTER
#endif // __has_include

#endif // QPDF_COMPAT_H
