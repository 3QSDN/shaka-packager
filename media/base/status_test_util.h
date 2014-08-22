// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MEDIA_BASE_STATUS_TEST_UTIL_H_
#define MEDIA_BASE_STATUS_TEST_UTIL_H_

#include <gtest/gtest.h>

#include "media/base/status.h"

#define EXPECT_OK(val) EXPECT_EQ(media::Status::OK, (val))
#define ASSERT_OK(val) ASSERT_EQ(media::Status::OK, (val))

#endif  // MEDIA_BASE_STATUS_TEST_UTIL_H_
