// SPDX-License-Identifier: GPL-3.0-or-later
// Recording on Haiku without a microphone: buffers as BMediaRecorder hands
// them over (44.1 kHz stereo 16-bit, a 440 Hz tone) go through the
// recorder, and the OGG/Opus file it writes is read back with the Media
// Kit's own decoder.
#include <MediaFile.h>
#include <MediaTrack.h>
#include <MediaFormats.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "../src/app/VoiceRecorder.h"

static int sFailures = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
	sFailures++; } } while (0)

int
main(int argc, char** argv)
{
	const char* path = argc > 1 ? argv[1] : "/tmp/voice_record_test.ogg";
	VoiceRecorder recorder;
	CHECK(recorder.StartFile(path) == B_OK);

	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	format.u.raw_audio.frame_rate = 44100;
	format.u.raw_audio.channel_count = 2;
	format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_SHORT;
	format.u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;

	// 3 s: a second of silence, then two seconds of the tone, in buffers
	// of 1024 frames as a sound card gives them.
	const int total = 44100 * 3;
	std::vector<int16> buffer;
	for (int done = 0; done < total; ) {
		int frames = std::min(1024, total - done);
		buffer.clear();
		for (int i = 0; i < frames; i++) {
			int n = done + i;
			int16 v = n < 44100 ? 0
				: (int16)(16000 * std::sin(2 * M_PI * 440 * n / 44100.0));
			buffer.push_back(v);
			buffer.push_back(v);
		}
		recorder.Feed(buffer.data(), buffer.size() * sizeof(int16), format);
		done += frames;
	}
	CHECK(std::fabs(recorder.Seconds() - 3.0) < 0.01);
	int32 seconds = 0;
	std::string levels;
	CHECK(recorder.Stop(true, &seconds, &levels) == B_OK);
	CHECK(seconds == 3);
	CHECK(levels.size() == 100 && levels[10] == 0 && levels[80] >= 30);

	// Read back with Haiku's decoder.
	entry_ref ref;
	CHECK(get_ref_for_path(path, &ref) == B_OK);
	BMediaFile file(&ref);
	CHECK(file.InitCheck() == B_OK && file.CountTracks() == 1);
	if (file.InitCheck() == B_OK && file.CountTracks() == 1) {
		BMediaTrack* track = file.TrackAt(0);
		media_format decoded;
		decoded.type = B_MEDIA_RAW_AUDIO;
		decoded.u.raw_audio = media_raw_audio_format::wildcard;
		decoded.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
		CHECK(track->DecodedFormat(&decoded) == B_OK);
		media_codec_info codec;
		track->GetCodecInfo(&codec);
		printf("codec: %s, rate %.0f, channels %u, duration %.3f s\n", codec.pretty_name,
			decoded.u.raw_audio.frame_rate, (unsigned)decoded.u.raw_audio.channel_count,
			track->Duration() / 1e6);
		CHECK(decoded.u.raw_audio.channel_count == 1);
		size_t frameSize = sizeof(float) * decoded.u.raw_audio.channel_count;
		std::vector<char> data(decoded.u.raw_audio.buffer_size > 0
			? decoded.u.raw_audio.buffer_size : 65536);
		std::vector<float> all;
		int64 frames = 0;
		while (track->ReadFrames(data.data(), &frames) == B_OK && frames > 0) {
			const float* f = (const float*)data.data();
			all.insert(all.end(), f, f + frames * frameSize / sizeof(float));
		}
		double rate = decoded.u.raw_audio.frame_rate;
		printf("decoded %zu samples (%.3f s)\n", all.size(), all.size() / rate);
		CHECK(std::fabs(all.size() / rate - 3.0) < 0.05);
		// Quiet first second, then the tone at about half scale.
		float quiet = 0, loud = 0;
		for (size_t i = 0; i < all.size(); i++) {
			if (i > rate * 0.1 && i < rate * 0.9)
				quiet = std::max(quiet, std::fabs(all[i]));
			if (i > rate * 1.5 && i < rate * 2.5)
				loud = std::max(loud, std::fabs(all[i]));
		}
		printf("peak in the silence %.4f, in the tone %.3f\n", quiet, loud);
		CHECK(quiet < 0.01 && loud > 0.4 && loud < 0.6);
		file.ReleaseTrack(track);
	}

	// Cancelling deletes the file.
	const char* cancelled = "/tmp/voice_record_cancelled.ogg";
	CHECK(recorder.StartFile(cancelled) == B_OK);
	recorder.Feed(buffer.data(), buffer.size() * sizeof(int16), format);
	CHECK(recorder.Stop(false) == B_OK);
	CHECK(access(cancelled, F_OK) != 0);

	printf(sFailures == 0 ? "voice recording tests passed\n" : "%d failures\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
