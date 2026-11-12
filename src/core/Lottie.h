// SPDX-License-Identifier: GPL-3.0-or-later
// Animated (.tgs) stickers: gzipped Lottie animations. Telegram allows a
// subset of Lottie in them (no masks, expressions, images, texts, 3D
// layers, merge paths, stars, repeaters or time remapping), and that's
// what this plays: shape and null layers, precompositions, parenting,
// track mattes, paths, rectangles, ellipses, solid and gradient fills,
// strokes, trim paths, and keyframes with their easing.
//
// A frame comes out as polygons with their paint, in pixels of the size
// asked for, ready to be filled or stroked by whatever draws them (the
// app does it with Haiku's BShape).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Json.h"

namespace pp {

struct LottiePoint {
	float x = 0, y = 0;
};

struct LottiePaint {
	enum Kind { kFill, kStroke } kind = kFill;
	// Solid colour, 0..1 each; alpha includes every opacity above it.
	float r = 0, g = 0, b = 0, a = 1;
	// Gradients (fills only): linear from start to end, or radial around
	// start reaching end. Stops are offset (0..1) and colour.
	enum Gradient { kNone, kLinear, kRadial } gradient = kNone;
	LottiePoint start, end;
	struct Stop { float offset, r, g, b, a; };
	std::vector<Stop> stops;
	bool evenOdd = false;
	// Strokes
	float width = 1;
	enum Cap { kButt, kRound, kSquare } cap = kButt;
	enum Join { kMiter, kRoundJoin, kBevel } join = kMiter;
	float miterLimit = 4;
};

// One thing to draw: closed or open polylines, filled or stroked.
struct LottieShape {
	std::vector<std::vector<LottiePoint>> contours;
	std::vector<bool> closed;
	LottiePaint paint;
};

// A frame: shapes drawn in order, the first at the bottom. A matte entry
// draws `content` through `matte` (its alpha or luminance, maybe inverted).
struct LottieItem;
using LottieList = std::vector<LottieItem>;
struct LottieItem {
	LottieShape shape;
	enum Matte { kNoMatte, kAlpha, kAlphaInverted, kLuma, kLumaInverted } matte = kNoMatte;
	std::shared_ptr<LottieList> content, matteSource;
};

class LottieAnimation {
public:
	LottieAnimation();
	~LottieAnimation();

	// From the JSON text (a .tgs file is it gzipped: see GunzipTgs). False
	// if it isn't a Lottie animation.
	bool Load(const std::string& jsonText);

	float FrameRate() const { return fFrameRate; }
	float FirstFrame() const { return fIn; }
	float LastFrame() const { return fOut; }   // exclusive
	int32_t Width() const { return fWidth; }
	int32_t Height() const { return fHeight; }
	// Frames in one run of the animation.
	int32_t FrameCount() const;

	// The shapes of frame `frame` (FirstFrame() .. LastFrame()), scaled to
	// width × height pixels.
	LottieList Render(float frame, float width, float height) const;

private:
	struct Data;
	std::unique_ptr<Data> fData;
	float fFrameRate = 30, fIn = 0, fOut = 0;
	int32_t fWidth = 0, fHeight = 0;
};

// The JSON inside a .tgs file (gzip), "" if it can't be read. Telegram's
// stickers are at most 64 KB packed; more than `maxSize` unpacked is
// refused.
std::string GunzipTgs(const std::string& packed, size_t maxSize = 8 << 20);

// The value of a cubic-bezier easing curve (0,0)-(x1,y1)-(x2,y2)-(1,1) at
// time t (0..1), as CSS and Lottie define it.
float BezierEase(float x1, float y1, float x2, float y2, float t);

} // namespace pp
