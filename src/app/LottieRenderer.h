// SPDX-License-Identifier: GPL-3.0-or-later
// Draws the frames of an animated sticker (pp::LottieAnimation) into a
// bitmap with Haiku's own drawing: shapes, gradients, strokes, and track
// mattes composited in bitmaps of their own.
#pragma once

#include <SupportDefs.h>

#include <memory>
#include <vector>

#include "../core/Lottie.h"

class BBitmap;
class BView;

class LottieRenderer {
public:
	LottieRenderer(int32 width, int32 height);
	~LottieRenderer();

	int32 Width() const { return fWidth; }
	int32 Height() const { return fHeight; }
	// Draws a frame (transparent background) and returns the bitmap, which
	// stays the renderer's.
	const BBitmap* Draw(const pp::LottieList& frame);

private:
	struct Canvas;
	Canvas* _Canvas(size_t depth);
	void _DrawList(Canvas* canvas, const pp::LottieList& list, size_t depth);

	int32 fWidth, fHeight;
	// Depth 0 is the result; deeper ones hold mattes and what they cut.
	std::vector<std::unique_ptr<Canvas>> fCanvases;
};
