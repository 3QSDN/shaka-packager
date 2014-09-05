// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "media/base/key_source.h"

#include "base/strings/string_number_conversions.h"
#include "media/base/aes_encryptor.h"
#include "media/base/buffer_writer.h"

namespace {
const uint8 kWidevineSystemId[] = {0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6,
                                   0x4a, 0xce, 0xa3, 0xc8, 0x27, 0xdc,
                                   0xd5, 0x1d, 0x21, 0xed};
}  // namespace

namespace media {

EncryptionKey::EncryptionKey() {}
EncryptionKey::~EncryptionKey() {}

KeySource::~KeySource() {}

Status KeySource::FetchKeys(const std::vector<uint8>& content_id,
                            const std::string& policy) {
  // Do nothing for fixed key decryption.
  return Status::OK;
}

Status KeySource::FetchKeys(const std::vector<uint8>& pssh_data) {
  // Do nothing for fixed key decryption.
  return Status::OK;
}

Status KeySource::GetKey(TrackType track_type, EncryptionKey* key) {
  DCHECK(key);
  DCHECK(encryption_key_);
  *key = *encryption_key_;
  return Status::OK;
}

Status KeySource::GetKey(const std::vector<uint8>& key_id,
                         EncryptionKey* key) {
  DCHECK(key);
  DCHECK(encryption_key_);
  if (key_id != encryption_key_->key_id) {
    return Status(error::NOT_FOUND, std::string("Key for key ID ") +
                  base::HexEncode(&key_id[0], key_id.size()) +
                  " was not found.");
  }
  *key = *encryption_key_;
  return Status::OK;
}

Status KeySource::GetCryptoPeriodKey(uint32 crypto_period_index,
                                     TrackType track_type,
                                     EncryptionKey* key) {
  NOTIMPLEMENTED();
  return Status(error::UNIMPLEMENTED, "");
}

scoped_ptr<KeySource> KeySource::CreateFromHexStrings(
    const std::string& key_id_hex,
    const std::string& key_hex,
    const std::string& pssh_data_hex,
    const std::string& iv_hex) {
  scoped_ptr<EncryptionKey> encryption_key(new EncryptionKey());

  if (!base::HexStringToBytes(key_id_hex, &encryption_key->key_id)) {
    LOG(ERROR) << "Cannot parse key_id_hex " << key_id_hex;
    return scoped_ptr<KeySource>();
  }

  if (!base::HexStringToBytes(key_hex, &encryption_key->key)) {
    LOG(ERROR) << "Cannot parse key_hex " << key_hex;
    return scoped_ptr<KeySource>();
  }

  std::vector<uint8> pssh_data;
  if (!base::HexStringToBytes(pssh_data_hex, &pssh_data)) {
    LOG(ERROR) << "Cannot parse pssh_hex " << pssh_data_hex;
    return scoped_ptr<KeySource>();
  }

  if (!iv_hex.empty()) {
    if (!base::HexStringToBytes(iv_hex, &encryption_key->iv)) {
      LOG(ERROR) << "Cannot parse iv_hex " << iv_hex;
      return scoped_ptr<KeySource>();
    }
  }

  encryption_key->pssh = PsshBoxFromPsshData(pssh_data);
  return scoped_ptr<KeySource>(
      new KeySource(encryption_key.Pass()));
}

KeySource::TrackType KeySource::GetTrackTypeFromString(
    const std::string& track_type_string) {
  if (track_type_string == "SD")
    return TRACK_TYPE_SD;
  if (track_type_string == "HD")
    return TRACK_TYPE_HD;
  if (track_type_string == "AUDIO")
    return TRACK_TYPE_AUDIO;
  LOG(WARNING) << "Unexpected track type: " << track_type_string;
  return TRACK_TYPE_UNKNOWN;
}

std::string KeySource::TrackTypeToString(TrackType track_type) {
  switch (track_type) {
    case TRACK_TYPE_SD:
      return "SD";
    case TRACK_TYPE_HD:
      return "HD";
    case TRACK_TYPE_AUDIO:
      return "AUDIO";
    default:
      NOTIMPLEMENTED() << "Unknown track type: " << track_type;
      return "UNKNOWN";
  }
}

std::vector<uint8> KeySource::PsshBoxFromPsshData(
    const std::vector<uint8>& pssh_data) {
  const uint8 kPsshFourCC[] = {'p', 's', 's', 'h'};
  const uint32 kVersionAndFlags = 0;

  const uint32 pssh_data_size = pssh_data.size();
  const uint32 total_size =
      sizeof(total_size) + sizeof(kPsshFourCC) + sizeof(kVersionAndFlags) +
      sizeof(kWidevineSystemId) + sizeof(pssh_data_size) + pssh_data_size;

  BufferWriter writer;
  writer.AppendInt(total_size);
  writer.AppendArray(kPsshFourCC, sizeof(kPsshFourCC));
  writer.AppendInt(kVersionAndFlags);
  writer.AppendArray(kWidevineSystemId, sizeof(kWidevineSystemId));
  writer.AppendInt(pssh_data_size);
  writer.AppendVector(pssh_data);
  return std::vector<uint8>(writer.Buffer(), writer.Buffer() + writer.Size());
}

KeySource::KeySource() {}
KeySource::KeySource(scoped_ptr<EncryptionKey> encryption_key)
    : encryption_key_(encryption_key.Pass()) {
  DCHECK(encryption_key_);
}

}  // namespace media
