// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/network_util.h"

namespace media {

uint32
ntohlFromBuffer(const unsigned char * buf) {
    return (static_cast<uint32>(buf[0])<<24) | (static_cast<uint32>(buf[1])<<16)
        | (static_cast<uint32>(buf[2])<<8) | (static_cast<uint32>(buf[3]));

}

uint16
ntohsFromBuffer( const unsigned char * buf) {
    return (static_cast<uint16>(buf[0])<<8) | (static_cast<uint16>(buf[1]));
}

uint64
ntohllFromBuffer( const unsigned char * buf ) {
    return  (static_cast<uint64>(buf[0])<<56)| (static_cast<uint64>(buf[1])<<48)
        | (static_cast<uint64>(buf[2])<<40)| (static_cast<uint64>(buf[3])<<32)
        | (static_cast<uint64>(buf[4])<<24)| (static_cast<uint64>(buf[5])<<16)
        | (static_cast<uint64>(buf[6])<<8) | (static_cast<uint64>(buf[7]));
}

}  // namespace media

