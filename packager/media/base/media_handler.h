// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_BASE_MEDIA_HANDLER_H_
#define PACKAGER_MEDIA_BASE_MEDIA_HANDLER_H_

#include <map>
#include <memory>
#include <utility>

#include "packager/media/base/status.h"

namespace shaka {
namespace media {

/// MediaHandler is the base media processing unit. Media handlers transform
/// the input streams and propagate the outputs to downstream media handlers.
/// There are three different types of media handlers:
///   1) Single input single output
///      This is the most basic handler. It only supports one input and one
///      output with both index as 0.
///   2) Multiple inputs multiple outputs
///      The number of outputs must be equal to the number of inputs. The
///      output stream at a specific index comes from the input stream at the
///      same index. Different streams usually share a common resource, although
///      they may be independent. One example of this is encryptor handler.
///   3) Single input multiple outputs
///      The input stream is splitted into multiple output streams. One example
///      of this is trick play handler.
/// Other types of media handlers are disallowed and not supported.
class MediaHandler {
 public:
  MediaHandler() = default;
  virtual ~MediaHandler() = default;

  /// Connect downstream handler at the specified output stream index.
  Status SetHandler(int output_stream_index,
                    std::shared_ptr<MediaHandler> handler);

  /// Connect downstream handler to the next availble output stream index.
  Status AddHandler(std::shared_ptr<MediaHandler> handler) {
    return SetHandler(next_output_stream_index_, handler);
  }

 protected:
  enum class StreamDataType {
    kUnknown,
    kPeriodInfo,
    kStreamInfo,
    kEncryptionConfig,
    kMediaSample,
    kMediaEvent,
    kSegmentInfo,
  };

  // TODO(kqyang): Define these structures.
  struct PeriodInfo {};
  struct StreamInfo {};
  struct EncryptionConfig {};
  struct MediaSample {};
  struct MediaEvent {};
  struct SegmentInfo {};

  // TODO(kqyang): Should we use protobuf?
  struct StreamData {
    int stream_index;
    StreamDataType stream_data_type;

    std::unique_ptr<PeriodInfo> period_info;
    std::unique_ptr<StreamInfo> stream_info;
    std::unique_ptr<EncryptionConfig> encryption_config;
    std::unique_ptr<MediaSample> media_sample;
    std::unique_ptr<MediaEvent> media_event;
    std::unique_ptr<SegmentInfo> segment_info;
  };

  /// Process the incoming stream data. Note that (1) stream_data.stream_index
  /// should be the input stream index; (2) The implementation needs to call
  /// DispatchXxx to dispatch the processed stream data to the downstream
  /// handlers after finishing processing if needed.
  virtual Status Process(std::unique_ptr<StreamData> stream_data) = 0;

  /// Flush the stream at the specified input stream index.
  virtual Status FlushStream(int input_stream_index);

  /// Validate if the stream at the specified index actually exists.
  virtual bool ValidateOutputStreamIndex(int stream_index) const;

  /// Dispatch the stream data to downstream handlers. Note that
  /// stream_data.stream_index should be the output stream index.
  Status Dispatch(std::unique_ptr<StreamData> stream_data);

  /// Dispatch the period info to downstream handlers.
  Status DispatchPeriodInfo(int stream_index,
                            std::unique_ptr<PeriodInfo> period_info) {
    std::unique_ptr<StreamData> stream_data(new StreamData);
    stream_data->stream_index = stream_index;
    stream_data->period_info = std::move(period_info);
    return Dispatch(std::move(stream_data));
  }

  /// Dispatch the stream info to downstream handlers.
  Status DispatchStreamInfo(int stream_index,
                            std::unique_ptr<StreamInfo> stream_info) {
    std::unique_ptr<StreamData> stream_data(new StreamData);
    stream_data->stream_index = stream_index;
    stream_data->stream_info = std::move(stream_info);
    return Dispatch(std::move(stream_data));
  }

  /// Dispatch the encryption config to downstream handlers.
  Status DispatchEncryptionConfig(
      int stream_index,
      std::unique_ptr<EncryptionConfig> encryption_config) {
    std::unique_ptr<StreamData> stream_data(new StreamData);
    stream_data->stream_index = stream_index;
    stream_data->encryption_config = std::move(encryption_config);
    return Dispatch(std::move(stream_data));
  }

  /// Dispatch the media sample to downstream handlers.
  Status DispatchMediaSample(int stream_index,
                             std::unique_ptr<MediaSample> media_sample) {
    std::unique_ptr<StreamData> stream_data(new StreamData);
    stream_data->stream_index = stream_index;
    stream_data->media_sample = std::move(media_sample);
    return Dispatch(std::move(stream_data));
  }

  /// Dispatch the media event to downstream handlers.
  Status DispatchMediaEvent(int stream_index,
                            std::unique_ptr<MediaEvent> media_event) {
    std::unique_ptr<StreamData> stream_data(new StreamData);
    stream_data->stream_index = stream_index;
    stream_data->media_event = std::move(media_event);
    return Dispatch(std::move(stream_data));
  }

  /// Dispatch the segment info to downstream handlers.
  Status DispatchSegmentInfo(int stream_index,
                             std::unique_ptr<SegmentInfo> segment_info) {
    std::unique_ptr<StreamData> stream_data(new StreamData);
    stream_data->stream_index = stream_index;
    stream_data->segment_info = std::move(segment_info);
    return Dispatch(std::move(stream_data));
  }

  int num_input_streams() const { return num_input_streams_; }

 private:
  MediaHandler(const MediaHandler&) = delete;
  MediaHandler& operator=(const MediaHandler&) = delete;

  // Number of input streams.
  int num_input_streams_ = 0;
  // The next available output stream index, used by AddHandler.
  int next_output_stream_index_ = 0;
  // output stream index -> {output handler, output handler input stream index}
  // map.
  std::map<int, std::pair<std::shared_ptr<MediaHandler>, int>> output_handlers_;
};

}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_BASE_MEDIA_HANDLER_H_
