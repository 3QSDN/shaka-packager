// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_FILTERS_NAL_UNIT_TO_BYTE_STREAM_CONVERTER_H_
#define PACKAGER_MEDIA_FILTERS_NAL_UNIT_TO_BYTE_STREAM_CONVERTER_H_

#include <stdint.h>
#include <vector>

#include "packager/base/macros.h"
#include "packager/base/memory/ref_counted.h"

namespace edash_packager {
namespace media {

class VideoStreamInfo;

class NalUnitToByteStreamConverter {
 public:
  NalUnitToByteStreamConverter();
  ~NalUnitToByteStreamConverter();

  /// This must be called before calling other methods.
  /// @param decoder_configuration_data is the pointer to a decoder config data.
  /// @param decoder_configuration_data_size is the size of @a
  ///        decoder_configuration_data.
  /// @param escape_data flags whether the decoder configuration and data
  ///        passed to ConvertUnitToByteStream() should be escaped with
  ///        emulation prevention byte.
  /// @return true on success, false otherwise.
  bool Initialize(const uint8_t* decoder_configuration_data,
                  size_t decoder_configuration_data_size,
                  bool escape_data);

  /// Converts unit stream to byte stream using the data passed to Initialize().
  /// The method will function correctly even if @a sample is encrypted using
  /// SAMPLE-AES encryption.
  /// @param sample is the sample to be converted.
  /// @param sample_size is the size of @a sample.
  /// @param output is set to the the converted sample, on success.
  /// @return true on success, false otherwise.
  bool ConvertUnitToByteStream(const uint8_t* sample, size_t sample_size,
                               bool is_key_frame,
                               std::vector<uint8_t>* output);

 private:
  friend class NalUnitToByteStreamConverterTest;

  int nalu_length_size_;
  std::vector<uint8_t> decoder_configuration_in_byte_stream_;
  bool escape_data_;

  DISALLOW_COPY_AND_ASSIGN(NalUnitToByteStreamConverter);
};

}  // namespace media
}  // namespace edash_packager

#endif  // PACKAGER_MEDIA_FILTERS_NAL_UNIT_TO_BYTE_STREAM_CONVERTER_H_
