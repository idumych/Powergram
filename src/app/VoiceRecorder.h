// SPDX-License-Identifier: GPL-3.0-or-later
// Records a voice message from the default sound input with the Media
// Kit's BMediaRecorder, as Telegram wants it: mono Opus at 48 kHz in an
// OGG file (libopus and libogg, which Haiku's media add-ons use too).
#pragma once

#include <Locker.h>
#include <String.h>

#include <memory>
#include <string>
#include <vector>

#include "../core/VoiceNote.h"

class BMediaRecorder;
struct media_format;
class OggOpusWriter;

class VoiceRecorder {
public:
	VoiceRecorder();
	~VoiceRecorder();

	// Starts recording into `path`; an error (no sound input, say) in
	// `problem` on failure.
	status_t Start(const char* path, BString* problem);
	// Stops. With `keep`, finishes the file and gives its length and
	// waveform; otherwise deletes it.
	status_t Stop(bool keep, int32* seconds = nullptr, std::string* levels = nullptr);
	bool IsRecording() const { return fRecorder != nullptr; }

	double Seconds();
	float TakeLevel();   // 0..1, the peak since the last call

	// For tests: what the recorder's hook does with a buffer.
	void Feed(const void* data, size_t size, const media_format& format);
	status_t StartFile(const char* path);

private:
	static void _Process(void* cookie, int64 timestamp, void* data, size_t size,
		const media_format& format);

	BMediaRecorder* fRecorder = nullptr;
	BLocker fLock;
	std::unique_ptr<pp::Resampler> fResampler;
	pp::WaveformMeter fMeter;
	std::unique_ptr<OggOpusWriter> fWriter;
	std::vector<float> fFloat;     // the buffer converted to float
	std::vector<float> fMono;      // 48 kHz mono, waiting for whole frames
	std::string fPath;
};

// Opus packets of 20 ms in an OGG stream (RFC 7845).
class OggOpusWriter {
public:
	~OggOpusWriter();
	status_t Open(const char* path);
	// 48 kHz mono; encodes whole 20 ms frames and keeps the rest.
	status_t Write(std::vector<float>& samples);
	// Encodes `rest` (under a frame) padded with silence, marks the end and
	// closes the file.
	status_t Close(const std::vector<float>& rest);

private:
	status_t _WritePages(bool flush);
	status_t _Encode(const float* frame, bool last);

	struct OpusEncoder* fEncoder = nullptr;
	void* fStream = nullptr;   // ogg_stream_state
	FILE* fFile = nullptr;
	int64 fPacket = 0;
	int64 fSamples = 0;        // real samples given
	int64 fEncoded = 0;        // samples encoded, padding included
	int fPreSkip = 0;
};
