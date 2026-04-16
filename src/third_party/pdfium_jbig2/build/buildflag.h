// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Minimal stub replacing the GN-generated build/buildflag.h.
// Only the BUILDFLAG() dispatch macro is needed by vendored PDFium code.

#ifndef BUILD_BUILDFLAG_H_
#define BUILD_BUILDFLAG_H_

#define BUILDFLAG(flag) (BUILDFLAG_INTERNAL_##flag())

#endif  // BUILD_BUILDFLAG_H_
