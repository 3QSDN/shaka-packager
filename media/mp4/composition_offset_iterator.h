// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MEDIA_MP4_COMPOSITION_OFFSET_ITERATOR_H_
#define MEDIA_MP4_COMPOSITION_OFFSET_ITERATOR_H_

#include <vector>

#include "media/mp4/box_definitions.h"

namespace media {
namespace mp4 {

/// Composition time to sample box (CTTS) iterator used to iterate through the
/// compressed table. This class also provides convenient functions to query
/// total number of samples and the composition offset for a particular sample.
class CompositionOffsetIterator {
 public:
  /// Create CompositionOffsetIterator from composition time to sample box.
  explicit CompositionOffsetIterator(
      const CompositionTimeToSample& composition_time_to_sample);
  ~CompositionOffsetIterator();

  /// Advance the iterator to the next sample.
  /// @return true if not past the last sample, false otherwise.
  bool AdvanceSample();

  /// @return true if the iterator is still valid, false if past the last
  ///         sample.
  bool IsValid() const;

  /// @return Sample offset for current sample.
  int32 sample_offset() const { return iterator_->sample_offset; }

  /// @return Sample offset @a sample, 1-based.
  int32 SampleOffset(uint32 sample) const;

  /// @return Total number of samples.
  uint32 NumSamples() const;

 private:
  uint32 sample_index_;
  const std::vector<CompositionOffset>& composition_offset_table_;
  std::vector<CompositionOffset>::const_iterator iterator_;

  DISALLOW_COPY_AND_ASSIGN(CompositionOffsetIterator);
};

}  // namespace mp4
}  // namespace media

#endif  // MEDIA_MP4_COMPOSITION_OFFSET_ITERATOR_H_
