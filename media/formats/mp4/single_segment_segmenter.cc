// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "media/formats/mp4/single_segment_segmenter.h"

#include "media/base/buffer_writer.h"
#include "media/base/media_stream.h"
#include "media/base/muxer_options.h"
#include "media/file/file.h"
#include "media/formats/mp4/box_definitions.h"

namespace media {
namespace mp4 {

SingleSegmentSegmenter::SingleSegmentSegmenter(const MuxerOptions& options,
                                               scoped_ptr<FileType> ftyp,
                                               scoped_ptr<Movie> moov)
    : Segmenter(options, ftyp.Pass(), moov.Pass()) {}
SingleSegmentSegmenter::~SingleSegmentSegmenter() {}

bool SingleSegmentSegmenter::GetInitRange(size_t* offset, size_t* size) {
  // In Finalize, ftyp and moov gets written first so offset must be 0.
  *offset = 0;
  *size = ftyp()->ComputeSize() + moov()->ComputeSize();
  return true;
}

bool SingleSegmentSegmenter::GetIndexRange(size_t* offset, size_t* size) {
  // Index range is right after init range so the offset must be the size of
  // ftyp and moov.
  *offset = ftyp()->ComputeSize() + moov()->ComputeSize();
  *size = vod_sidx_->ComputeSize();
  return true;
}

Status SingleSegmentSegmenter::DoInitialize() {
  temp_file_.reset(File::Open(options().temp_file_name.c_str(), "w"));
  return temp_file_
             ? Status::OK
             : Status(error::FILE_FAILURE,
                      "Cannot open file to write " + options().temp_file_name);
}

Status SingleSegmentSegmenter::DoFinalize() {
  DCHECK(temp_file_);
  DCHECK(ftyp());
  DCHECK(moov());
  DCHECK(vod_sidx_);

  // Close the temp file to prepare for reading later.
  if (!temp_file_.release()->Close()) {
    return Status(error::FILE_FAILURE,
                  "Cannot close the temp file " + options().temp_file_name);
  }

  scoped_ptr<File, FileCloser> file(
      File::Open(options().output_file_name.c_str(), "w"));
  if (file == NULL) {
    return Status(error::FILE_FAILURE,
                  "Cannot open file to write " + options().output_file_name);
  }

  // Write ftyp, moov and sidx to output file.
  scoped_ptr<BufferWriter> buffer(new BufferWriter());
  ftyp()->Write(buffer.get());
  moov()->Write(buffer.get());
  vod_sidx_->Write(buffer.get());
  Status status = buffer->WriteToFile(file.get());
  if (!status.ok())
    return status;

  // Load the temp file and write to output file.
  scoped_ptr<File, FileCloser> temp_file(
      File::Open(options().temp_file_name.c_str(), "r"));
  if (temp_file == NULL) {
    return Status(error::FILE_FAILURE,
                  "Cannot open file to read " + options().temp_file_name);
  }

  const int kBufSize = 0x40000;  // 256KB.
  scoped_ptr<uint8[]> buf(new uint8[kBufSize]);
  while (!temp_file->Eof()) {
    int64 size = temp_file->Read(buf.get(), kBufSize);
    if (size <= 0) {
      return Status(error::FILE_FAILURE,
                    "Failed to read file " + options().temp_file_name);
    }
    int64 size_written = file->Write(buf.get(), size);
    if (size_written != size) {
      return Status(error::FILE_FAILURE,
                    "Failed to write file " + options().output_file_name);
    }
  }
  return Status::OK;
}

Status SingleSegmentSegmenter::DoFinalizeSegment() {
  DCHECK(sidx());
  DCHECK(fragment_buffer());
  // sidx() contains pre-generated segment references with one reference per
  // fragment. In VOD, this segment is converted into a subsegment, i.e. one
  // reference, which contains all the fragments in sidx().
  std::vector<SegmentReference>& refs = sidx()->references;
  SegmentReference& vod_ref = refs[0];
  uint64 first_sap_time =
      refs[0].sap_delta_time + refs[0].earliest_presentation_time;
  for (uint32 i = 1; i < sidx()->references.size(); ++i) {
    vod_ref.referenced_size += refs[i].referenced_size;
    // NOTE: We calculate subsegment duration based on the total duration of
    // this subsegment instead of subtracting earliest_presentation_time as
    // indicated in the spec.
    vod_ref.subsegment_duration += refs[i].subsegment_duration;
    vod_ref.earliest_presentation_time = std::min(
        vod_ref.earliest_presentation_time, refs[i].earliest_presentation_time);

    if (vod_ref.sap_type == SegmentReference::TypeUnknown &&
        refs[i].sap_type != SegmentReference::TypeUnknown) {
      vod_ref.sap_type = refs[i].sap_type;
      first_sap_time =
          refs[i].sap_delta_time + refs[i].earliest_presentation_time;
    }
  }
  // Calculate sap delta time w.r.t. earliest_presentation_time.
  if (vod_ref.sap_type != SegmentReference::TypeUnknown) {
    vod_ref.sap_delta_time =
        first_sap_time - vod_ref.earliest_presentation_time;
  }

  // Create segment if it does not exist yet.
  if (vod_sidx_ == NULL) {
    vod_sidx_.reset(new SegmentIndex());
    vod_sidx_->reference_id = sidx()->reference_id;
    vod_sidx_->timescale = sidx()->timescale;
    // earliest_presentation_time is the earliest presentation time of any
    // access unit in the reference stream in the first subsegment.
    vod_sidx_->earliest_presentation_time = vod_ref.earliest_presentation_time;
  }
  vod_sidx_->references.push_back(vod_ref);

  // Append fragment buffer to temp file.
  return fragment_buffer()->WriteToFile(temp_file_.get());
}

}  // namespace mp4
}  // namespace media
