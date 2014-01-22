// Copyright (c) 2013 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Defines command line flags for fixed key encryption.

#ifndef APP_FIXED_KEY_ENCRYPTION_FLAGS_H_
#define APP_FIXED_KEY_ENCRYPTION_FLAGS_H_

#include <gflags/gflags.h>

DEFINE_bool(enable_fixed_key_encryption,
            false,
            "Enable encryption with fixed key.");
DEFINE_string(key_id, "", "Key id in hex string format.");
DEFINE_string(key, "", "Key in hex string format.");
DEFINE_string(pssh, "", "PSSH in hex string format.");

static bool IsNotEmptyWithFixedKeyEncryption(const char* flag_name,
                                             const std::string& flag_value) {
  return FLAGS_enable_fixed_key_encryption ? !flag_value.empty() : true;
}

static bool dummy_key_id_validator =
    google::RegisterFlagValidator(&FLAGS_key_id,
                                  &IsNotEmptyWithFixedKeyEncryption);
static bool dummy_key_validator =
    google::RegisterFlagValidator(&FLAGS_key,
                                  &IsNotEmptyWithFixedKeyEncryption);
static bool dummy_pssh_validator =
    google::RegisterFlagValidator(&FLAGS_pssh,
                                  &IsNotEmptyWithFixedKeyEncryption);

#endif  // APP_FIXED_KEY_ENCRYPTION_FLAGS_H_
