// SPDX-License-Identifier: GPL-3.0-or-later
#include "VideoPlayback.h"

#include <Bitmap.h>
#include <Entry.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <SoundPlayer.h>

#include <algorithm>
#include <cstring>

#include "Messages.h"


VideoPlayback::VideoPlayback(BMessenger target)
	:
	fTarget(target),
	fFrameLock("video frame"),
	fVideoLock("video track"),
	fAudioLock("audio track"),
	fClockLock("video clock")
{
}


VideoPlayback::~VideoPlayback()
{
	Close();
}


status_t
VideoPlayback::Open(const char* path, bool loop, bool muted)
{
	Close();
	entry_ref ref;
	status_t status = get_ref_for_path(path, &ref);
	if (status != B_OK)
		return status;
	fFile = new BMediaFile(&ref);
	if ((status = fFile->InitCheck()) != B_OK) {
		Close();
		return status;
	}
	fLoop = loop;

	// The first video track that decodes to RGB32, and the first audio
	// track that decodes to floats (as voice messages do).
	media_format videoFormat;
	media_format audioFormat;
	for (int32 i = 0; i < fFile->CountTracks(); i++) {
		BMediaTrack* track = fFile->TrackAt(i);
		if (track == nullptr)
			continue;
		media_format encoded;
		track->EncodedFormat(&encoded);
		bool keep = false;
		if (encoded.type == B_MEDIA_ENCODED_VIDEO && fVideoTrack == nullptr) {
			videoFormat = media_format();
			videoFormat.type = B_MEDIA_RAW_VIDEO;
			videoFormat.u.raw_video = media_raw_video_format::wildcard;
			videoFormat.u.raw_video.display.format = B_RGB32;
			if (track->DecodedFormat(&videoFormat) == B_OK
				&& videoFormat.type == B_MEDIA_RAW_VIDEO
				&& (videoFormat.u.raw_video.display.format == B_RGB32
					|| videoFormat.u.raw_video.display.format == B_RGBA32)) {
				fVideoTrack = track;
				keep = true;
			}
		} else if (encoded.type == B_MEDIA_ENCODED_AUDIO && fAudioTrack == nullptr
			&& !muted) {
			audioFormat = media_format();
			audioFormat.type = B_MEDIA_RAW_AUDIO;
			audioFormat.u.raw_audio = media_raw_audio_format::wildcard;
			audioFormat.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
			if (track->DecodedFormat(&audioFormat) == B_OK
				&& audioFormat.type == B_MEDIA_RAW_AUDIO
				&& audioFormat.u.raw_audio.format == media_raw_audio_format::B_AUDIO_FLOAT) {
				fAudioTrack = track;
				keep = true;
			}
		}
		if (!keep)
			fFile->ReleaseTrack(track);
	}
	if (fVideoTrack == nullptr) {
		Close();
		return B_MEDIA_BAD_FORMAT;
	}

	const media_video_display_info& display = videoFormat.u.raw_video.display;
	BRect bounds(0, 0, display.line_width - 1, display.line_count - 1);
	int32 bytesPerRow = display.bytes_per_row > 0
		? (int32)display.bytes_per_row : B_ANY_BYTES_PER_ROW;
	fDecoding = new BBitmap(bounds, 0, B_RGB32, bytesPerRow);
	fShown = new BBitmap(bounds, 0, B_RGB32, bytesPerRow);
	if (fDecoding->InitCheck() != B_OK || fShown->InitCheck() != B_OK
		|| display.line_width == 0 || display.line_count == 0) {
		Close();
		return B_NO_MEMORY;
	}
	memset(fShown->Bits(), 0, fShown->BitsLength());
	fDuration = fVideoTrack->Duration();
	float rate = videoFormat.u.raw_video.field_rate;
	int64 count = fVideoTrack->CountFrames();
	if (rate > 0 && rate < 1000)
		fFrameTime = 1e6 / rate;
	else if (count > 0 && fDuration > 0)
		fFrameTime = (double)fDuration / count;
	else
		fFrameTime = 40000;

	if (fAudioTrack != nullptr) {
		media_raw_audio_format& raw = audioFormat.u.raw_audio;
		fAudioFrameSize = sizeof(float) * std::max<uint32>(1, raw.channel_count);
		fAudioRate = raw.frame_rate;
		fAudioPosition = 0;
		fAudioBufferSize = std::max<size_t>(raw.buffer_size, 65536);
		fAudioBuffer = new char[fAudioBufferSize];
		fAudioAvailable = fAudioUsed = 0;
		fAudioEnded = false;
		fAudioSkip = 0;
		fDuration = std::max(fDuration, fAudioTrack->Duration());
		fPlayer = new BSoundPlayer(&raw, "Powergram video", _FillAudio, nullptr, this);
		if (fPlayer->InitCheck() == B_OK) {
			fPlayer->Start();
			fPlayer->SetHasData(true);
		} else {
			// No sound output: the picture plays anyway.
			delete fPlayer;
			fPlayer = nullptr;
			fFile->ReleaseTrack(fAudioTrack);
			fAudioTrack = nullptr;
		}
	}

	fQuit = false;
	fPlaying = false;
	fEnded = false;
	fGeneration = 0;
	_SetClock(0);
	fWake = create_sem(0, "video wake");
	fNeedFrame = true;
	fThread = spawn_thread(_VideoThread, "video decoder", B_DISPLAY_PRIORITY, this);
	if (fWake < 0 || fThread < 0) {
		Close();
		return B_NO_MORE_THREADS;
	}
	resume_thread(fThread);
	return B_OK;
}


