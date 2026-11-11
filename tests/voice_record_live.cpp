// SPDX-License-Identifier: GPL-3.0-or-later
// Records two seconds from the default sound input through BMediaRecorder,
// as the app does, and reports what came out. Not run by make test: it
// needs a sound input (a VM's silent one will do).
#include <Application.h>
#include <MediaFile.h>
#include <MediaTrack.h>

#include <cstdio>
#include <unistd.h>

#include "../src/app/VoiceRecorder.h"

int
main(int argc, char** argv)
{
	BApplication app("application/x-vnd.powergram-voice-live");
	const char* path = argc > 1 ? argv[1] : "/tmp/voice_record_live.ogg";
	VoiceRecorder recorder;
	BString problem;
	status_t status = recorder.Start(path, &problem);
	if (status != B_OK) {
		printf("start failed: %s (%s)\n", problem.String(), strerror(status));
		return 1;
	}
	for (int i = 0; i < 10; i++) {
		usleep(200000);
		printf("%.2f s, level %.3f\n", recorder.Seconds(), recorder.TakeLevel());
	}
	int32 seconds = 0;
	std::string levels;
	status = recorder.Stop(true, &seconds, &levels);
	printf("stop: %s, %d s, %zu levels\n", strerror(status), (int)seconds, levels.size());
	entry_ref ref;
	get_ref_for_path(path, &ref);
	BMediaFile file(&ref);
	if (file.InitCheck() == B_OK && file.CountTracks() == 1) {
		BMediaTrack* track = file.TrackAt(0);
		printf("read back: %.3f s\n", track->Duration() / 1e6);
		file.ReleaseTrack(track);
	} else
		printf("read back failed\n");
	return status == B_OK && seconds == 2 ? 0 : 1;
}
