// SPDX-License-Identifier: GPL-3.0-or-later
// Small line icons for the buttons by the message entry, drawn as vector
// shapes at the size asked for, in the given colour.
#pragma once

#include <GraphicsDefs.h>

class BBitmap;

enum IconKind {
	kIconAttach,    // a paper clip
	kIconEmoji,     // a smiling face
	kIconSticker,   // a square with a peeled corner
	kIconMic        // a microphone
};

// A B_RGBA32 bitmap `size` pixels square; the caller owns it.
BBitmap* MakeIcon(IconKind kind, float size, rgb_color color);