void
VideoPlayback::Close()
{
	if (fThread >= 0) {
		fQuit = true;
		_Wake();
		status_t result;
		wait_for_thread(fThread, &result);
		fThread = -1;
	}
	if (fWake >= 0) {
		delete_sem(fWake);
		fWake = -1;
	}
	if (fPlayer != nullptr) {
		fPlayer->Stop();
		delete fPlayer;
		fPlayer = nullptr;
	}
	if (fFile != nullptr) {
		if (fVideoTrack != nullptr)
			fFile->ReleaseTrack(fVideoTrack);
		if (fAudioTrack != nullptr)
			fFile->ReleaseTrack(fAudioTrack);
		delete fFile;
	}
	fFile = nullptr;
	fVideoTrack = nullptr;
	fAudioTrack = nullptr;
	fFrameLock.Lock();
	delete fDecoding;
	delete fShown;
	fDecoding = fShown = nullptr;
	fFrameLock.Unlock();
	delete[] fAudioBuffer;
	fAudioBuffer = nullptr;
	fPlaying = false;
	fDuration = 0;
	fFramePosted = false;
}


void
VideoPlayback::Play()
{
	if (fFile == nullptr || fPlaying)
		return;
	if (fEnded) {
		fEnded = false;
		Seek(0);
	}
	fClockLock.Lock();
	fClockStart = system_time();
	fPlaying = true;
	fClockLock.Unlock();
	_Wake();
}


void
VideoPlayback::Pause()
{
	if (!fPlaying)
		return;
	fClockLock.Lock();
	fClockBase = _Clock();
	fPlaying = false;
	fClockLock.Unlock();
	_Wake();
}


void
VideoPlayback::Seek(bigtime_t time)
{
	if (fFile == nullptr)
		return;
	time = std::max<bigtime_t>(0, std::min(time, fDuration));
	fVideoLock.Lock();
	fAudioLock.Lock();
	_SeekTracks(time);
	fAudioLock.Unlock();
	fVideoLock.Unlock();
	fEnded = false;
	_Wake();
}


// With both track locks held.
void
VideoPlayback::_SeekTracks(bigtime_t time)
{
	// The key frame at or before `time`: decoding starts there.
	bigtime_t at = time;
	if (fVideoTrack->SeekToTime(&at, B_MEDIA_SEEK_CLOSEST_BACKWARD) != B_OK)
		at = time;
	if (fAudioTrack != nullptr) {
		// Sound seeks to a packet before `at` (up to half a second on
		// R1/beta6): what's before `at` is decoded and dropped.
		bigtime_t audioAt = at;
		if (fAudioTrack->SeekToTime(&audioAt, B_MEDIA_SEEK_CLOSEST_BACKWARD) != B_OK
			|| audioAt > at)
			audioAt = at;
		fAudioAvailable = fAudioUsed = 0;
		fAudioEnded = false;
		fAudioSkip = (int64)((at - audioAt) * (double)fAudioRate / 1e6);
		fAudioPosition = at;
	}
	_SetClock(at);
	fGeneration++;
	fNeedFrame = true;
}


bigtime_t
VideoPlayback::Position()
{
	fClockLock.Lock();
	bigtime_t now = _Clock();
	fClockLock.Unlock();
	return std::max<bigtime_t>(0, std::min(now, fDuration));
}


BSize
VideoPlayback::VideoSize() const
{
	if (fShown == nullptr)
		return BSize(0, 0);
	BRect bounds = fShown->Bounds();
	return BSize(bounds.Width() + 1, bounds.Height() + 1);
}


// With fClockLock held (or from a lone reader that tolerates a race).
bigtime_t
VideoPlayback::_Clock()
{
	if (!fPlaying)
		return fClockBase;
	return fClockBase + system_time() - fClockStart;
}


