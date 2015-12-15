// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MEDIA_FORMATS_WEBM_SINGLE_SEGMENT_SEGMENTER_H_
#define MEDIA_FORMATS_WEBM_SINGLE_SEGMENT_SEGMENTER_H_

#include "packager/media/formats/webm/segmenter.h"

#include "packager/base/memory/scoped_ptr.h"
#include "packager/media/base/status.h"
#include "packager/media/formats/webm/mkv_writer.h"

namespace edash_packager {
namespace media {

struct MuxerOptions;

namespace webm {

/// An implementation of a Segmenter for a single-segment.  This assumes that
/// the output file is seekable.  For non-seekable files, use
/// TwoPassSingleSegmentSegmenter.
class SingleSegmentSegmenter : public Segmenter {
 public:
  explicit SingleSegmentSegmenter(const MuxerOptions& options);
  ~SingleSegmentSegmenter() override;

  /// @name Segmenter implementation overrides.
  /// @{
  bool GetInitRangeStartAndEnd(uint32_t* start, uint32_t* end) override;
  bool GetIndexRangeStartAndEnd(uint32_t* start, uint32_t* end) override;
  /// @}

 protected:
  MkvWriter* writer() { return writer_.get(); }
  void set_init_end(uint64_t end) { init_end_ = end; }
  void set_index_start(uint64_t start) { index_start_ = start; }
  void set_writer(scoped_ptr<MkvWriter> writer) { writer_ = writer.Pass(); }

  // Segmenter implementation overrides.
  Status DoInitialize(scoped_ptr<MkvWriter> writer) override;
  Status DoFinalize() override;

 private:
  // Segmenter implementation overrides.
  Status NewSubsegment(uint64_t start_timescale) override;
  Status NewSegment(uint64_t start_timescale) override;

  scoped_ptr<MkvWriter> writer_;
  uint32_t init_end_;
  uint32_t index_start_;

  DISALLOW_COPY_AND_ASSIGN(SingleSegmentSegmenter);
};

}  // namespace webm
}  // namespace media
}  // namespace edash_packager

#endif  // MEDIA_FORMATS_WEBM_SINGLE_SEGMENT_SEGMENTER_H_
