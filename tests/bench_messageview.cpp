// SPDX-License-Identifier: GPL-3.0-or-later
// Times MessageView layouts with synthetic messages (Haiku only):
//   make bench && build/bench_messageview
#include <Application.h>
#include <OS.h>
#include <Window.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../src/app/MessageView.h"

int
main(int argc, char** argv)
{
	bool show = argc > 1 && strcmp(argv[1], "--show") == 0;
	// --show --media DIR: end with test photos and stickers from DIR
	// (photo.jpg, mini.jpg, sticker.webp).
	const char* mediaDir = argc > 3 && strcmp(argv[2], "--media") == 0 ? argv[3] : nullptr;
	BApplication app("application/x-vnd.powergram-bench");
	BWindow* window = new BWindow(BRect(0, 0, 640, 600), "bench",
		B_TITLED_WINDOW, 0);
	MessageView* view = new MessageView();
	view->SetResizingMode(B_FOLLOW_ALL);
	view->MoveTo(0, 0);
	view->ResizeTo(640, 600);
	window->AddChild(view);

	const char* words[] = {"Renku", "build", "the", "driver", "package",
		"works", "on", "my", "machine", "nightly", "kernel", "log",
		"https://github.com/RenkuOS/Source/pull/1234", "and", "it", "boots"};
	std::vector<MessageRow> rows;
	for (int i = 0; i < 300; i++) {
		MessageRow row = {};
		row.id = i + 1;
		row.sender = i % 3 ? "Someone" : "";
		for (int w = 0; w < 10 + (i * 7) % 90; w++)
			row.text << words[(i + w) % 16] << " ";
		row.time = "12:34";
		row.outgoing = i % 4 == 0;
		if (i % 5 == 0) {
			row.replySender = "Other";
			row.replyText = "an earlier message that is being replied to";
		}
		rows.push_back(row);
	}

	window->Lock();
	for (int run = 0; run < 3; run++) {
		bigtime_t start = system_time();
		view->SetMessages(rows, true);
		printf("SetMessages(%zu rows): %.1f ms\n", rows.size(),
			(system_time() - start) / 1000.0);
	}
	bigtime_t start = system_time();
	view->ResizeTo(500, 600);
	view->FrameResized(500, 600);
	printf("resize relayout: %.1f ms\n", (system_time() - start) / 1000.0);
	// Scroll position must survive re-layouts (anchored to the message at
	// the bottom), and a reader at the end must stay at the end.
	int failures = 0;
	auto check = [&](const char* what, int64 wantId, float wantFrom, bool wantEnd) {
		float from;
		bool atEnd;
		int64 id = view->BottomMessage(&from, &atEnd);
		bool ok = atEnd == wantEnd && (wantEnd || (id == wantId && fabsf(from - wantFrom) <= 1));
		printf("%s %s: bottom message %lld, %.1f px from the edge%s\n",
			ok ? "PASS" : "FAIL", what, (long long)id, from, atEnd ? ", at end" : "");
		failures += !ok;
	};

	view->ResizeTo(640, 600);
	view->FrameResized(640, 600);
	check("at end after layout", 0, 0, true);
	view->ResizeTo(400, 600);
	view->FrameResized(400, 600);
	check("at end after narrowing", 0, 0, true);

	// Into the middle, as a reader would scroll.
	view->ScrollTo(BPoint(0, 3000));
	float from;
	bool atEnd;
	int64 id = view->BottomMessage(&from, &atEnd);
	check("middle, before", id, from, false);
	view->ResizeTo(640, 600);
	view->FrameResized(640, 600);
	check("middle, after widening", id, from, false);
	view->ResizeTo(640, 540);   // the reply/edit bar appears
	view->FrameResized(640, 540);
	check("middle, after the view got shorter", id, from, false);

	// Older messages load above; a message above the anchor gets taller.
	std::vector<MessageRow> more;
	for (int i = 0; i < 50; i++) {
		MessageRow row = rows[i];
		row.id = -1000 + i;
		more.push_back(row);
	}
	more.insert(more.end(), rows.begin(), rows.end());
	view->SetMessages(more, false);
	check("middle, after older messages loaded", id, from, false);
	for (MessageRow& row : more)
		if (row.id == id - 3)
			row.text << " and now it is much longer, edited to add several more lines of text "
				"so that its height changes a lot after the edit";
	view->SetMessages(more, false);
	check("middle, after an edit above", id, from, false);

	// Back at the end: a new message keeps the reader at the end.
	view->ScrollTo(BPoint(0, 1e6));
	MessageRow newest = rows.back();
	newest.id = 100000;
	more.push_back(newest);
	view->SetMessages(more, false);
	check("end, after a new message", 0, 0, true);
	printf("%s\n", failures ? "SCROLL TESTS FAILED" : "scroll tests passed");

	if (mediaDir != nullptr) {
		BString dir(mediaDir);
		std::string mini;
		if (FILE* f = fopen(BString(dir).Append("/mini.jpg").String(), "rb")) {
			char buffer[4096];
			size_t n;
			while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
				mini.append(buffer, n);
			fclose(f);
		}
		auto media = [&](int64 id, int32 kind, const char* image, bool out) {
			MessageRow row = {};
			row.id = 200000 + id;
			row.time = "12:34";
			row.outgoing = out;
			row.mediaKind = kind;
			if (image != nullptr)
				row.imagePath = BString(dir).Append("/").Append(image);
			row.minithumbnail = kind == 1 ? mini : "";
			row.mediaWidth = kind == 1 ? 1920 : 512;
			row.mediaHeight = kind == 1 ? 1080 : 512;
			return row;
		};
		MessageRow photo = media(1, 1, "photo.jpg", false);
		photo.sender = "Someone";
		photo.text = "A photo with a caption under it";
		rows.push_back(photo);
		rows.push_back(media(2, 1, nullptr, false));          // still loading
		MessageRow spoiler = media(3, 1, "photo.jpg", true);
		spoiler.spoiler = true;
		rows.push_back(spoiler);
		MessageRow secret = media(4, 1, nullptr, false);
		secret.secret = true;
		rows.push_back(secret);
		rows.push_back(media(5, 2, "sticker.webp", false));   // WebP sticker
		MessageRow animated = media(6, 2, nullptr, true);     // .tgs: emoji only
		animated.emoji = "🎉";
		rows.push_back(animated);
		MessageRow video = media(9, 3, "photo.jpg", false);  // a video's thumbnail
		video.duration = 142;
		video.text = "A video with a caption";
		rows.push_back(video);
		rows.push_back(media(10, 4, "photo.jpg", true));    // a GIF
		MessageRow note = media(11, 5, "square.jpg", false); // round video message
		note.mediaWidth = note.mediaHeight = 384;
		note.duration = 9;
		rows.push_back(note);
		// A group: runs of messages from two people, with and without a picture.
		for (int g = 0; g < 5; g++) {
			MessageRow row = {};
			row.id = 200100 + g;
			row.time = "12:40";
			bool ann = g < 3;
			row.sender = ann ? "Ann Example" : "Bo Sample";
			row.text = ann ? "A message in a group, from someone with a picture"
				: "And someone without one";
			row.showAvatar = true;
			row.senderKey = ann ? 7 : 8;
			row.avatarInitials = ann ? "AE" : "BS";
			if (ann)
				row.avatarPath = BString(dir).Append("/square.jpg");
			rows.push_back(row);
		}
		MessageRow emojiText = {};
		emojiText.id = 200008;
		emojiText.time = "12:35";
		emojiText.text = "Emoji in text: \xF0\x9F\x8E\x89 \xF0\x9F\x98\x80";
		rows.push_back(emojiText);
		MessageRow reply = {};
		reply.id = 200007;
		reply.time = "12:35";
		reply.text = "Nice picture!";
		reply.outgoing = true;
		reply.replySender = "Someone";
		reply.replyText = "Photo";
		rows.push_back(reply);
		view->SetMessages(rows, true);
		// BENCH_TOP=<id>: scroll that message to the top, for a screenshot.
		if (const char* top = getenv("BENCH_TOP")) {
			view->SetMessages(rows, false);
			view->ScrollToMessage(atoll(top));
		}
	}

	window->Unlock();
	if (show) {
		// Leave it on screen (for a screenshot); quit with the window.
		window->SetFlags(B_QUIT_ON_WINDOW_CLOSE);
		window->MoveTo(40, 40);
		window->Show();
		app.Run();
		return 0;
	}
	window->PostMessage(B_QUIT_REQUESTED);
	return 0;
}