void
VideoPlayback::_SetClock(bigtime_t time)
{
	fClockLock.Lock();
	fClockBase = time;
	fClockStart = system_time();
	fClockLock.Unlock();
}


void
VideoPlayback::_ReachedEnd()
{
	if (fLoop) {
		fVideoLock.Lock();
		fAudioLock.Lock();
		_SeekTracks(0);
		fAudioLock.Unlock();
		fVideoLock.Unlock();
		return;
	}
	fClockLock.Lock();
	fClockBase = fDuration;
	fPlaying = false;
	fClockLock.Unlock();
	fEnded = true;
	fTarget.SendMessage(kMsgPlaybackEnded);
}


status_t
VideoPlayback::_VideoThread(void* cookie)
{
	((VideoPlayback*)cookie)->_VideoLoop();
	return B_OK;
}


void
VideoPlayback::_VideoLoop()
{
	while (!fQuit) {
		if (!fPlaying && !fNeedFrame) {
			acquire_sem(fWake);
			continue;
		}
		fVideoLock.Lock();
		int32 generation = fGeneration;
		bool forced = fNeedFrame.exchange(false);
		// When the frame is due, from its number and the frame rate: on
		// R1/beta6 the header's start_time and the track's CurrentTime()
		// count in the file's own time units, not microseconds.
		bigtime_t due = (bigtime_t)(fVideoTrack->CurrentFrame() * fFrameTime);
		int64 frames = 1;
		media_header header;
		status_t status = fVideoTrack->ReadFrames(fDecoding->Bits(), &frames, &header);
		fVideoLock.Unlock();
		if (status != B_OK || frames <= 0) {
			if (fPlaying)
				_ReachedEnd();
			else
				fNeedFrame = false;
			continue;
		}

		// Wait until the frame is due (a seek or a pause interrupts), and
		// skip it when it's already well behind the clock.
		bool late = false;
		while (!fQuit && fPlaying && fGeneration == generation) {
			fClockLock.Lock();
			bigtime_t wait = due - _Clock();
			fClockLock.Unlock();
			if (wait <= 0) {
				late = wait < -100000;
				break;
			}
			acquire_sem_etc(fWake, 1, B_RELATIVE_TIMEOUT, std::min<bigtime_t>(wait, 50000));
		}
		if (fQuit || fGeneration != generation)
			continue;
		// After a seek, frames up to the target are decoded but not shown.
		fClockLock.Lock();
		bool behind = !fPlaying && !forced && due < _Clock();
		fClockLock.Unlock();
		if ((late && !forced) || behind)
			continue;

		fFrameLock.Lock();
		std::swap(fDecoding, fShown);
		fFrameLock.Unlock();
		if (!fFramePosted.exchange(true))
			fTarget.SendMessage(kMsgPlaybackFrame);
	}
}


// The sound player's thread: decoded sound while playing, silence else.
void
VideoPlayback::_FillAudio(void* cookie, void* buffer, size_t size,
	const media_raw_audio_format&)
{
	VideoPlayback* self = (VideoPlayback*)cookie;
	char* out = (char*)buffer;
	if (!self->fPlaying || self->fAudioLock.LockWithTimeout(20000) != B_OK) {
		memset(out, 0, size);
		return;
	}
	while (size > 0) {
		if (self->fAudioUsed >= self->fAudioAvailable) {
			int64 frames = 0;
			if (self->fAudioEnded
				|| self->fAudioTrack->ReadFrames(self->fAudioBuffer, &frames) != B_OK
				|| frames <= 0) {
				self->fAudioEnded = true;
				memset(out, 0, size);
				break;
			}
			self->fAudioAvailable = std::min<size_t>(frames * self->fAudioFrameSize,
				self->fAudioBufferSize);
			self->fAudioUsed = 0;
			if (self->fAudioSkip > 0) {
				int64 drop = std::min<int64>(self->fAudioSkip,
					self->fAudioAvailable / self->fAudioFrameSize);
				self->fAudioUsed = drop * self->fAudioFrameSize;
				self->fAudioSkip -= drop;
				continue;
			}
		}
		size_t n = std::min(size, self->fAudioAvailable - self->fAudioUsed);
		memcpy(out, self->fAudioBuffer + self->fAudioUsed, n);
		out += n;
		size -= n;
		self->fAudioUsed += n;
		if (self->fAudioRate > 0)
			self->fAudioPosition += (bigtime_t)(n / self->fAudioFrameSize * 1e6 / self->fAudioRate);
	}
	self->fAudioLock.Unlock();
}
