// SPDX-License-Identifier: GPL-3.0-or-later
#include "VoiceRecorder.h"

#include <Autolock.h>
#include <MediaRecorder.h>
#include <MediaRoster.h>

#include <ogg/ogg.h>
#include <opus/opus.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>

static const int kFrame = 960;   // 20 ms at 48 kHz

OggOpusWriter::~OggOpusWriter()
{
	if (fStream != nullptr) {
		ogg_stream_clear((ogg_stream_state*)fStream);
		delete (ogg_stream_state*)fStream;
	}
	if (fEncoder != nullptr)
		opus_encoder_destroy(fEncoder);
	if (fFile != nullptr)
		fclose(fFile);
}


static void
PutLE(unsigned char* p, uint32 value, int bytes)
{
	for (int i = 0; i < bytes; i++)
		p[i] = (unsigned char)(value >> (8 * i));
}


status_t
OggOpusWriter::Open(const char* path)
{
	int error = 0;
	fEncoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &error);
	if (error != OPUS_OK || fEncoder == nullptr)
		return B_ERROR;
	opus_encoder_ctl(fEncoder, OPUS_SET_BITRATE(32000));
	opus_int32 lookahead = 0;
	opus_encoder_ctl(fEncoder, OPUS_GET_LOOKAHEAD(&lookahead));
	fPreSkip = lookahead;

	fFile = fopen(path, "wb");
	if (fFile == nullptr)
		return B_ERROR;
	ogg_stream_state* stream = new ogg_stream_state;
	srand((unsigned)time(nullptr) ^ (unsigned)getpid());
	ogg_stream_init(stream, rand());
	fStream = stream;

	// The identification header, then the comment header, each on its own page.
	unsigned char head[19];
	memcpy(head, "OpusHead", 8);
	head[8] = 1;                  // version
	head[9] = 1;                  // channels
	PutLE(head + 10, fPreSkip, 2);
	PutLE(head + 12, 48000, 4);   // the input's rate
	PutLE(head + 16, 0, 2);       // output gain
	head[18] = 0;                 // mapping family: mono/stereo
	ogg_packet packet = {};
	packet.packet = head;
	packet.bytes = sizeof(head);
	packet.b_o_s = 1;
	packet.packetno = fPacket++;
	ogg_stream_packetin(stream, &packet);
	if (_WritePages(true) != B_OK)
		return B_IO_ERROR;

	const char* vendor = "Powergram";
	std::vector<unsigned char> tags(8 + 4 + strlen(vendor) + 4);
	memcpy(tags.data(), "OpusTags", 8);
	PutLE(tags.data() + 8, (uint32)strlen(vendor), 4);
	memcpy(tags.data() + 12, vendor, strlen(vendor));
	PutLE(tags.data() + 12 + strlen(vendor), 0, 4);   // no comments
	packet = {};
	packet.packet = tags.data();
	packet.bytes = (long)tags.size();
	packet.packetno = fPacket++;
	ogg_stream_packetin(stream, &packet);
	return _WritePages(true);
}


status_t
OggOpusWriter::_WritePages(bool flush)
{
	ogg_page page;
	ogg_stream_state* stream = (ogg_stream_state*)fStream;
	while (flush ? ogg_stream_flush(stream, &page) : ogg_stream_pageout(stream, &page)) {
		if (fwrite(page.header, 1, page.header_len, fFile) != (size_t)page.header_len
			|| fwrite(page.body, 1, page.body_len, fFile) != (size_t)page.body_len)
			return B_IO_ERROR;
	}
	return B_OK;
}


status_t
OggOpusWriter::_Encode(const float* frame, bool last)
{
	unsigned char data[1500];
	opus_int32 bytes = opus_encode_float(fEncoder, frame, kFrame, data, sizeof(data));
	if (bytes < 0)
		return B_ERROR;
	fEncoded += kFrame;
	ogg_packet packet = {};
	packet.packet = data;
	packet.bytes = bytes;
	packet.packetno = fPacket++;
	packet.e_o_s = last ? 1 : 0;
	// Samples the decoder will have produced (pre-skip included); the last
	// page's says where the real sound ends.
	packet.granulepos = last ? fPreSkip + fSamples : fEncoded;
	ogg_stream_packetin((ogg_stream_state*)fStream, &packet);
	return _WritePages(last);
}


status_t
OggOpusWriter::Write(std::vector<float>& samples)
{
	size_t used = 0;
	while (samples.size() - used >= (size_t)kFrame) {
		fSamples += kFrame;
		status_t status = _Encode(samples.data() + used, false);
		if (status != B_OK)
			return status;
		used += kFrame;
	}
	samples.erase(samples.begin(), samples.begin() + used);
	return B_OK;
}


status_t
OggOpusWriter::Close(const std::vector<float>& rest)
{
	if (fFile == nullptr)
		return B_NO_INIT;
	// The last part frame, then silence until the encoder's delay is
	// covered too; the last page's granule position trims the padding.
	size_t used = 0;
	fSamples += (int64)rest.size();
	status_t status = B_OK;
	std::vector<float> frame(kFrame);
	while (status == B_OK) {
		frame.assign(kFrame, 0.0f);
		size_t take = std::min(rest.size() - used, (size_t)kFrame);
		std::copy(rest.begin() + used, rest.begin() + used + take, frame.begin());
		used += take;
		bool last = used == rest.size() && fEncoded + kFrame >= fPreSkip + fSamples;
		status = _Encode(frame.data(), last);
		if (last)
			break;
	}
	if (fclose(fFile) != 0 && status == B_OK)
		status = B_IO_ERROR;
	fFile = nullptr;
	return status;
}


