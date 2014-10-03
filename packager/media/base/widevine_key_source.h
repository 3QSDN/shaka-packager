// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MEDIA_BASE_WIDEVINE_KEY_SOURCE_H_
#define MEDIA_BASE_WIDEVINE_KEY_SOURCE_H_

#include <map>

#include "packager/base/memory/scoped_ptr.h"
#include "packager/base/synchronization/waitable_event.h"
#include "packager/base/values.h"
#include "packager/media/base/closure_thread.h"
#include "packager/media/base/key_source.h"

namespace edash_packager {
namespace media {
class HttpFetcher;
class RequestSigner;
template <class T> class ProducerConsumerQueue;

/// WidevineKeySource talks to the Widevine encryption service to
/// acquire the encryption keys.
class WidevineKeySource : public KeySource {
 public:
  /// @param server_url is the Widevine common encryption server url.
  /// @param signer signs the request message. It should not be NULL.
  WidevineKeySource(const std::string& server_url,
                    scoped_ptr<RequestSigner> signer);

  virtual ~WidevineKeySource();

  /// @name KeySource implementation overrides.
  /// @{
  virtual Status FetchKeys(const std::vector<uint8_t>& content_id,
                           const std::string& policy) OVERRIDE;
  virtual Status FetchKeys(const std::vector<uint8_t>& pssh_data) OVERRIDE;
  virtual Status FetchKeys(uint32_t asset_id);

  virtual Status GetKey(TrackType track_type, EncryptionKey* key) OVERRIDE;
  virtual Status GetKey(const std::vector<uint8_t>& key_id,
                        EncryptionKey* key) OVERRIDE;
  virtual Status GetCryptoPeriodKey(uint32_t crypto_period_index,
                                    TrackType track_type,
                                    EncryptionKey* key) OVERRIDE;
  /// @}

  /// Inject an @b HttpFetcher object, mainly used for testing.
  /// @param http_fetcher points to the @b HttpFetcher object to be injected.
  void set_http_fetcher(scoped_ptr<HttpFetcher> http_fetcher);

 protected:
   ClosureThread key_production_thread_;

 private:
  typedef std::map<TrackType, EncryptionKey*> EncryptionKeyMap;
  class RefCountedEncryptionKeyMap;
  typedef ProducerConsumerQueue<scoped_refptr<RefCountedEncryptionKeyMap> >
      EncryptionKeyQueue;

  // Internal routine for getting keys.
  Status GetKeyInternal(uint32_t crypto_period_index,
                        TrackType track_type,
                        EncryptionKey* key);

  // Common implementation of FetchKeys methods above.
  Status FetchKeysCommon(bool widevine_classic);

  // The closure task to fetch keys repeatedly.
  void FetchKeysTask();

  // Fetch keys from server.
  Status FetchKeysInternal(bool enable_key_rotation,
                           uint32_t first_crypto_period_index,
                           bool widevine_classic);

  // Fill |request| with necessary fields for Widevine encryption request.
  // |request| should not be NULL.
  void FillRequest(bool enable_key_rotation,
                   uint32_t first_crypto_period_index,
                   std::string* request);
  // Sign and properly format |request|.
  // |signed_request| should not be NULL. Return OK on success.
  Status SignRequest(const std::string& request, std::string* signed_request);
  // Decode |response| from JSON formatted |raw_response|.
  // |response| should not be NULL.
  bool DecodeResponse(const std::string& raw_response, std::string* response);
  // Extract encryption key from |response|, which is expected to be properly
  // formatted. |transient_error| will be set to true if it fails and the
  // failure is because of a transient error from the server. |transient_error|
  // should not be NULL.
  bool ExtractEncryptionKey(bool enable_key_rotation,
                            bool widevine_classic,
                            const std::string& response,
                            bool* transient_error);
  // Push the keys to the key pool.
  bool PushToKeyPool(EncryptionKeyMap* encryption_key_map);

  // The fetcher object used to fetch HTTP response from server.
  // It is initialized to a default fetcher on class initialization.
  // Can be overridden using set_http_fetcher for testing or other purposes.
  scoped_ptr<HttpFetcher> http_fetcher_;
  std::string server_url_;
  scoped_ptr<RequestSigner> signer_;
  base::DictionaryValue request_dict_;

  const uint32_t crypto_period_count_;
  base::Lock lock_;
  bool key_production_started_;
  base::WaitableEvent start_key_production_;
  uint32_t first_crypto_period_index_;
  scoped_ptr<EncryptionKeyQueue> key_pool_;
  EncryptionKeyMap encryption_key_map_;  // For non key rotation request.
  Status common_encryption_request_status_;

  DISALLOW_COPY_AND_ASSIGN(WidevineKeySource);
};

}  // namespace media
}  // namespace edash_packager

#endif  // MEDIA_BASE_WIDEVINE_KEY_SOURCE_H_
