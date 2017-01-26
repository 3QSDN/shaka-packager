// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/base/demuxer.h"

#include "packager/base/bind.h"
#include "packager/base/logging.h"
#include "packager/media/base/decryptor_source.h"
#include "packager/media/base/key_source.h"
#include "packager/media/base/media_sample.h"
#include "packager/media/base/media_stream.h"
#include "packager/media/base/stream_info.h"
#include "packager/media/file/file.h"
#include "packager/media/formats/mp2t/mp2t_media_parser.h"
#include "packager/media/formats/mp4/mp4_media_parser.h"
#include "packager/media/formats/webm/webm_media_parser.h"
#include "packager/media/formats/webvtt/webvtt_media_parser.h"
#include "packager/media/formats/wvm/wvm_media_parser.h"

namespace {
// 65KB, sufficient to determine the container and likely all init data.
const size_t kInitBufSize = 0x10000;
const size_t kBufSize = 0x200000;  // 2MB
// Maximum number of allowed queued samples. If we are receiving a lot of
// samples before seeing init_event, something is not right. The number
// set here is arbitrary though.
const size_t kQueuedSamplesLimit = 10000;
}

namespace shaka {
namespace media {

Demuxer::Demuxer(const std::string& file_name)
    : file_name_(file_name),
      media_file_(NULL),
      init_event_received_(false),
      container_name_(CONTAINER_UNKNOWN),
      buffer_(new uint8_t[kBufSize]),
      cancelled_(false) {
}

Demuxer::~Demuxer() {
  if (media_file_)
    media_file_->Close();
}

void Demuxer::SetKeySource(std::unique_ptr<KeySource> key_source) {
  key_source_ = std::move(key_source);
}

Status Demuxer::Initialize() {
  DCHECK(!media_file_);
  DCHECK(!init_event_received_);

  LOG(INFO) << "Initialize Demuxer for file '" << file_name_ << "'.";

  media_file_ = File::Open(file_name_.c_str(), "r");
  if (!media_file_) {
    return Status(error::FILE_FAILURE,
                  "Cannot open file for reading " + file_name_);
  }

  // Read enough bytes before detecting the container.
  size_t bytes_read = 0;
  while (bytes_read < kInitBufSize) {
    int64_t read_result =
        media_file_->Read(buffer_.get() + bytes_read, kInitBufSize);
    if (read_result < 0)
      return Status(error::FILE_FAILURE, "Cannot read file " + file_name_);
    if (read_result == 0)
      break;
    bytes_read += read_result;
  }
  container_name_ = DetermineContainer(buffer_.get(), bytes_read);

  // Initialize media parser.
  switch (container_name_) {
    case CONTAINER_MOV:
      parser_.reset(new mp4::MP4MediaParser());
      break;
    case CONTAINER_MPEG2TS:
      parser_.reset(new mp2t::Mp2tMediaParser());
      break;
    case CONTAINER_MPEG2PS:
      parser_.reset(new wvm::WvmMediaParser());
      break;
    case CONTAINER_WEBM:
      parser_.reset(new WebMMediaParser());
      break;
    case CONTAINER_WEBVTT:
      parser_.reset(new WebVttMediaParser());
      break;
    default:
      NOTIMPLEMENTED();
      return Status(error::UNIMPLEMENTED, "Container not supported.");
  }

  parser_->Init(base::Bind(&Demuxer::ParserInitEvent, base::Unretained(this)),
                base::Bind(&Demuxer::NewSampleEvent, base::Unretained(this)),
                key_source_.get());

  // Handle trailing 'moov'.
  if (container_name_ == CONTAINER_MOV)
    static_cast<mp4::MP4MediaParser*>(parser_.get())->LoadMoov(file_name_);

  if (!parser_->Parse(buffer_.get(), bytes_read)) {
    init_parsing_status_ =
        Status(error::PARSER_FAILURE, "Cannot parse media file " + file_name_);
  }

  // Parse until init event received or on error.
  while (!init_event_received_ && init_parsing_status_.ok())
    init_parsing_status_ = Parse();
  // Defer error reporting if init completed successfully.
  return init_event_received_ ? Status::OK : init_parsing_status_;
}

void Demuxer::ParserInitEvent(
    const std::vector<std::shared_ptr<StreamInfo>>& stream_infos) {
  init_event_received_ = true;
  for (const std::shared_ptr<StreamInfo>& stream_info : stream_infos)
    streams_.emplace_back(new MediaStream(stream_info, this));
}

Demuxer::QueuedSample::QueuedSample(uint32_t local_track_id,
                                    std::shared_ptr<MediaSample> local_sample)
    : track_id(local_track_id), sample(local_sample) {}
Demuxer::QueuedSample::~QueuedSample() {}

bool Demuxer::NewSampleEvent(uint32_t track_id,
                             const std::shared_ptr<MediaSample>& sample) {
  if (!init_event_received_) {
    if (queued_samples_.size() >= kQueuedSamplesLimit) {
      LOG(ERROR) << "Queued samples limit reached: " << kQueuedSamplesLimit;
      return false;
    }
    queued_samples_.push_back(QueuedSample(track_id, sample));
    return true;
  }
  while (!queued_samples_.empty()) {
    if (!PushSample(queued_samples_.front().track_id,
                    queued_samples_.front().sample)) {
      return false;
    }
    queued_samples_.pop_front();
  }
  return PushSample(track_id, sample);
}

bool Demuxer::PushSample(uint32_t track_id,
                         const std::shared_ptr<MediaSample>& sample) {
  for (const std::unique_ptr<MediaStream>& stream : streams_) {
    if (track_id == stream->info()->track_id()) {
      Status status = stream->PushSample(sample);
      if (!status.ok())
        LOG(ERROR) << "Demuxer::PushSample failed with " << status;
      return status.ok();
    }
  }
  LOG(ERROR) << "Track " << track_id << " not found.";
  return false;
}

Status Demuxer::Run() {
  Status status;

  LOG(INFO) << "Demuxer::Run() on file '" << file_name_ << "'.";

  // Start the streams.
  for (const std::unique_ptr<MediaStream>& stream : streams_) {
    status = stream->Start(MediaStream::kPush);
    if (!status.ok())
      return status;
  }

  while (!cancelled_ && (status = Parse()).ok())
    continue;

  if (cancelled_ && status.ok())
    return Status(error::CANCELLED, "Demuxer run cancelled");

  if (status.error_code() == error::END_OF_STREAM) {
    // Push EOS sample to muxer to indicate end of stream.
    const std::shared_ptr<MediaSample>& sample = MediaSample::CreateEOSBuffer();
    for (const std::unique_ptr<MediaStream>& stream : streams_) {
      status = stream->PushSample(sample);
      if (!status.ok())
        return status;
    }
  }
  return status;
}

Status Demuxer::Parse() {
  DCHECK(media_file_);
  DCHECK(parser_);
  DCHECK(buffer_);

  // Return early and avoid call Parse(...) again if it has already failed at
  // the initialization.
  if (!init_parsing_status_.ok())
    return init_parsing_status_;

  int64_t bytes_read = media_file_->Read(buffer_.get(), kBufSize);
  if (bytes_read == 0) {
    if (!parser_->Flush())
      return Status(error::PARSER_FAILURE, "Failed to flush.");
    return Status(error::END_OF_STREAM, "");
  } else if (bytes_read < 0) {
    return Status(error::FILE_FAILURE, "Cannot read file " + file_name_);
  }

  return parser_->Parse(buffer_.get(), bytes_read)
             ? Status::OK
             : Status(error::PARSER_FAILURE,
                      "Cannot parse media file " + file_name_);
}

void Demuxer::Cancel() {
  cancelled_ = true;
}

}  // namespace media
}  // namespace shaka
