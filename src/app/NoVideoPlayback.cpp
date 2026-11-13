// SPDX-License-Identifier: GPL-3.0-or-later
// The media viewer's video playback, for systems whose Media Kit decodes no
// video: Haiku arm64 (no Media Kit yet) and Linux (Hokku decodes audio
// only). The viewer then opens videos in the system's player.
#include "VideoPlayback.h"

VideoPlayback::VideoPlayback(BMessenger target) : fTarget(target) {}
VideoPlayback::~VideoPlayback() {}
status_t VideoPlayback::Open(const char*, bool, bool) { return B_NOT_SUPPORTED; }
void VideoPlayback::Close() {}
void VideoPlayback::Play() {}
void VideoPlayback::Pause() {}
void VideoPlayback::Seek(bigtime_t) {}
bigtime_t VideoPlayback::Position() { return 0; }
BSize VideoPlayback::VideoSize() const { return BSize(0, 0); }
