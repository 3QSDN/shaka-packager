// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gflags/gflags.h>
#include <iostream>

#include "app/fixed_key_encryption_flags.h"
#include "app/muxer_flags.h"
#include "app/widevine_encryption_flags.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "media/base/demuxer.h"
#include "media/base/fixed_encryptor_source.h"
#include "media/base/widevine_encryptor_source.h"
#include "media/base/media_stream.h"
#include "media/base/muxer_options.h"
#include "media/base/request_signer.h"
#include "media/base/stream_info.h"
#include "media/event/vod_media_info_dump_muxer_listener.h"
#include "media/file/file.h"
#include "media/file/file_closer.h"
#include "media/mp4/mp4_muxer.h"

DEFINE_bool(dump_stream_info, false, "Dump demuxed stream info.");

namespace {
const char kUsage[] =
    "Packager driver program. Sample Usage:\n%s <input> [flags]";
}  // namespace

namespace media {

void DumpStreamInfo(const std::vector<MediaStream*>& streams) {
  printf("Found %d stream(s).\n", streams.size());
  for (size_t i = 0; i < streams.size(); ++i)
    printf("Stream [%d] %s\n", i, streams[i]->info()->ToString().c_str());
}

// Create and initialize encryptor source.
scoped_ptr<EncryptorSource> CreateEncryptorSource() {
  scoped_ptr<EncryptorSource> encryptor_source;
  if (FLAGS_enable_widevine_encryption) {
    std::string rsa_private_key;
    if (!File::ReadFileToString(FLAGS_signing_key_path.c_str(),
                                &rsa_private_key)) {
      LOG(ERROR) << "Failed to read from '" << FLAGS_signing_key_path << "'.";
      return scoped_ptr<EncryptorSource>();
    }

    scoped_ptr<RequestSigner> signer(
        RsaRequestSigner::CreateSigner(FLAGS_signer, rsa_private_key));
    if (!signer) {
      LOG(ERROR) << "Cannot create signer object from '"
                 << FLAGS_signing_key_path << "'.";
      return scoped_ptr<EncryptorSource>();
    }

    WidevineEncryptorSource::TrackType track_type =
        WidevineEncryptorSource::GetTrackTypeFromString(FLAGS_track_type);
    if (track_type == WidevineEncryptorSource::TRACK_TYPE_UNKNOWN) {
      LOG(ERROR) << "Unknown track_type specified.";
      return scoped_ptr<EncryptorSource>();
    }

    encryptor_source.reset(new WidevineEncryptorSource(
        FLAGS_server_url, FLAGS_content_id, track_type, signer.Pass()));
  } else if (FLAGS_enable_fixed_key_encryption) {
    encryptor_source.reset(
        new FixedEncryptorSource(FLAGS_key_id, FLAGS_key, FLAGS_pssh));
  }

  if (encryptor_source) {
    Status status = encryptor_source->Initialize();
    if (!status.ok()) {
      LOG(ERROR) << "Encryptor source failed to initialize: "
                 << status.ToString();
      return scoped_ptr<EncryptorSource>();
    }
  }
  return encryptor_source.Pass();
}

bool GetMuxerOptions(MuxerOptions* muxer_options) {
  DCHECK(muxer_options);

  muxer_options->single_segment = FLAGS_single_segment;
  muxer_options->segment_duration = FLAGS_segment_duration;
  muxer_options->fragment_duration = FLAGS_fragment_duration;
  muxer_options->segment_sap_aligned = FLAGS_segment_sap_aligned;
  muxer_options->fragment_sap_aligned = FLAGS_fragment_sap_aligned;
  muxer_options->normalize_presentation_timestamp =
      FLAGS_normalize_presentation_timestamp;
  muxer_options->num_subsegments_per_sidx = FLAGS_num_subsegments_per_sidx;
  muxer_options->output_file_name = FLAGS_output;
  muxer_options->segment_template = FLAGS_segment_template;
  muxer_options->temp_file_name = FLAGS_temp_file;

  // Create a temp file if needed.
  if (muxer_options->single_segment && muxer_options->temp_file_name.empty()) {
    base::FilePath path;
    if (!base::CreateTemporaryFile(&path)) {
      LOG(ERROR) << "Failed to create a temporary file.";
      return false;
    }
    muxer_options->temp_file_name = path.value();
  }
  return true;
}

MediaStream* FindFirstStreamOfType(const std::vector<MediaStream*>& streams,
                                   StreamType stream_type) {
  typedef std::vector<MediaStream*>::const_iterator StreamIterator;
  for (StreamIterator it = streams.begin(); it != streams.end(); ++it) {
    if ((*it)->info()->stream_type() == stream_type)
      return *it;
  }
  return NULL;
}
MediaStream* FindFirstVideoStream(const std::vector<MediaStream*>& streams) {
  return FindFirstStreamOfType(streams, kStreamVideo);
}
MediaStream* FindFirstAudioStream(const std::vector<MediaStream*>& streams) {
  return FindFirstStreamOfType(streams, kStreamAudio);
}

bool AddStreamToMuxer(const std::vector<MediaStream*>& streams, Muxer* muxer) {
  DCHECK(muxer);

  MediaStream* stream = NULL;
  if (FLAGS_stream == "video") {
    stream = FindFirstVideoStream(streams);
  } else if (FLAGS_stream == "audio") {
    stream = FindFirstAudioStream(streams);
  } else {
    // Expect FLAGS_stream to be a zero based stream id.
    size_t stream_id;
    if (!base::StringToSizeT(FLAGS_stream, &stream_id) ||
        stream_id >= streams.size()) {
      LOG(ERROR) << "Invalid argument --stream=" << FLAGS_stream << "; "
                 << "should be 'audio', 'video', or a number within [0, "
                 << streams.size() - 1 << "].";
      return false;
    }
    stream = streams[stream_id];
    DCHECK(stream);
  }

  // This could occur only if FLAGS_stream=audio|video and the corresponding
  // stream does not exist in the input.
  if (!stream) {
    LOG(ERROR) << "No " << FLAGS_stream << " stream found in the input.";
    return false;
  }
  Status status = muxer->AddStream(stream);
  if (!status.ok()) {
    LOG(ERROR) << "Muxer failed to add stream: " << status.ToString();
    return false;
  }
  return true;
}

bool RunPackager(const std::string& input) {
  Status status;

  // Setup and initialize Demuxer.
  Demuxer demuxer(input, NULL);
  status = demuxer.Initialize();
  if (!status.ok()) {
    LOG(ERROR) << "Demuxer failed to initialize: " << status.ToString();
    return false;
  }

  if (FLAGS_dump_stream_info)
    DumpStreamInfo(demuxer.streams());

  if (FLAGS_output.empty()) {
    LOG(INFO) << "No output specified. Exiting.";
    return true;
  }

  // Setup muxer.
  MuxerOptions muxer_options;
  if (!GetMuxerOptions(&muxer_options))
    return false;

  scoped_ptr<Muxer> muxer(new mp4::MP4Muxer(muxer_options));
  scoped_ptr<event::MuxerListener> muxer_listener;
  scoped_ptr<File, FileCloser> mpd_file;
  if (FLAGS_output_media_info) {
    std::string output_mpd_file_name = FLAGS_output + ".media_info";
    mpd_file.reset(File::Open(output_mpd_file_name.c_str(), "w"));
    if (!mpd_file) {
      LOG(ERROR) << "Failed to open " << output_mpd_file_name;
      return false;
    }

    scoped_ptr<event::VodMediaInfoDumpMuxerListener> media_info_muxer_listener(
        new event::VodMediaInfoDumpMuxerListener(mpd_file.get()));
    media_info_muxer_listener->SetContentProtectionSchemeIdUri(
        FLAGS_scheme_id_uri);
    muxer_listener = media_info_muxer_listener.Pass();
    muxer->SetMuxerListener(muxer_listener.get());
  }

  if (!AddStreamToMuxer(demuxer.streams(), muxer.get()))
    return false;

  scoped_ptr<EncryptorSource> encryptor_source;
  if (FLAGS_enable_widevine_encryption || FLAGS_enable_fixed_key_encryption) {
    encryptor_source = CreateEncryptorSource();
    if (!encryptor_source)
      return false;
  }
  muxer->SetEncryptorSource(encryptor_source.get(), FLAGS_clear_lead);

  status = muxer->Initialize();
  if (!status.ok()) {
    LOG(ERROR) << "Muxer failed to initialize: " << status.ToString();
    return false;
  }

  // Start remuxing process.
  status = demuxer.Run();
  if (!status.ok()) {
    LOG(ERROR) << "Remuxing failed: " << status.ToString();
    return false;
  }
  status = muxer->Finalize();
  if (!status.ok()) {
    LOG(ERROR) << "Muxer failed to finalize: " << status.ToString();
    return false;
  }

  printf("Packaging completed successfully.\n");
  return true;
}

}  // namespace media

int main(int argc, char** argv) {
  google::SetUsageMessage(base::StringPrintf(kUsage, argv[0]));
  google::ParseCommandLineFlags(&argc, &argv, true);
  if (argc < 2) {
    google::ShowUsageWithFlags(argv[0]);
    return 1;
  }
  return media::RunPackager(argv[1]) ? 0 : 1;
}
