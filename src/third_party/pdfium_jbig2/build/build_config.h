// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Minimal stub replacing the GN-generated build/build_config.h.
// Defines the platform, architecture, and compiler macros consumed by
// the vendored PDFium JBIG2 sources.

#ifndef BUILD_BUILD_CONFIG_H_
#define BUILD_BUILD_CONFIG_H_

#include "build/buildflag.h"

// ---------------------------------------------------------------------------
// Operating system
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
  #define BUILDFLAG_INTERNAL_IS_WIN()   (1)
  #define BUILDFLAG_INTERNAL_IS_APPLE() (0)
  #define BUILDFLAG_INTERNAL_IS_MAC()   (0)
  #define BUILDFLAG_INTERNAL_IS_IOS()   (0)
  #define BUILDFLAG_INTERNAL_IS_LINUX() (0)
  #define BUILDFLAG_INTERNAL_IS_ANDROID()(0)
#elif defined(__APPLE__)
  #include <TargetConditionals.h>
  #define BUILDFLAG_INTERNAL_IS_WIN()   (0)
  #define BUILDFLAG_INTERNAL_IS_APPLE() (1)
  #if TARGET_OS_IPHONE
    #define BUILDFLAG_INTERNAL_IS_MAC() (0)
    #define BUILDFLAG_INTERNAL_IS_IOS() (1)
  #else
    #define BUILDFLAG_INTERNAL_IS_MAC() (1)
    #define BUILDFLAG_INTERNAL_IS_IOS() (0)
  #endif
  #define BUILDFLAG_INTERNAL_IS_LINUX() (0)
  #define BUILDFLAG_INTERNAL_IS_ANDROID()(0)
#elif defined(__ANDROID__)
  #define BUILDFLAG_INTERNAL_IS_WIN()   (0)
  #define BUILDFLAG_INTERNAL_IS_APPLE() (0)
  #define BUILDFLAG_INTERNAL_IS_MAC()   (0)
  #define BUILDFLAG_INTERNAL_IS_IOS()   (0)
  #define BUILDFLAG_INTERNAL_IS_LINUX() (0)
  #define BUILDFLAG_INTERNAL_IS_ANDROID()(1)
#elif defined(__linux__)
  #define BUILDFLAG_INTERNAL_IS_WIN()   (0)
  #define BUILDFLAG_INTERNAL_IS_APPLE() (0)
  #define BUILDFLAG_INTERNAL_IS_MAC()   (0)
  #define BUILDFLAG_INTERNAL_IS_IOS()   (0)
  #define BUILDFLAG_INTERNAL_IS_LINUX() (1)
  #define BUILDFLAG_INTERNAL_IS_ANDROID()(0)
#else
  #define BUILDFLAG_INTERNAL_IS_WIN()   (0)
  #define BUILDFLAG_INTERNAL_IS_APPLE() (0)
  #define BUILDFLAG_INTERNAL_IS_MAC()   (0)
  #define BUILDFLAG_INTERNAL_IS_IOS()   (0)
  #define BUILDFLAG_INTERNAL_IS_LINUX() (0)
  #define BUILDFLAG_INTERNAL_IS_ANDROID()(0)
#endif

// ---------------------------------------------------------------------------
// CPU architecture
// ---------------------------------------------------------------------------
#if defined(__x86_64__) || defined(_M_X64)
  #define ARCH_CPU_X86_FAMILY  1
  #define ARCH_CPU_64_BITS     1
  #define ARCH_CPU_LITTLE_ENDIAN 1
#elif defined(__i386__) || defined(_M_IX86)
  #define ARCH_CPU_X86_FAMILY  1
  #define ARCH_CPU_32_BITS     1
  #define ARCH_CPU_LITTLE_ENDIAN 1
#elif defined(__aarch64__) || defined(_M_ARM64)
  #define ARCH_CPU_ARM64       1
  #define ARCH_CPU_64_BITS     1
  #define ARCH_CPU_LITTLE_ENDIAN 1
#elif defined(__arm__)
  #define ARCH_CPU_ARMEL       1
  #define ARCH_CPU_32_BITS     1
  #define ARCH_CPU_LITTLE_ENDIAN 1
#endif

// ---------------------------------------------------------------------------
// Compiler
// ---------------------------------------------------------------------------
#if defined(__clang__) || defined(__GNUC__)
  #define COMPILER_GCC 1
#elif defined(_MSC_VER)
  #define COMPILER_MSVC 1
#endif

#endif  // BUILD_BUILD_CONFIG_H_
