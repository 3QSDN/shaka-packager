// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/base/aes_cryptor.h"

#include <openssl/aes.h>

#include "packager/base/logging.h"
#include "packager/base/stl_util.h"

namespace edash_packager {
namespace media {

AesCryptor::AesCryptor() : aes_key_(new AES_KEY) {}
AesCryptor::~AesCryptor() {}

bool AesCryptor::Crypt(const std::vector<uint8_t>& text,
                       std::vector<uint8_t>* crypt_text) {
  // Save text size to make it work for in-place conversion, since the
  // next statement will update the text size.
  const size_t text_size = text.size();
  crypt_text->resize(text_size + NumPaddingBytes(text_size));
  size_t crypt_text_size = crypt_text->size();
  if (!CryptInternal(text.data(), text_size, crypt_text->data(),
                     &crypt_text_size)) {
    return false;
  }
  DCHECK_LE(crypt_text_size, crypt_text->size());
  crypt_text->resize(crypt_text_size);
  return true;
}

bool AesCryptor::Crypt(const std::string& text, std::string* crypt_text) {
  // Save text size to make it work for in-place conversion, since the
  // next statement will update the text size.
  const size_t text_size = text.size();
  crypt_text->resize(text_size + NumPaddingBytes(text_size));
  size_t crypt_text_size = crypt_text->size();
  if (!CryptInternal(reinterpret_cast<const uint8_t*>(text.data()), text_size,
                     reinterpret_cast<uint8_t*>(string_as_array(crypt_text)),
                     &crypt_text_size))
    return false;
  DCHECK_LE(crypt_text_size, crypt_text->size());
  crypt_text->resize(crypt_text_size);
  return true;
}

size_t AesCryptor::NumPaddingBytes(size_t size) const {
  // No padding by default.
  return 0;
}

}  // namespace media
}  // namespace edash_packager


