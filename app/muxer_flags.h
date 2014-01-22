// Copyright (c) 2013 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Defines Muxer flags.

#ifndef APP_MUXER_FLAGS_H_
#define APP_MUXER_FLAGS_H_

#include <gflags/gflags.h>

DEFINE_bool(audio, false, "Add the first audio stream to muxer.");
DEFINE_bool(video, false, "Add the first video stream to muxer.");
DEFINE_double(clear_lead,
              10.0,
              "Clear lead in seconds if encryption is enabled.");

DEFINE_bool(single_segment,
            true,
            "Generate a single segment for the media presentation. This option "
            "should be set for on demand profile.");
DEFINE_double(segment_duration,
              10.0f,
              "Segment duration in seconds. If single_segment is specified, "
              "this parameter sets the duration of a subsegment; otherwise, "
              "this parameter sets the duration of a segment. Actual segment "
              "durations may not be exactly as requested.");
DEFINE_bool(segment_sap_aligned,
            true,
            "Force segments to begin with stream access points.");
DEFINE_double(fragment_duration,
              2.0f,
              "Fragment duration in seconds. Should not be larger than "
              "the segment duration. Actual fragment durations may not be "
              "exactly as requested.");
DEFINE_bool(fragment_sap_aligned,
            true,
            "Force fragments to begin with stream access points. This flag "
            "implies segment_sap_aligned.");
DEFINE_int32(num_subsegments_per_sidx,
             1,
             "For ISO BMFF only. Set the number of subsegments in each "
             "SIDX box. If 0, a single SIDX box is used per segment; if "
             "-1, no SIDX box is used; Otherwise, the muxer packs N "
             "subsegments in the root SIDX of the segment, with "
             "segment_duration/N/fragment_duration fragments per "
             "subsegment.");
DEFINE_string(output,
              "",
              "Output file path. If segment_template is not specified, "
              "the muxer generates this single output file with all "
              "segments concatenated; Otherwise, it specifies the "
              "initialization segment name.");
DEFINE_string(segment_template,
              "",
              "Output segment name pattern for generated segments. It "
              "can furthermore be configured using a subset of "
              "SegmentTemplate identifiers: $Number$, $Bandwidth$ and "
              "$Time$.");
DEFINE_string(temp_file,
              "",
              "Specify a temporary file for on demand media file creation. If "
              "not specified, a new file will be created in an OS-dependent "
              "temporary directory.");

#endif  // APP_MUXER_FLAGS_H_
