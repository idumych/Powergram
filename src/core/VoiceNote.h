// SPDX-License-Identifier: GPL-3.0-or-later
// Recording a voice message, the parts that aren't sound hardware: mixing
// to mono at Opus's 48 kHz, the waveform Telegram draws from 5-bit levels,
// and the request that sends it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Json.h"

namespace pp {

// Mono 48 kHz from any rate and channel count, by linear interpolation
// (speech doesn't need more), keeping its place across chunks.
class Resampler {
public:
	Resampler(double inputRate, int channels);
	// Interleaved float samples in; mono 48 kHz samples appended to `out`.
	void Process(const float* samples, size_t frames, std::vector<float>& out);

private:
	double fStep;        // input frames per output frame
	int fChannels;
	double fPosition = 0;   // of the next output frame, in input frames
	float fLast = 0;        // the frame before this chunk
	bool fHaveLast = false;
};

// The loudest moment of each stretch of a recording, turned into the 100
// levels (0..31) Telegram draws as bars.
class WaveformMeter {
public:
	void Add(const float* mono, size_t count);
	// 0..1, the peak since the last call: for a live level while recording.
	float TakeLevel();
	// One level (0..31) per bar, scaled so the loudest is 31.
	std::string Levels(int bars = 100) const;
	double Seconds() const { return fSamples / 48000.0; }

private:
	std::vector<float> fPeaks;   // one per 10 ms
	float fCurrent = 0;
	size_t fInCurrent = 0;
	size_t fSamples = 0;
	float fLevel = 0;
};

// The inverse of UnpackWaveform: 5 bits per level, low bits first.
std::string PackWaveform(const std::string& levels);
std::string EncodeBase64(const std::string& data);

namespace request {

json SendVoice(int64_t chatId, const std::string& path, int32_t duration,
	const std::string& levels, int64_t replyTo = 0);
json RecordingVoice(int64_t chatId, bool recording);

} // namespace request

} // namespace pp
