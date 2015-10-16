// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MEDIA_FORMATS_MP4_ENCRYPTING_FRAGMENTER_H_
#define MEDIA_FORMATS_MP4_ENCRYPTING_FRAGMENTER_H_

#include "packager/media/formats/mp4/fragmenter.h"

namespace edash_packager {
namespace media {

class AesCtrEncryptor;
struct EncryptionKey;

namespace mp4 {

/// EncryptingFragmenter generates MP4 fragments with sample encrypted.
class EncryptingFragmenter : public Fragmenter {
 public:
  /// @param traf points to a TrackFragment box.
  /// @param encryption_key contains the encryption parameters.
  /// @param clear_time specifies clear lead duration in units of the current
  ///        track's timescale.
  /// @param nalu_length_size specifies the size of NAL unit length, in bytes,
  ///        for subsample encryption.
  EncryptingFragmenter(TrackFragment* traf,
                       scoped_ptr<EncryptionKey> encryption_key,
                       int64_t clear_time,
                       uint8_t nalu_length_size);

  ~EncryptingFragmenter() override;

  /// @name Fragmenter implementation overrides.
  /// @{
  Status AddSample(scoped_refptr<MediaSample> sample) override;
  Status InitializeFragment(int64_t first_sample_dts) override;
  void FinalizeFragment() override;
  /// @}

 protected:
  /// Prepare current fragment for encryption.
  /// @return OK on success, an error status otherwise.
  virtual Status PrepareFragmentForEncryption(bool enable_encryption);
  /// Finalize current fragment for encryption.
  virtual void FinalizeFragmentForEncryption();

  /// Create the encryptor for the internal encryption key. The existing
  /// encryptor will be reset if it is not NULL.
  /// @return OK on success, an error status otherwise.
  Status CreateEncryptor();

  EncryptionKey* encryption_key() { return encryption_key_.get(); }
  AesCtrEncryptor* encryptor() { return encryptor_.get(); }

  void set_encryption_key(scoped_ptr<EncryptionKey> encryption_key) {
    encryption_key_ = encryption_key.Pass();
  }

 private:
  void EncryptBytes(uint8_t* data, uint32_t size);
  Status EncryptSample(scoped_refptr<MediaSample> sample);

  // Should we enable subsample encryption?
  bool IsSubsampleEncryptionRequired() { return nalu_length_size_ != 0; }

  scoped_ptr<EncryptionKey> encryption_key_;
  scoped_ptr<AesCtrEncryptor> encryptor_;
  // If this stream contains AVC, subsample encryption specifies that the size
  // and type of NAL units remain unencrypted. This field specifies the size of
  // the size field. Can be 1, 2 or 4 bytes.
  const uint8_t nalu_length_size_;
  int64_t clear_time_;

  DISALLOW_COPY_AND_ASSIGN(EncryptingFragmenter);
};

}  // namespace mp4
}  // namespace media
}  // namespace edash_packager

#endif  // MEDIA_FORMATS_MP4_ENCRYPTING_FRAGMENTER_H_
