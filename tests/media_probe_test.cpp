// SPDX-License-Identifier: GPL-3.0-or-later
// What Powergram sends files as, on Haiku: tests/data has a 320x240 3 s
// H.264 MP4, a WebM, a 64x48 PNG and a text file.
#include <Application.h>

#include <cstdio>
#include <string>

#include "../src/app/MediaProbe.h"

static int sFailures = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
	sFailures++; } } while (0)

int
main(int argc, char** argv)
{
	// Reading images makes bitmaps, which need the app_server connection
	// a BApplication brings (the app has one).
	BApplication app("application/x-vnd.powergram-media-probe-test");
	std::string dir = argc > 1 ? argv[1] : "tests/data";
	pp::OutgoingFile mp4 = ProbeFile((dir + "/clip.mp4").c_str());
	printf("clip.mp4: kind %d, %dx%d, %d s\n", mp4.kind, mp4.width, mp4.height, mp4.duration);
	CHECK(mp4.kind == pp::OutgoingFile::kVideo && mp4.width == 320 && mp4.height == 240
		&& mp4.duration == 3);
	pp::OutgoingFile webm = ProbeFile((dir + "/clip.webm").c_str());
	CHECK(webm.kind == pp::OutgoingFile::kDocument);   // not every app plays it
	pp::OutgoingFile png = ProbeFile((dir + "/photo.png").c_str());
	CHECK(png.kind == pp::OutgoingFile::kPhoto && png.width == 64 && png.height == 48);
	// A GIF (40 × 30, five frames) goes as an animation, which keeps moving.
	pp::OutgoingFile gif = ProbeFile((dir + "/anim.gif").c_str());
	CHECK(gif.kind == pp::OutgoingFile::kAnimation && gif.width == 40 && gif.height == 30);
	pp::OutgoingFile text = ProbeFile((dir + "/notes.txt").c_str());
	CHECK(text.kind == pp::OutgoingFile::kDocument && text.path == dir + "/notes.txt");
	printf(sFailures == 0 ? "media probe tests passed\n" : "%d failures\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
