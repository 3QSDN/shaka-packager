// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MEDIA_BASE_KEY_SOURCE_H_
#define MEDIA_BASE_KEY_SOURCE_H_

#include <vector>

#include "packager/base/memory/scoped_ptr.h"
#include "packager/media/base/status.h"

namespace edash_packager {
namespace media {

const uint8_t kWidevineSystemId[] = {0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6,
                                     0x4a, 0xce, 0xa3, 0xc8, 0x27, 0xdc,
                                     0xd5, 0x1d, 0x21, 0xed};

struct EncryptionKey {
  EncryptionKey();
  ~EncryptionKey();

  std::vector<uint8_t> key_id;
  std::vector<uint8_t> key;
  std::vector<uint8_t> pssh;
  std::vector<uint8_t> iv;
};

/// KeySource is responsible for encryption key acquisition.
class KeySource {
 public:
  enum TrackType {
    TRACK_TYPE_UNKNOWN = 0,
    TRACK_TYPE_SD = 1,
    TRACK_TYPE_HD = 2,
    TRACK_TYPE_AUDIO = 3,
    TRACK_TYPE_UNSPECIFIED = 4,
    NUM_VALID_TRACK_TYPES = 4
  };

  virtual ~KeySource();

  /// Fetch keys for CENC from the key server.
  /// @param content_id the unique id identify the content.
  /// @param policy specifies the DRM content rights.
  /// @return OK on success, an error status otherwise.
  virtual Status FetchKeys(const std::vector<uint8_t>& content_id,
                           const std::string& policy);

  /// Fetch keys for CENC from the key server.
  /// @param pssh_box The entire PSSH box for the content to be decrypted
  /// @return OK on success, an error status otherwise.
  virtual Status FetchKeys(const std::vector<uint8_t>& pssh_box);

  /// Fetch keys for CENC from the key server.
  /// @param key_ids the key IDs for the keys to fetch from the server.
  /// @return OK on success, an error status otherwise.
  virtual Status FetchKeys(const std::vector<std::vector<uint8_t>>& key_ids);

  /// Fetch keys for WVM decryption from the key server.
  /// @param asset_id is the Widevine Classic asset ID for the content to be
  /// decrypted.
  /// @return OK on success, an error status otherwise.
  virtual Status FetchKeys(uint32_t asset_id);

  /// Get encryption key of the specified track type.
  /// @param track_type is the type of track for which retrieving the key.
  /// @param key is a pointer to the EncryptionKey which will hold the retrieved
  ///        key. Owner retains ownership, and may not be NULL.
  /// @return OK on success, an error status otherwise.
  virtual Status GetKey(TrackType track_type, EncryptionKey* key);

  /// Get the encryption key specified by the CENC key ID.
  /// @param key_id is the unique identifier for the key being retreived.
  /// @param key is a pointer to the EncryptionKey which will hold the retrieved
  ///        key. Owner retains ownership, and may not be NULL.
  /// @return OK on success, or an error status otherwise.
  virtual Status GetKey(const std::vector<uint8_t>& key_id, EncryptionKey* key);

  /// Get encryption key of the specified track type at the specified index.
  /// @param crypto_period_index is the sequence number of the key rotation
  ///        period for which the key is being retrieved.
  /// @param track_type is the type of track for which retrieving the key.
  /// @param key is a pointer to the EncryptionKey which will hold the retrieved
  ///        key. Owner retains ownership, and may not be NULL.
  /// @return OK on success, an error status otherwise.
  virtual Status GetCryptoPeriodKey(uint32_t crypto_period_index,
                                    TrackType track_type,
                                    EncryptionKey* key);

  /// Returns the UUID of the key source in human readable form.
  /// UUIDs are listed here:
  /// http://dashif.org/identifiers/protection/
  /// @return UUID of the key source, empty string if not specified.
  virtual std::string UUID();

  /// Returns the name, and possibly with a version number, of the key source.
  /// (This would be the ContentProtection@value attribute in the MPD. DASH-IF-
  /// IOP v3.0 recommends this to be the DRM system and version name in human
  /// readable from.)
  /// @return the name of the key source, empty string if not specified.
  virtual std::string SystemName();

  /// Create KeySource object from hex strings.
  /// @param key_id_hex is the key id in hex string.
  /// @param key_hex is the key in hex string.
  /// @param pssh_data_hex is the pssh_data in hex string.
  /// @param iv_hex is the IV in hex string. If not specified, a randomly
  ///        generated IV with the default length will be used.
  /// Note: GetKey on the created key source will always return the same key
  ///       for all track types.
  static scoped_ptr<KeySource> CreateFromHexStrings(
      const std::string& key_id_hex,
      const std::string& key_hex,
      const std::string& pssh_data_hex,
      const std::string& iv_hex);

  /// Convert string representation of track type to enum representation.
  static TrackType GetTrackTypeFromString(const std::string& track_type_string);

  /// Convert TrackType to string.
  static std::string TrackTypeToString(TrackType track_type);

 protected:
  KeySource();

  /// @return the raw bytes of the pssh box with system ID and box header
  ///         included.
  static std::vector<uint8_t> PsshBoxFromPsshData(
      const std::vector<uint8_t>& pssh_data);

 private:
  explicit KeySource(scoped_ptr<EncryptionKey> encryption_key);

  scoped_ptr<EncryptionKey> encryption_key_;

  DISALLOW_COPY_AND_ASSIGN(KeySource);
};

}  // namespace media
}  // namespace edash_packager

#endif  // MEDIA_BASE_KEY_SOURCE_H_
