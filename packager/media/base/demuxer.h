// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MEDIA_BASE_DEMUXER_H_
#define MEDIA_BASE_DEMUXER_H_

#include <deque>
#include <memory>
#include <vector>

#include "packager/base/compiler_specific.h"
#include "packager/media/base/container_names.h"
#include "packager/media/base/status.h"

namespace shaka {
namespace media {

class Decryptor;
class File;
class KeySource;
class MediaParser;
class MediaSample;
class MediaStream;
class StreamInfo;

/// Demuxer is responsible for extracting elementary stream samples from a
/// media file, e.g. an ISO BMFF file.
class Demuxer {
 public:
  /// @param file_name specifies the input source. It uses prefix matching to
  ///        create a proper File object. The user can extend File to support
  ///        a custom File object with its own prefix.
  explicit Demuxer(const std::string& file_name);
  ~Demuxer();

  /// Set the KeySource for media decryption.
  /// @param key_source points to the source of decryption keys. The key
  ///        source must support fetching of keys for the type of media being
  ///        demuxed.
  void SetKeySource(std::unique_ptr<KeySource> key_source);

  /// Initialize the Demuxer. Calling other public methods of this class
  /// without this method returning OK, results in an undefined behavior.
  /// This method primes the demuxer by parsing portions of the media file to
  /// extract stream information.
  /// @return OK on success.
  Status Initialize();

  /// Drive the remuxing from demuxer side (push). Read the file and push
  /// the Data to Muxer until Eof.
  Status Run();

  /// Read from the source and send it to the parser.
  Status Parse();

  /// Cancel a demuxing job in progress. Will cause @a Run to exit with an error
  /// status of type CANCELLED.
  void Cancel();

  /// @return Streams in the media container being demuxed. The caller cannot
  ///         add or remove streams from the returned vector, but the caller is
  ///         allowed to change the internal state of the streams in the vector
  ///         through MediaStream APIs.
  const std::vector<std::unique_ptr<MediaStream>>& streams() {
    return streams_;
  }

  /// @return Container name (type). Value is CONTAINER_UNKNOWN if the demuxer
  ///         is not initialized.
  MediaContainerName container_name() { return container_name_; }

 private:
  Demuxer(const Demuxer&) = delete;
  Demuxer& operator=(const Demuxer&) = delete;

  struct QueuedSample {
    QueuedSample(uint32_t track_id, std::shared_ptr<MediaSample> sample);
    ~QueuedSample();

    uint32_t track_id;
    std::shared_ptr<MediaSample> sample;
  };

  // Parser init event.
  void ParserInitEvent(const std::vector<std::shared_ptr<StreamInfo>>& streams);
  // Parser new sample event handler. Queues the samples if init event has not
  // been received, otherwise calls PushSample() to push the sample to
  // corresponding stream.
  bool NewSampleEvent(uint32_t track_id,
                      const std::shared_ptr<MediaSample>& sample);
  // Helper function to push the sample to corresponding stream.
  bool PushSample(uint32_t track_id,
                  const std::shared_ptr<MediaSample>& sample);

  std::string file_name_;
  File* media_file_;
  bool init_event_received_;
  Status init_parsing_status_;
  // Queued samples received in NewSampleEvent() before ParserInitEvent().
  std::deque<QueuedSample> queued_samples_;
  std::unique_ptr<MediaParser> parser_;
  std::vector<std::unique_ptr<MediaStream>> streams_;
  MediaContainerName container_name_;
  std::unique_ptr<uint8_t[]> buffer_;
  std::unique_ptr<KeySource> key_source_;
  bool cancelled_;
};

}  // namespace media
}  // namespace shaka

#endif  // MEDIA_BASE_DEMUXER_H_
