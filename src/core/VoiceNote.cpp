// SPDX-License-Identifier: GPL-3.0-or-later
#include "VoiceNote.h"

#include <algorithm>
#include <cmath>


namespace pp {

Resampler::Resampler(double inputRate, int channels)
	:
	fStep(inputRate / 48000.0),
	fChannels(std::max(1, channels))
{
}


void
Resampler::Process(const float* samples, size_t frames, std::vector<float>& out)
{
	auto mono = [&](size_t frame) {
		float sum = 0;
		for (int c = 0; c < fChannels; c++)
			sum += samples[frame * fChannels + c];
		return sum / fChannels;
	};
	// fPosition counts in input frames from this chunk's first; -1 is the
	// last frame of the chunk before.
	for (;;) {
		long index = (long)std::floor(fPosition);
		if (index + 1 >= (long)frames)
			break;   // needs the next chunk
		float a = index < 0 ? (fHaveLast ? fLast : mono(0)) : mono((size_t)index);
		float b = mono((size_t)(index + 1));
		double t = fPosition - index;
		out.push_back((float)(a + (b - a) * t));
		fPosition += fStep;
	}
	if (frames > 0) {
		fLast = mono(frames - 1);
		fHaveLast = true;
		fPosition -= (double)frames;
	}
}


void
WaveformMeter::Add(const float* mono, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		float v = std::fabs(mono[i]);
		fCurrent = std::max(fCurrent, v);
		fLevel = std::max(fLevel, v);
		if (++fInCurrent == 480) {
			fPeaks.push_back(fCurrent);
			fCurrent = 0;
			fInCurrent = 0;
		}
	}
	fSamples += count;
}


float
WaveformMeter::TakeLevel()
{
	float level = std::min(1.0f, fLevel);
	fLevel = 0;
	return level;
}


std::string
WaveformMeter::Levels(int bars) const
{
	std::string levels;
	if (fPeaks.empty() || bars <= 0)
		return levels;
	float loudest = *std::max_element(fPeaks.begin(), fPeaks.end());
	for (int i = 0; i < bars; i++) {
		size_t from = fPeaks.size() * i / bars;
		size_t to = std::max(from + 1, fPeaks.size() * (i + 1) / bars);
		float peak = 0;
		for (size_t k = from; k < to && k < fPeaks.size(); k++)
			peak = std::max(peak, fPeaks[k]);
		int level = loudest > 0.001f ? (int)std::lround(31 * peak / loudest) : 0;
		levels += (char)std::min(31, std::max(0, level));
	}
	return levels;
}


std::string
PackWaveform(const std::string& levels)
{
	std::string packed((levels.size() * 5 + 7) / 8, '\0');
	for (size_t i = 0; i < levels.size(); i++) {
		unsigned value = (unsigned char)levels[i] & 31;
		size_t bit = i * 5;
		packed[bit / 8] = (char)((unsigned char)packed[bit / 8] | (value << (bit % 8)));
		if (bit % 8 > 3)
			packed[bit / 8 + 1] = (char)((unsigned char)packed[bit / 8 + 1]
				| (value >> (8 - bit % 8)));
	}
	return packed;
}


std::string
EncodeBase64(const std::string& data)
{
	static const char* chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	unsigned value = 0;
	int bits = 0;
	for (unsigned char c : data) {
		value = (value << 8) | c;
		bits += 8;
		while (bits >= 6) {
			bits -= 6;
			out += chars[(value >> bits) & 63];
		}
	}
	if (bits > 0)
		out += chars[(value << (6 - bits)) & 63];
	while (out.size() % 4 != 0)
		out += '=';
	return out;
}


namespace request {

json
SendVoice(int64_t chatId, const std::string& path, int32_t duration,
	const std::string& levels, int64_t replyTo)
{
	json request = {{"@type", "sendMessage"}, {"chat_id", chatId},
		{"input_message_content", {{"@type", "inputMessageVoiceNote"},
			{"voice_note", {{"@type", "inputVoiceNote"},
				{"voice_note", {{"@type", "inputFileLocal"}, {"path", path}}},
				{"duration", duration},
				{"waveform", EncodeBase64(PackWaveform(levels))}}}}}};
	if (replyTo != 0)
		request["reply_to"] = {{"@type", "inputMessageReplyToMessage"},
			{"message_id", replyTo}};
	return request;
}


json
RecordingVoice(int64_t chatId, bool recording)
{
	return {{"@type", "sendChatAction"}, {"chat_id", chatId},
		{"action", {{"@type", recording ? "chatActionRecordingVoiceNote" : "chatActionCancel"}}}};
}

} // namespace request

} // namespace pp
