// SPDX-License-Identifier: GPL-3.0-or-later
#include "Kinetic.h"

#include <algorithm>
#include <cmath>

namespace pp {

namespace {

// How quickly a flick slows down: its speed falls to a third in this many
// seconds (as on phones).
const double kDecay = 0.325;
// The spring that pulls stretched content back: critically damped, so it
// settles without bouncing, in about half a second.
const double kSpring = 12;
// Slower than this (pixels a second), it has stopped.
const float kRest = 10;
// Only the last tenth of a second of the drag sets the flick's speed.
const double kSampleWindow = 0.1;
const float kMaxSpeed = 8000;

} // namespace


float
RubberBand(float overscroll, float size)
{
	if (overscroll <= 0 || size <= 0)
		return 0;
	const float c = 0.55f;
	return (1 - 1 / (overscroll * c / size + 1)) * size;
}


void
KineticScroll::SetRange(float min, float max, float viewSize)
{
	fMin = min;
	fMax = std::max(min, max);
	fSize = std::max(viewSize, 1.0f);
}


void
KineticScroll::Begin(float position, float pointer, double time)
{
	// Caught while stretched: the stretch is where the drag starts from.
	float raw = position;
	if (position < fMin) {
		float shown = fMin - position;
		raw = fMin - (shown < fSize ? shown * fSize / (fSize - shown) / 0.55f : shown);
	} else if (position > fMax) {
		float shown = position - fMax;
		raw = fMax + (shown < fSize ? shown * fSize / (fSize - shown) / 0.55f : shown);
	}
	fStartPosition = raw;
	fStartPointer = pointer;
	fPosition = position;
	fVelocity = 0;
	fDragging = true;
	fMoving = false;
	fSamples.clear();
	fSamples.push_back({time, pointer});
	fLastTime = time;
}


float
KineticScroll::Drag(float pointer, double time)
{
	if (!fDragging)
		return fPosition;
	float raw = fStartPosition - (pointer - fStartPointer);
	if (raw < fMin)
		fPosition = fMin - RubberBand(fMin - raw, fSize);
	else if (raw > fMax)
		fPosition = fMax + RubberBand(raw - fMax, fSize);
	else
		fPosition = raw;
	fSamples.push_back({time, pointer});
	while (fSamples.size() > 2 && time - fSamples.front().time > kSampleWindow)
		fSamples.pop_front();
	fLastTime = time;
	return fPosition;
}


void
KineticScroll::Release(double time)
{
	fDragging = false;
	fVelocity = 0;
	// The speed over the last moments; none if the finger rested first.
	if (fSamples.size() >= 2 && time - fSamples.back().time < 0.05) {
		const Sample& first = fSamples.front();
		const Sample& last = fSamples.back();
		double span = last.time - first.time;
		if (span > 0.005)
			fVelocity = (float)(-(last.pointer - first.pointer) / span);
	}
	fVelocity = std::clamp(fVelocity, -kMaxSpeed, kMaxSpeed);
	// Past an end, a flick further out is mostly lost to the stretch.
	if ((fPosition < fMin && fVelocity < 0) || (fPosition > fMax && fVelocity > 0))
		fVelocity *= 0.2f;
	fLastTime = time;
	fMoving = std::fabs(fVelocity) >= kRest || fPosition < fMin || fPosition > fMax;
}


bool
KineticScroll::Step(double time, float* position)
{
	if (!fMoving) {
		*position = fPosition;
		return false;
	}
	double dt = std::min(time - fLastTime, 0.1);
	fLastTime = time;
	// Small steps keep the spring steady whatever the timer does.
	int steps = std::max(1, (int)std::ceil(dt / 0.004));
	double h = dt / steps;
	double x = fPosition, v = fVelocity;
	for (int i = 0; i < steps; i++) {
		double bound = x < fMin ? fMin : x > fMax ? fMax : x;
		if (bound == x) {
			// Within the range: it coasts and slows down.
			x += v * h;
			v *= std::exp(-h / kDecay);
		} else {
			// Past an end: the spring pulls it back.
			double a = -kSpring * kSpring * (x - bound) - 2 * kSpring * v;
			v += a * h;
			x += v * h;
			// Back at the end, coming in: it stops there.
			if ((bound == fMin && x >= fMin && v >= 0) || (bound == fMax && x <= fMax && v <= 0)) {
				if (std::fabs(v) < 200) {
					x = bound;
					v = 0;
				}
			}
		}
	}
	fPosition = (float)x;
	fVelocity = (float)v;
	bool inside = fPosition >= fMin && fPosition <= fMax;
	if (inside && std::fabs(fVelocity) < kRest) {
		fVelocity = 0;
		fMoving = false;
	} else if (!inside && std::fabs(fVelocity) < kRest
		&& std::fabs(fPosition - (fPosition < fMin ? fMin : fMax)) < 0.5f) {
		fPosition = fPosition < fMin ? fMin : fMax;
		fVelocity = 0;
		fMoving = false;
	}
	*position = fPosition;
	return fMoving;
}


void
TouchDetector::Feed(int32_t subtype, uint32_t buttons, float x, float y, bool moved)
{
	fTablet = subtype == 3;
	if (!fTablet) {
		fHoverMoves = 0;
		return;
	}
	if (moved && buttons == 0) {
		// A finger lifting may report its last position once: only a
		// pointer that moves with nothing pressed is hovering.
		if (fLastX >= 0 && (std::fabs(x - fLastX) > 1 || std::fabs(y - fLastY) > 1)) {
			if (++fHoverMoves >= 3)
				fHovers = true;
		}
	} else if (buttons != 0)
		fHoverMoves = 0;
	fLastX = x;
	fLastY = y;
}

} // namespace pp
