// SPDX-License-Identifier: GPL-3.0-or-later
// The media viewer's video playback on Haiku: frames arrive while playing
// and stop while paused, the clock follows, seeking moves it, the end is
// reported, GIFs loop, and files that aren't videos are refused.
//   video_playback_test tests/data
#include <Application.h>
#include <Bitmap.h>
#include <Looper.h>
#include <OS.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>

#include "../src/app/Messages.h"
#include "../src/app/VideoPlayback.h"

static int sFailures = 0;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #condition); \
		sFailures++; \
	} \
} while (0)

// Counts what the playback tells its window.
class Counter : public BLooper {
public:
	Counter() : BLooper("counter") {}
	void MessageReceived(BMessage* message) override
	{
		if (message->what == kMsgPlaybackFrame) {
			frames++;
			if (playback != nullptr)
				playback->FrameDrawn();
		} else if (message->what == kMsgPlaybackEnded)
			ended++;
		else
			BLooper::MessageReceived(message);
	}
	std::atomic<int> frames{0};
	std::atomic<int> ended{0};
	VideoPlayback* playback = nullptr;
};


static double
Seconds(bigtime_t time)
{
	return time / 1e6;
}


// Whether the shown frame has something other than black in it.
static bool
FrameHasPicture(VideoPlayback& playback)
{
	bool picture = false;
	playback.LockFrame();
	const BBitmap* frame = playback.Frame();
	if (frame != nullptr) {
		const uint8* bits = (const uint8*)frame->Bits();
		for (int32 i = 0; i < frame->BitsLength() && !picture; i += 4)
			picture = bits[i] > 16 || bits[i + 1] > 16 || bits[i + 2] > 16;
	}
	playback.UnlockFrame();
	return picture;
}


int
main(int argc, char** argv)
{
	std::string dir = argc > 1 ? argv[1] : "tests/data";
	BApplication app("application/x-vnd.powergram-video-test");
	Counter* counter = new Counter();
	counter->Run();
	VideoPlayback playback{BMessenger(counter)};
	counter->playback = &playback;

	// A video with sound: size, length, and the first frame while paused.
	status_t status = playback.Open((dir + "/clip-sound.mp4").c_str(), false, false);
	CHECK(status == B_OK);
	BSize size = playback.VideoSize();
	printf("clip-sound.mp4: %gx%g, %.2f s, sound %s\n", size.width, size.height,
		Seconds(playback.Duration()), playback.HasAudio() ? "playing" : "unavailable");
	CHECK(size.width == 480 && size.height == 270);
	CHECK(fabs(Seconds(playback.Duration()) - 8.0) < 0.2);
	snooze(300000);
	CHECK(counter->frames >= 1);
	CHECK(FrameHasPicture(playback));
	CHECK(playback.Position() == 0 && !playback.IsPlaying());

	// Playing: about 25 frames a second, the clock with the wall clock.
	int before = counter->frames;
	playback.Play();
	snooze(2000000);
	double position = Seconds(playback.Position());
	int shown = counter->frames - before;
	printf("after 2 s playing: at %.2f s, %d frames shown\n", position, shown);
	CHECK(fabs(position - 2.0) < 0.25);
	if (playback.HasAudio()) {
		// The sound handed out runs a buffer or so ahead of the picture.
		double ahead = Seconds(playback.AudioPosition()) - position;
		printf("sound handed out up to %.2f s (%+.3f s from the picture)\n",
			Seconds(playback.AudioPosition()), ahead);
		CHECK(ahead > -0.05 && ahead < 0.25);
	}
	CHECK(shown >= 35 && shown <= 55);

	// Paused: no frames, the clock stopped.
	playback.Pause();
	snooze(100000);
	before = counter->frames;
	bigtime_t pausedAt = playback.Position();
	bigtime_t soundAt = playback.AudioPosition();
	snooze(700000);
	CHECK(playback.AudioPosition() == soundAt);   // silence while paused
	printf("paused: %d frames in 0.7 s, clock moved %.3f s\n", counter->frames - before,
		Seconds(playback.Position() - pausedAt));
	CHECK(counter->frames == before);
	CHECK(playback.Position() == pausedAt);

	// Seeking while paused shows the frame there (key frames every second).
	before = counter->frames;
	playback.Seek(5000000);
	snooze(300000);
	printf("seek to 5 s: at %.2f s, %d new frame(s)\n", Seconds(playback.Position()),
		counter->frames - before);
	CHECK(fabs(Seconds(playback.Position()) - 5.0) < 0.05);
	if (playback.HasAudio())
		CHECK(fabs(Seconds(playback.AudioPosition()) - 5.0) < 0.1);
	CHECK(counter->frames == before + 1);

	// Playing on to the end: reported once, and stopped there; sound and
	// picture together after the seek.
	playback.Play();
	bigtime_t start = system_time();
	snooze(1000000);
	if (playback.HasAudio()) {
		double ahead = Seconds(playback.AudioPosition() - playback.Position());
		printf("1 s after the seek: picture at %.2f s, sound %+.3f s\n",
			Seconds(playback.Position()), ahead);
		CHECK(ahead > -0.05 && ahead < 0.25);
	}
	while (counter->ended == 0 && system_time() - start < 6000000)
		snooze(50000);
	double took = Seconds(system_time() - start);
	printf("end reached after %.2f s (3 s expected), at %.2f s\n", took,
		Seconds(playback.Position()));
	CHECK(counter->ended == 1);
	CHECK(fabs(took - 3.0) < 0.4);
	CHECK(!playback.IsPlaying() && playback.Position() == playback.Duration());

	// Play after the end starts again.
	playback.Play();
	snooze(500000);
	printf("replay: at %.2f s\n", Seconds(playback.Position()));
	CHECK(Seconds(playback.Position()) < 1.0 && playback.IsPlaying());
	playback.Close();

	// A GIF (a silent MP4, 3 s) loops and never ends.
	counter->ended = 0;
	CHECK(playback.Open((dir + "/clip.mp4").c_str(), true, true) == B_OK);
	CHECK(!playback.HasAudio());
	playback.Play();
	snooze(4500000);
	printf("looping 3 s clip after 4.5 s: at %.2f s, %d ended\n",
		Seconds(playback.Position()), (int)counter->ended);
	CHECK(counter->ended == 0 && playback.IsPlaying());
	CHECK(Seconds(playback.Position()) > 1.0 && Seconds(playback.Position()) < 2.0);
	playback.Close();

	// WebM: Haiku's decoders play it, though it's sent as a file.
	status = playback.Open((dir + "/clip.webm").c_str(), false, true);
	printf("clip.webm: %s\n", status == B_OK ? "plays" : strerror(status));
	playback.Close();

	// Not a video: refused, nothing left running. (A PNG opens: Haiku
	// reads pictures as one-frame videos; the app shows photos itself.)
	CHECK(playback.Open((dir + "/notes.txt").c_str(), false, false) != B_OK);
	CHECK(playback.Open((dir + "/missing.mp4").c_str(), false, false) != B_OK);
	playback.Close();

	counter->Lock();
	counter->Quit();
	if (sFailures == 0)
		printf("video playback tests passed\n");
	return sFailures == 0 ? 0 : 1;
}
