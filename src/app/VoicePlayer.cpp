// SPDX-License-Identifier: GPL-3.0-or-later
#include "VoicePlayer.h"

#include <Entry.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <SoundPlayer.h>

#include <algorithm>
#include <cstring>


status_t
VoicePlayer::Play(const char* path)
{
	Stop();
	entry_ref ref;
	status_t status = get_ref_for_path(path, &ref);
	if (status != B_OK)
		return status;
	fFile = new BMediaFile(&ref);
	if ((status = fFile->InitCheck()) != B_OK) {
		Stop();
		return status;
	}

	// The first track that decodes to raw audio, as floats: silence is
	// then all zero bytes, whatever the channel count.
	media_format format;
	for (int32 i = 0; i < fFile->CountTracks() && fTrack == nullptr; i++) {
		BMediaTrack* track = fFile->TrackAt(i);
		if (track == nullptr)
			continue;
		format = media_format();
		format.type = B_MEDIA_RAW_AUDIO;
		format.u.raw_audio = media_raw_audio_format::wildcard;
		format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
		if (track->DecodedFormat(&format) == B_OK && format.type == B_MEDIA_RAW_AUDIO
			&& format.u.raw_audio.format == media_raw_audio_format::B_AUDIO_FLOAT)
			fTrack = track;
		else
			fFile->ReleaseTrack(track);
	}
	if (fTrack == nullptr) {
		Stop();
		return B_MEDIA_BAD_FORMAT;
	}

	media_raw_audio_format& raw = format.u.raw_audio;
	fFrameSize = sizeof(float) * std::max<uint32>(1, raw.channel_count);
	fBufferSize = std::max<size_t>(raw.buffer_size, 65536);
	fBuffer = new char[fBufferSize];
	fAvailable = fUsed = 0;
	fFrames = fTrack->CountFrames();
	fPlayed = 0;
	fEnded = false;

	fPlayer = new BSoundPlayer(&raw, "Powergram voice message", _Fill, nullptr, this);
	if ((status = fPlayer->InitCheck()) != B_OK) {
		Stop();
		return status;
	}
	fPlayer->Start();
	fPlayer->SetHasData(true);
	return B_OK;
}


void
VoicePlayer::Stop()
{
	if (fPlayer != nullptr) {
		fPlayer->Stop();
		delete fPlayer;
		fPlayer = nullptr;
	}
	if (fFile != nullptr) {
		if (fTrack != nullptr)
			fFile->ReleaseTrack(fTrack);
		delete fFile;
	}
	fFile = nullptr;
	fTrack = nullptr;
	delete[] fBuffer;
	fBuffer = nullptr;
	fEnded = true;
}


bool
VoicePlayer::IsPlaying() const
{
	return fPlayer != nullptr && !fEnded;
}


float
VoicePlayer::Progress() const
{
	if (fFrames <= 0)
		return 0;
	return std::min(1.0f, (float)fPlayed / fFrames);
}


// The sound player's thread: decoded chunks into its buffers.
void
VoicePlayer::_Fill(void* cookie, void* buffer, size_t size, const media_raw_audio_format&)
{
	VoicePlayer* player = (VoicePlayer*)cookie;
	char* out = (char*)buffer;
	while (size > 0) {
		if (player->fUsed >= player->fAvailable) {
			int64 frames = 0;
			if (player->fEnded
				|| player->fTrack->ReadFrames(player->fBuffer, &frames) != B_OK
				|| frames <= 0) {
				player->fEnded = true;
				memset(out, 0, size);
				return;
			}
			player->fAvailable = std::min<size_t>(frames * player->fFrameSize,
				player->fBufferSize);
			player->fUsed = 0;
		}
		size_t n = std::min(size, player->fAvailable - player->fUsed);
		memcpy(out, player->fBuffer + player->fUsed, n);
		out += n;
		size -= n;
		player->fUsed += n;
		player->fPlayed += n / player->fFrameSize;
	}
}
