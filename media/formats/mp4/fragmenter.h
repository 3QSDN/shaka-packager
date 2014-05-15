// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef MEDIA_FORMATS_MP4_FRAGMENTER_H_
#define MEDIA_FORMATS_MP4_FRAGMENTER_H_

#include <vector>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "media/base/status.h"

namespace media {

class BufferWriter;
class MediaSample;

namespace mp4 {

struct SegmentReference;
struct TrackFragment;

/// Fragmenter is responsible for the generation of MP4 fragments, i.e. 'traf'
/// box and corresponding 'mdat' box.
class Fragmenter {
 public:
  /// @param traf points to a TrackFragment box.
  /// @param normalize_presentation_timestamp defines whether PTS should be
  ///        normalized to start from zero.
  Fragmenter(TrackFragment* traf, bool normalize_presentation_timestamp);

  virtual ~Fragmenter();

  /// Add a sample to the fragmenter.
  /// @param sample points to the sample to be added.
  /// @return OK on success, an error status otherwise.
  virtual Status AddSample(scoped_refptr<MediaSample> sample);

  /// Initialize the fragment with default data.
  /// @return OK on success, an error status otherwise.
  virtual Status InitializeFragment();

  /// Finalize and optimize the fragment.
  virtual void FinalizeFragment();

  /// Fill @a reference with current fragment information.
  void GenerateSegmentReference(SegmentReference* reference);

  uint64 fragment_duration() const { return fragment_duration_; }
  uint64 first_sap_time() const { return first_sap_time_; }
  uint64 earliest_presentation_time() const {
    return earliest_presentation_time_;
  }
  bool fragment_finalized() const { return fragment_finalized_; }
  BufferWriter* data() { return data_.get(); }
  BufferWriter* aux_data() { return aux_data_.get(); }

 protected:
  TrackFragment* traf() { return traf_; }

  /// Optimize sample entries table. If all values in @a entries are identical,
  /// then @a entries is cleared and the value is assigned to @a default_value;
  /// otherwise it is a NOP. Return true if the table is optimized.
  template <typename T>
  bool OptimizeSampleEntries(std::vector<T>* entries, T* default_value);

 private:
  // Check if the current fragment starts with SAP.
  bool StartsWithSAP();

  TrackFragment* traf_;
  bool fragment_finalized_;
  uint64 fragment_duration_;
  bool normalize_presentation_timestamp_;
  int64 presentation_start_time_;
  int64 earliest_presentation_time_;
  int64 first_sap_time_;
  scoped_ptr<BufferWriter> data_;
  scoped_ptr<BufferWriter> aux_data_;

  DISALLOW_COPY_AND_ASSIGN(Fragmenter);
};

template <typename T>
bool Fragmenter::OptimizeSampleEntries(std::vector<T>* entries,
                                       T* default_value) {
  DCHECK(entries);
  DCHECK(default_value);
  DCHECK(!entries->empty());

  typename std::vector<T>::const_iterator it = entries->begin();
  T value = *it;
  for (; it < entries->end(); ++it)
    if (value != *it)
      return false;

  // Clear |entries| if it contains only one value.
  entries->clear();
  *default_value = value;
  return true;
}

}  // namespace mp4
}  // namespace media

#endif  // MEDIA_FORMATS_MP4_FRAGMENTER_H_
