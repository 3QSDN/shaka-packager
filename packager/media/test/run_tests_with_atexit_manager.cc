// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gtest/gtest.h>

#include "packager/base/at_exit.h"
#include "packager/base/command_line.h"
#include "packager/base/logging.h"

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  // Needed to enable VLOG/DVLOG through --vmodule or --v.
  CommandLine::Init(argc, argv);
  CHECK(logging::InitLogging(logging::LoggingSettings()));

  base::AtExitManager exit;
  return RUN_ALL_TESTS();
}
