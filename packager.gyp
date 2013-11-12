# Copyright (c) 2013 Google Inc. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# TODO(kqyang): this file should be in media directory.
{
  'target_defaults': {
    'include_dirs': [
      '.',
    ],
  },
  'targets': [
    {
      'target_name': 'media_base',
      'type': 'static_library',
      'sources': [
        'media/base/audio_stream_info.cc',
        'media/base/audio_stream_info.h',
        'media/base/bit_reader.cc',
        'media/base/bit_reader.h',
        'media/base/buffers.h',
        'media/base/byte_queue.cc',
        'media/base/byte_queue.h',
        'media/base/container_names.cc',
        'media/base/container_names.h',
        # TODO(kqyang): demuxer should not be here, it looks like some kinds of
        # circular dependencies.
        'media/base/demuxer.cc',
        'media/base/demuxer.h',
        'media/base/decrypt_config.cc',
        'media/base/decrypt_config.h',
        'media/base/decryptor_source.h',
        'media/base/encryptor_source.h',
        'media/base/limits.h',
        'media/base/media_parser.h',
        'media/base/media_sample.cc',
        'media/base/media_sample.h',
        'media/base/media_stream.cc',
        'media/base/media_stream.h',
        'media/base/muxer.cc',
        'media/base/muxer.h',
        'media/base/status.cc',
        'media/base/status.h',
        'media/base/stream_info.cc',
        'media/base/stream_info.h',
        'media/base/text_track.h',
        'media/base/video_stream_info.cc',
        'media/base/video_stream_info.h',
      ],
      'dependencies': [
        'base/base.gyp:base',
      ],
    },
    {
      'target_name': 'media_test_support',
      'type': 'static_library',
      'sources': [
        # TODO(kqyang): move these files to test directory.
        'media/base/run_tests_with_atexit_manager.cc',
        'media/base/test_data_util.cc',
        'media/base/test_data_util.h',
      ],
      'dependencies': [
        'base/base.gyp:base',
        'testing/gtest.gyp:gtest',
      ],
    },
    {
      'target_name': 'media_base_unittest',
      'type': 'executable',
      'sources': [
        'media/base/bit_reader_unittest.cc',
        'media/base/container_names_unittest.cc',
        'media/base/status_test_util.h',
        'media/base/status_test_util_unittest.cc',
        'media/base/status_unittest.cc',
      ],
      'dependencies': [
        'media_base',
        'media_test_support',
        'testing/gtest.gyp:gtest',
        'testing/gmock.gyp:gmock',
      ],
    },
    {
      'target_name': 'mp4',
      'type': 'static_library',
      'sources': [
        'media/mp4/aac.cc',
        'media/mp4/aac.h',
        'media/mp4/box_definitions.cc',
        'media/mp4/box_definitions.h',
        'media/mp4/box_reader.cc',
        'media/mp4/box_reader.h',
        'media/mp4/cenc.cc',
        'media/mp4/cenc.h',
        'media/mp4/chunk_info_iterator.cc',
        'media/mp4/chunk_info_iterator.h',
        'media/mp4/composition_offset_iterator.cc',
        'media/mp4/composition_offset_iterator.h',
        'media/mp4/decoding_time_iterator.cc',
        'media/mp4/decoding_time_iterator.h',
        'media/mp4/es_descriptor.cc',
        'media/mp4/es_descriptor.h',
        'media/mp4/fourccs.h',
        'media/mp4/mp4_media_parser.cc',
        'media/mp4/mp4_media_parser.h',
        'media/mp4/offset_byte_queue.cc',
        'media/mp4/offset_byte_queue.h',
        'media/mp4/rcheck.h',
        'media/mp4/sync_sample_iterator.cc',
        'media/mp4/sync_sample_iterator.h',
        'media/mp4/track_run_iterator.cc',
        'media/mp4/track_run_iterator.h',
      ],
      'dependencies': [
        'media_base',
      ],
    },
    {
      'target_name': 'mp4_unittest',
      'type': 'executable',
      'sources': [
        'media/mp4/aac_unittest.cc',
        'media/mp4/box_reader_unittest.cc',
        'media/mp4/chunk_info_iterator_unittest.cc',
        'media/mp4/composition_offset_iterator_unittest.cc',
        'media/mp4/decoding_time_iterator_unittest.cc',
        'media/mp4/es_descriptor_unittest.cc',
        'media/mp4/mp4_media_parser_unittest.cc',
        'media/mp4/offset_byte_queue_unittest.cc',
        'media/mp4/sync_sample_iterator_unittest.cc',
        'media/mp4/track_run_iterator_unittest.cc',
      ],
      'dependencies': [
        'media_test_support',
        'mp4',
        'testing/gtest.gyp:gtest',
        'testing/gmock.gyp:gmock',
      ]
    },
    {
      'target_name': 'file',
      'type': 'static_library',
      'sources': [
        'media/file/file.cc',
        'media/file/file.h',
        'media/file/local_file.cc',
        'media/file/local_file.h',
      ],
      'dependencies': [
        'base/base.gyp:base',
      ],
    },
    {
      'target_name': 'file_unittest',
      'type': 'executable',
      'sources': [
        'media/file/file_unittest.cc',
      ],
      'dependencies': [
        'file',
        'testing/gtest.gyp:gtest',
        'testing/gtest.gyp:gtest_main',
      ],
    },
    {
      'target_name': 'packager_test',
      'type': 'executable',
      'sources': [
        'media/test/packager_test.cc',
      ],
      'dependencies': [
        'file',
        'media_test_support',
        'mp4',
        'testing/gtest.gyp:gtest',
      ],
    },
  ],
}