VoiceRecorder::VoiceRecorder()
	:
	fLock("voice recorder")
{
}


VoiceRecorder::~VoiceRecorder()
{
	Stop(false);
}


status_t
VoiceRecorder::StartFile(const char* path)
{
	BAutolock lock(fLock);
	fPath = path;
	fResampler.reset();
	fMeter = pp::WaveformMeter();
	fMono.clear();
	fWriter.reset(new OggOpusWriter);
	status_t status = fWriter->Open(path);
	if (status != B_OK)
		fWriter.reset();
	return status;
}


status_t
VoiceRecorder::Start(const char* path, BString* problem)
{
	Stop(false);
	status_t status = StartFile(path);
	if (status != B_OK) {
		*problem = "Could not create the recording.";
		return status;
	}
#ifndef APP_COSMOE
	BMediaRoster* roster = BMediaRoster::Roster(&status);
	media_node input;
	if (roster == nullptr || (status = roster->GetAudioInput(&input)) != B_OK) {
		*problem = "No sound input was found. Check the Media preferences.";
		Stop(false);
		return status != B_OK ? status : B_ERROR;
	}
#endif
	fRecorder = new BMediaRecorder("Powergram voice message", B_MEDIA_RAW_AUDIO);
	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	if ((status = fRecorder->InitCheck()) == B_OK)
		status = fRecorder->SetHooks(_Process, nullptr, this);
	if (status == B_OK) {
		fRecorder->SetAcceptedFormat(format);
#ifdef APP_COSMOE
		// Hokku has no media nodes: this opens the default sound input.
		status = fRecorder->Connect(format);
#else
		status = fRecorder->Connect(input, nullptr, &format);
#endif
	}
	if (status == B_OK)
		status = fRecorder->Start();
	if (status != B_OK) {
		*problem = "Could not start recording from the sound input: ";
		*problem << strerror(status);
		Stop(false);
	}
#ifndef APP_COSMOE
	roster->ReleaseNode(input);
#endif
	return status;
}


status_t
VoiceRecorder::Stop(bool keep, int32* seconds, std::string* levels)
{
	if (fRecorder != nullptr) {
		fRecorder->Stop(true);
		fRecorder->Disconnect();
		fRecorder->SetHooks(nullptr, nullptr, nullptr);
		delete fRecorder;
		fRecorder = nullptr;
	}
	BAutolock lock(fLock);
	if (fWriter == nullptr)
		return B_NO_INIT;
	status_t status = B_OK;
	if (keep) {
		status = fWriter->Write(fMono);
		if (status == B_OK)
			status = fWriter->Close(fMono);
		if (seconds != nullptr)
			*seconds = (int32)(fMeter.Seconds() + 0.5);
		if (levels != nullptr)
			*levels = fMeter.Levels();
	}
	fWriter.reset();
	if (!keep || status != B_OK)
		unlink(fPath.c_str());
	return status;
}


double
VoiceRecorder::Seconds()
{
	BAutolock lock(fLock);
	return fMeter.Seconds();
}


float
VoiceRecorder::TakeLevel()
{
	BAutolock lock(fLock);
	return fMeter.TakeLevel();
}


void
VoiceRecorder::_Process(void* cookie, int64, void* data, size_t size,
	const media_format& format)
{
	static_cast<VoiceRecorder*>(cookie)->Feed(data, size, format);
}


void
VoiceRecorder::Feed(const void* data, size_t size, const media_format& format)
{
	const media_raw_audio_format& raw = format.u.raw_audio;
	int channels = std::max<int>(1, (int)raw.channel_count);
	size_t sampleSize = raw.format & media_raw_audio_format::B_AUDIO_SIZE_MASK;
	if (sampleSize == 0 || raw.frame_rate <= 0)
		return;
	size_t count = size / sampleSize;
	BAutolock lock(fLock);
	if (fWriter == nullptr)
		return;
	fFloat.resize(count);
	for (size_t i = 0; i < count; i++) {
		switch (raw.format) {
			case media_raw_audio_format::B_AUDIO_FLOAT:
				fFloat[i] = ((const float*)data)[i];
				break;
			case media_raw_audio_format::B_AUDIO_INT:
				fFloat[i] = ((const int32*)data)[i] / 2147483648.0f;
				break;
			case media_raw_audio_format::B_AUDIO_SHORT:
				fFloat[i] = ((const int16*)data)[i] / 32768.0f;
				break;
			case media_raw_audio_format::B_AUDIO_CHAR:
				fFloat[i] = ((const int8*)data)[i] / 128.0f;
				break;
			case media_raw_audio_format::B_AUDIO_UCHAR:
				fFloat[i] = (((const uint8*)data)[i] - 128) / 128.0f;
				break;
			default:
				return;
		}
	}
	if (fResampler == nullptr)
		fResampler.reset(new pp::Resampler(raw.frame_rate, channels));
	size_t before = fMono.size();
	fResampler->Process(fFloat.data(), count / channels, fMono);
	fMeter.Add(fMono.data() + before, fMono.size() - before);
	fWriter->Write(fMono);
}
