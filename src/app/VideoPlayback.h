// SPDX-License-Identifier: GPL-3.0-or-later
// Plays a video file with the Media Kit for the media viewer: a thread
// decodes frames to RGB32 and shows each when it's due, while a
// BSoundPlayer plays the sound. Both follow one clock, which pausing
// stops and seeking moves.
#pragma once

#include <Locker.h>
#include <Messenger.h>
#include <OS.h>
#include <Size.h>

#include <atomic>

class BBitmap;
class BMediaFile;
class BMediaTrack;
class BSoundPlayer;
struct media_raw_audio_format;

class VideoPlayback {
public:
	// `target` gets kMsgPlaybackFrame when a new frame is ready and
	// kMsgPlaybackEnded when the end is reached (never when looping).
	explicit VideoPlayback(BMessenger target);
	~VideoPlayback();

	// Opens `path` and shows its first frame, paused. `loop` starts again
	// at the end; `muted` leaves the sound out.
	status_t Open(const char* path, bool loop, bool muted);
	void Close();

	void Play();
	void Pause();
	bool IsPlaying() const { return fPlaying; }
	void Seek(bigtime_t time);
	bigtime_t Position();
	bigtime_t Duration() const { return fDuration; }
	BSize VideoSize() const;
	bool HasAudio() const { return fAudioTrack != nullptr; }
	// Where the sound handed to the sound player has got to (for tests:
	// it should stay with Position()).
	bigtime_t AudioPosition() const { return fAudioPosition; }

	// The frame to draw, between LockFrame and UnlockFrame (may be null).
	bool LockFrame() { return fFrameLock.Lock(); }
	const BBitmap* Frame() const { return fShown; }
	void UnlockFrame() { fFrameLock.Unlock(); }
	// The window has drawn the frame it was told about.
	void FrameDrawn() { fFramePosted = false; }

private:
	static status_t _VideoThread(void* cookie);
	void _VideoLoop();
	static void _FillAudio(void* cookie, void* buffer, size_t size,
		const media_raw_audio_format& format);
	bigtime_t _Clock();
	void _SetClock(bigtime_t time);
	void _SeekTracks(bigtime_t time);
	void _ReachedEnd();
	void _Wake() { release_sem(fWake); }

	BMessenger fTarget;
	BMediaFile* fFile = nullptr;
	BMediaTrack* fVideoTrack = nullptr;
	BMediaTrack* fAudioTrack = nullptr;
	BSoundPlayer* fPlayer = nullptr;
	bigtime_t fDuration = 0;
	double fFrameTime = 40000;     // microseconds a frame is shown
	bool fLoop = false;

	// Decoding into one bitmap while the other is shown.
	BBitmap* fDecoding = nullptr;
	BBitmap* fShown = nullptr;
	BLocker fFrameLock;
	std::atomic<bool> fFramePosted{false};

	// The video track, the audio track and the clock, each with its lock;
	// seeking takes all of them.
	BLocker fVideoLock;
	BLocker fAudioLock;
	BLocker fClockLock;
	bigtime_t fClockBase = 0;      // media time at fClockStart
	bigtime_t fClockStart = 0;     // system time it was taken, while playing
	std::atomic<bool> fPlaying{false};
	std::atomic<bool> fQuit{false};
	std::atomic<bool> fNeedFrame{false};   // show one even while paused
	std::atomic<int32> fGeneration{0};     // bumped by every seek
	bool fEnded = false;
	sem_id fWake = -1;
	thread_id fThread = -1;

	// Sound decoded, waiting to be played.
	char* fAudioBuffer = nullptr;
	size_t fAudioBufferSize = 0;
	size_t fAudioAvailable = 0;
	size_t fAudioUsed = 0;
	size_t fAudioFrameSize = 0;
	float fAudioRate = 0;
	std::atomic<bigtime_t> fAudioPosition{0};
	int64 fAudioSkip = 0;          // frames to drop after a seek
	bool fAudioEnded = false;
};
