// SPDX-License-Identifier: GPL-3.0-or-later
// Plays a voice message (Telegram sends OGG/Opus) with the Media Kit:
// the track is decoded to float samples straight into a BSoundPlayer.
#pragma once

#include <SupportDefs.h>

class BMediaFile;
class BMediaTrack;
class BSoundPlayer;
struct media_raw_audio_format;

class VoicePlayer {
public:
	~VoicePlayer() { Stop(); }

	// Stops what was playing and starts `path`.
	status_t Play(const char* path);
	void Stop();
	// Still playing: false once it's played to the end (or stopped).
	bool IsPlaying() const;
	float Progress() const;   // 0..1

private:
	static void _Fill(void* cookie, void* buffer, size_t size,
		const media_raw_audio_format& format);

	BMediaFile* fFile = nullptr;
	BMediaTrack* fTrack = nullptr;
	BSoundPlayer* fPlayer = nullptr;
	char* fBuffer = nullptr;     // one decoded chunk
	size_t fBufferSize = 0;
	size_t fAvailable = 0;       // bytes of it decoded
	size_t fUsed = 0;            // and already played
	size_t fFrameSize = 0;
	int64 fFrames = 0;
	volatile int64 fPlayed = 0;
	volatile bool fEnded = false;
};
