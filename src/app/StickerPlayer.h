// SPDX-License-Identifier: GPL-3.0-or-later
// Plays an animated sticker in the chat: Lottie (.tgs) ones drawn with
// LottieRenderer, video (.webm) ones decoded with libvpx (WebmPlayer.cpp).
// The chat view asks for the frame due now at the size it draws it.
#pragma once

#include <OS.h>
#include <String.h>

class BBitmap;

class StickerPlayer {
public:
	enum Format { kTgs = 1, kWebm = 2 };

	// nullptr if the file can't be played (not there yet, broken, or a
	// format this build can't decode).
	static StickerPlayer* Open(const BString& path, int32 format);
	virtual ~StickerPlayer() {}

	// The frame for `now` (it loops), width × height pixels with a
	// transparent background; nullptr if there's none. The bitmap stays
	// the player's.
	virtual const BBitmap* Frame(int32 width, int32 height, bigtime_t now) = 0;
	// How often the frame changes.
	virtual bigtime_t FrameInterval() const = 0;
};

// Video stickers: in WebmPlayer.cpp, or a stand-in returning nullptr where
// libvpx and libwebm aren't available.
StickerPlayer* OpenWebmSticker(const BString& path);
