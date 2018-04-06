// Copyright 2018 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/chunking/cue_alignment_handler.h"

namespace shaka {
namespace media {
namespace {
// The max number of samples that are allowed to be buffered before we shutdown
// because there is likely a problem with the content or how the pipeline was
// configured. This is about 20 seconds of buffer for audio with 48kHz.
const size_t kMaxBufferSize = 1000;

double TimeInSeconds(const StreamInfo& info, const StreamData& data) {
  int64_t time_scale;
  int64_t scaled_time;
  switch (data.stream_data_type) {
    case StreamDataType::kMediaSample:
      time_scale = info.time_scale();
      if (info.stream_type() == kStreamAudio) {
        // Return the start time for video and mid-point for audio, so that for
        // an audio sample, if the portion of the sample after the cue point is
        // bigger than the portion of the sample before the cue point, the
        // sample is placed after the cue.
        // It does not matter for text samples as text samples will be cut at
        // cue point.
        scaled_time =
            data.media_sample->pts() + data.media_sample->duration() / 2;
      } else {
        scaled_time = data.media_sample->pts();
      }
      break;
    case StreamDataType::kTextSample:
      // Text is always in MS but the stream info time scale is 0.
      time_scale = 1000;
      scaled_time = data.text_sample->start_time();
      break;
    default:
      time_scale = 0;
      scaled_time = 0;
      NOTREACHED() << "TimeInSeconds should only be called on media samples "
                      "and text samples.";
      break;
  }

  return static_cast<double>(scaled_time) / time_scale;
}
}  // namespace

CueAlignmentHandler::CueAlignmentHandler(SyncPointQueue* sync_points)
    : sync_points_(sync_points) {}

Status CueAlignmentHandler::InitializeInternal() {
  sync_points_->AddThread();
  stream_states_.resize(num_input_streams());
  return Status::OK;
}

Status CueAlignmentHandler::Process(std::unique_ptr<StreamData> data) {
  switch (data->stream_data_type) {
    case StreamDataType::kStreamInfo:
      return OnStreamInfo(std::move(data));
    case StreamDataType::kTextSample:
    case StreamDataType::kMediaSample:
      return OnSample(std::move(data));
    default:
      VLOG(3) << "Dropping unsupported data type "
              << static_cast<int>(data->stream_data_type);
      return Status::OK;
  }
}

Status CueAlignmentHandler::OnFlushRequest(size_t stream_index) {
  // We need to wait for all stream to flush before we can flush each stream.
  // This allows cached buffers to be cleared and cues to be properly
  // synchronized and set on all streams..

  stream_states_[stream_index].to_be_flushed = true;
  for (const StreamState& stream_state : stream_states_) {
    if (!stream_state.to_be_flushed)
      return Status::OK;
  }

  // |to_be_flushed| is set on all streams. We don't expect to see data in any
  // buffers.
  for (size_t i = 0; i < stream_states_.size(); ++i) {
    StreamState& stream_state = stream_states_[i];
    if (!stream_state.samples.empty()) {
      LOG(WARNING) << "Unexpected data seen on stream " << i;
      while (!stream_state.samples.empty()) {
        Status status = Dispatch(std::move(stream_state.samples.front()));
        if (!status.ok())
          return status;
        stream_state.samples.pop_front();
      }
    }
  }
  return FlushAllDownstreams();
}

Status CueAlignmentHandler::OnStreamInfo(std::unique_ptr<StreamData> data) {
  StreamState& stream_state = stream_states_[data->stream_index];
  // Keep a copy of the stream info so that we can check type and check
  // timescale.
  stream_state.info = data->stream_info;
  // Get the first hint for the stream. Use a negative hint so that if there is
  // suppose to be a sync point at zero, we will still respect it.
  stream_state.next_cue_hint = sync_points_->GetHint(-1);

  return Dispatch(std::move(data));
}

Status CueAlignmentHandler::OnSample(std::unique_ptr<StreamData> sample) {
  // There are two modes:
  //  1. There is a video input.
  //  2. There are no video inputs.
  //
  // When there is a video input, we rely on the video input get the next sync
  // point and release all the samples.
  //
  // When there are no video inputs, we rely on the sync point queue to block
  // us until there is a sync point.

  const uint64_t stream_index = sample->stream_index;
  StreamState& stream_state = stream_states_[stream_index];

  if (stream_state.info->stream_type() == kStreamVideo) {
    const double sample_time = TimeInSeconds(*stream_state.info, *sample);
    if (sample->media_sample->is_key_frame() &&
        sample_time >= stream_state.next_cue_hint) {
      std::shared_ptr<const CueEvent> next_sync =
          sync_points_->PromoteAt(sample_time);
      if (!next_sync) {
        LOG(ERROR)
            << "Failed to promote sync point at " << sample_time
            << ". This happens only if video streams are not GOP-aligned.";
        return Status(error::INVALID_ARGUMENT,
                      "Streams are not properly GOP-aligned.");
      }

      Status status(DispatchCueEvent(stream_index, next_sync));
      stream_state.cue.reset();
      status.Update(UseNewSyncPoint(next_sync));
      if (!status.ok())
        return status;
    }
    return Dispatch(std::move(sample));
  }

  // Accept the sample. This will output it if it comes before the hint point or
  // will cache it if it comes after the hint point.
  Status status = AcceptSample(std::move(sample), &stream_state);
  if (!status.ok()) {
    return status;
  }

  // If all the streams are waiting on a hint, it means that none has next sync
  // point determined. It also means that there are no video streams and we need
  // to wait for all streams to converge on a hint so that we can get the next
  // sync point.
  if (EveryoneWaitingAtHint()) {
    // All streams should have the same hint right now.
    const double next_cue_hint = stream_state.next_cue_hint;
    for (const StreamState& stream_state : stream_states_) {
      DCHECK_EQ(next_cue_hint, stream_state.next_cue_hint);
    }

    std::shared_ptr<const CueEvent> next_sync =
        sync_points_->GetNext(next_cue_hint);
    if (!next_sync) {
      // This happens only if the job is cancelled.
      return Status(error::CANCELLED, "SyncPointQueue is cancelled.");
    }

    Status status = UseNewSyncPoint(next_sync);
    if (!status.ok()) {
      return status;
    }
  }

  return Status::OK;
}

Status CueAlignmentHandler::UseNewSyncPoint(
    std::shared_ptr<const CueEvent> new_sync) {
  const double new_hint = sync_points_->GetHint(new_sync->time_in_seconds);
  DCHECK_GT(new_hint, new_sync->time_in_seconds);

  Status status;
  for (StreamState& stream_state : stream_states_) {
    // No stream should be so out of sync with the others that they would
    // still be working on an old cue.
    if (stream_state.cue) {
      // TODO(kqyang): Could this happen for text when there are no text samples
      // between the two cues?
      LOG(ERROR) << "Found two cue events that are too close together. One at "
                 << stream_state.cue->time_in_seconds << " and the other at "
                 << new_sync->time_in_seconds;
      return Status(error::INVALID_ARGUMENT, "Cue events too close together");
    }

    // Add the cue and update the hint. The cue will always be used over the
    // hint, so hint should always be greater than the latest cue.
    stream_state.cue = new_sync;
    stream_state.next_cue_hint = new_hint;

    while (status.ok() && !stream_state.samples.empty()) {
      std::unique_ptr<StreamData>& sample = stream_state.samples.front();
      const double sample_time_in_seconds =
          TimeInSeconds(*stream_state.info, *sample);
      if (sample_time_in_seconds >= stream_state.next_cue_hint) {
        DCHECK(!stream_state.cue);
        break;
      }

      const size_t stream_index = sample->stream_index;
      if (stream_state.cue) {
        status.Update(DispatchCueIfNeeded(stream_index, sample_time_in_seconds,
                                          &stream_state));
      }
      status.Update(Dispatch(std::move(sample)));
      stream_state.samples.pop_front();
    }
  }
  return status;
}

bool CueAlignmentHandler::EveryoneWaitingAtHint() const {
  for (const StreamState& stream_state : stream_states_) {
    if (stream_state.samples.empty()) {
      return false;
    }
  }
  return true;
}

// Accept Sample will either:
//  1. Send the sample downstream, as it comes before the next sync point and
//     therefore can skip the buffering.
//  2. Save the sample in the buffer as it comes after the next sync point.
Status CueAlignmentHandler::AcceptSample(std::unique_ptr<StreamData> sample,
                                         StreamState* stream_state) {
  DCHECK(stream_state);

  const size_t stream_index = sample->stream_index;
  if (stream_state->samples.empty()) {
    const double sample_time_in_seconds =
        TimeInSeconds(*stream_state->info, *sample);
    if (sample_time_in_seconds < stream_state->next_cue_hint) {
      Status status;
      if (stream_state->cue) {
        status.Update(DispatchCueIfNeeded(stream_index, sample_time_in_seconds,
                                          stream_state));
      }
      status.Update(Dispatch(std::move(sample)));
      return status;
    }
    DCHECK(!stream_state->cue);
  }

  stream_state->samples.push_back(std::move(sample));
  if (stream_state->samples.size() > kMaxBufferSize) {
    LOG(ERROR) << "Stream " << stream_index << " has buffered "
               << stream_state->samples.size() << " when the max is "
               << kMaxBufferSize;
    return Status(error::INVALID_ARGUMENT,
                  "Streams are not properly multiplexed.");
  }

  return Status::OK;
}

Status CueAlignmentHandler::DispatchCueIfNeeded(
    size_t stream_index,
    double next_sample_time_in_seconds,
    StreamState* stream_state) {
  DCHECK(stream_state->cue);
  if (next_sample_time_in_seconds < stream_state->cue->time_in_seconds)
    return Status::OK;
  DCHECK_LT(stream_state->cue->time_in_seconds, stream_state->next_cue_hint);
  return DispatchCueEvent(stream_index, std::move(stream_state->cue));
}

}  // namespace media
}  // namespace shaka
