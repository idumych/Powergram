// SPDX-License-Identifier: GPL-3.0-or-later
// Stand-ins for the parts of the app that need Haiku's Media Kit, for systems
// built without it (Haiku arm64 so far). Voice messages open in the system's
// player, recording says why it can't, the viewer shows photos only, and
// files to send are photos or plain files.
#include <Beep.h>
#include <Bitmap.h>
#include <Entry.h>
#include <TranslationUtils.h>

#include "MediaProbe.h"
#include "VideoPlayback.h"
#include "VoicePlayer.h"
#include "VoiceRecorder.h"

// The new-message sound (Beep.h: in libmedia, though declared with libbe's)
status_t system_beep(const char*) { return B_NOT_SUPPORTED; }
status_t add_system_beep_event(const char*, uint32) { return B_NOT_SUPPORTED; }


status_t VoicePlayer::Play(const char*) { return B_NOT_SUPPORTED; }
void VoicePlayer::Stop() {}
bool VoicePlayer::IsPlaying() const { return false; }
float VoicePlayer::Progress() const { return 0; }


VoiceRecorder::VoiceRecorder() {}
VoiceRecorder::~VoiceRecorder() {}
OggOpusWriter::~OggOpusWriter() {}

status_t
VoiceRecorder::Start(const char*, BString* problem)
{
	*problem = "Recording voice messages needs Haiku's Media Kit, which this system "
		"doesn't have yet.";
	return B_NOT_SUPPORTED;
}

status_t VoiceRecorder::Stop(bool, int32*, std::string*) { return B_NOT_SUPPORTED; }
double VoiceRecorder::Seconds() { return 0; }
float VoiceRecorder::TakeLevel() { return 0; }


VideoPlayback::VideoPlayback(BMessenger target) : fTarget(target) {}
VideoPlayback::~VideoPlayback() {}
status_t VideoPlayback::Open(const char*, bool, bool) { return B_NOT_SUPPORTED; }
void VideoPlayback::Close() {}
void VideoPlayback::Play() {}
void VideoPlayback::Pause() {}
void VideoPlayback::Seek(bigtime_t) {}
bigtime_t VideoPlayback::Position() { return 0; }
BSize VideoPlayback::VideoSize() const { return BSize(0, 0); }


pp::OutgoingFile
ProbeFile(const char* path)
{
	pp::OutgoingFile file;
	file.path = path;
	entry_ref ref;
	if (get_ref_for_path(path, &ref) != B_OK)
		return file;
	if (BBitmap* image = BTranslationUtils::GetBitmap(&ref)) {
		BRect bounds = image->Bounds();
		delete image;
		file.kind = pp::OutgoingFile::kPhoto;
		file.width = bounds.IntegerWidth() + 1;
		file.height = bounds.IntegerHeight() + 1;
	}
	return file;
}
