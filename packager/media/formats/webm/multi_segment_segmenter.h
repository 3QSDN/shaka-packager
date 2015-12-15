// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MEDIA_FORMATS_WEBM_MULTI_SEGMENT_SEGMENTER_H_
#define MEDIA_FORMATS_WEBM_MULTI_SEGMENT_SEGMENTER_H_

#include "packager/media/formats/webm/segmenter.h"

#include "packager/base/memory/scoped_ptr.h"
#include "packager/media/base/status.h"
#include "packager/media/formats/webm/mkv_writer.h"

namespace edash_packager {
namespace media {

struct MuxerOptions;

namespace webm {

/// An implementation of a Segmenter for a multi-segment.  Since this does not
/// use seeking, it does not matter if the underlying files support seeking.
class MultiSegmentSegmenter : public Segmenter {
 public:
  explicit MultiSegmentSegmenter(const MuxerOptions& options);
  ~MultiSegmentSegmenter() override;

  /// @name Segmenter implementation overrides.
  /// @{
  bool GetInitRangeStartAndEnd(uint32_t* start, uint32_t* end) override;
  bool GetIndexRangeStartAndEnd(uint32_t* start, uint32_t* end) override;
  /// @}

 protected:
  // Segmenter implementation overrides.
  Status DoInitialize(scoped_ptr<MkvWriter> writer) override;
  Status DoFinalize() override;

 private:
  // Segmenter implementation overrides.
  Status NewSubsegment(uint64_t start_timescale) override;
  Status NewSegment(uint64_t start_timescale) override;

  Status FinalizeSegment();

  scoped_ptr<MkvWriter> writer_;
  uint32_t num_segment_;

  DISALLOW_COPY_AND_ASSIGN(MultiSegmentSegmenter);
};

}  // namespace webm
}  // namespace media
}  // namespace edash_packager

#endif  // MEDIA_FORMATS_WEBM_MULTI_SEGMENT_SEGMENTER_H_
