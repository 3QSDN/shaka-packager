// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_NETWORK_UTIL_H_
#define MEDIA_BASE_NETWORK_UTIL_H_

#include "base/base_export.h"
#include "base/basictypes.h"

namespace edash_packager {
namespace media {

uint32 ntohlFromBuffer(const unsigned char * buf);
uint16 ntohsFromBuffer(const unsigned char * buf);
uint64 ntohllFromBuffer(const unsigned char * buf);

}  // namespace media
}  // namespace edash_packager

#endif  // MEDIA_BASE_NETWORK_UTIL_H_
