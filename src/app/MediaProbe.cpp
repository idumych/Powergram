// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaProbe.h"

#include <Bitmap.h>
#include <Entry.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <Mime.h>
#include <Node.h>
#include <NodeInfo.h>
#include <TranslationUtils.h>

#include <cmath>

// The first video track's display size and the file's length; false if
// the Media Kit finds no video in it.
static bool
ProbeVideo(const entry_ref& ref, pp::OutgoingFile* file)
{
	BMediaFile media(&ref);
	if (media.InitCheck() != B_OK)
		return false;
	for (int32 i = 0; i < media.CountTracks(); i++) {
		BMediaTrack* track = media.TrackAt(i);
		media_format format;
		if (track == nullptr || track->EncodedFormat(&format) != B_OK
			|| format.type != B_MEDIA_ENCODED_VIDEO) {
			if (track != nullptr)
				media.ReleaseTrack(track);
			continue;
		}
		const media_video_display_info& display = format.u.encoded_video.output.display;
		file->width = (int32)display.line_width;
		file->height = (int32)display.line_count;
		file->duration = (int32)lround(track->Duration() / 1e6);
		media.ReleaseTrack(track);
		return file->width > 0 && file->height > 0;
	}
	return false;
}


pp::OutgoingFile
ProbeFile(const char* path)
{
	pp::OutgoingFile file;
	file.path = path;
	entry_ref ref;
	if (get_ref_for_path(path, &ref) != B_OK)
		return file;
	// Images Haiku can read go as photos, with their size.
	if (BBitmap* image = BTranslationUtils::GetBitmap(&ref)) {
		BRect bounds = image->Bounds();
		delete image;
		file.kind = pp::OutgoingFile::kPhoto;
		file.width = bounds.IntegerWidth() + 1;
		file.height = bounds.IntegerHeight() + 1;
		return file;
	}
	// MP4 and QuickTime videos as videos; the type from the file system,
	// else guessed from the name.
	char type[B_MIME_TYPE_LENGTH] = "";
	BNode node(&ref);
	BNodeInfo info(&node);
	if (info.GetType(type) != B_OK || type[0] == '\0') {
		BMimeType guess;
		if (BMimeType::GuessMimeType(path, &guess) == B_OK && guess.Type() != nullptr)
			strlcpy(type, guess.Type(), sizeof(type));
	}
	if (pp::IsPlayableVideo(type, path) && ProbeVideo(ref, &file))
		file.kind = pp::OutgoingFile::kVideo;
	return file;
}
