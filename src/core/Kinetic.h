// SPDX-License-Identifier: GPL-3.0-or-later
// Scrolling with a finger: the content follows the drag, stretches with
// growing resistance past either end (rubber banding), keeps moving after
// a flick and slows down, and springs back to the end it went past. Also
// telling a touch screen from a pointer that only looks like one (the
// absolute "tablet" pointer of VirtualBox and QEMU).
#pragma once

#include <cstdint>
#include <deque>

namespace pp {

// How far content shows past its end when dragged `overscroll` pixels past
// it, in a view `size` pixels tall: almost as far at first, then less and
// less, never more than the view (as on phones).
float RubberBand(float overscroll, float size);

class KineticScroll {
public:
	// The scroll positions allowed (min..max) and the view's height.
	void SetRange(float min, float max, float viewSize);

	// A finger down at `pointer` (a screen y) while scrolled to `position`.
	void Begin(float position, float pointer, double time);
	// The finger moved: the scroll position to show.
	float Drag(float pointer, double time);
	// The finger lifted: the flick's speed carries on.
	void Release(double time);
	// The position at `time` while it moves on its own; false once it has
	// come to rest (within the range).
	bool Step(double time, float* position);

	bool Dragging() const { return fDragging; }
	bool Moving() const { return fMoving; }
	float Position() const { return fPosition; }
	float Velocity() const { return fVelocity; }   // pixels a second, of the scroll position

private:
	float fMin = 0, fMax = 0, fSize = 1;
	float fStartPosition = 0, fStartPointer = 0;
	float fPosition = 0, fVelocity = 0;
	double fLastTime = 0;
	bool fDragging = false;
	bool fMoving = false;
	struct Sample { double time; float pointer; };
	std::deque<Sample> fSamples;
};

// Whether pointer events come from a touch screen. Haiku reports touch
// screens, pen tablets and the absolute pointers of virtual machines alike
// (be:device_subtype 3, B_TABLET_POINTING_DEVICE); a finger, though, only
// moves the pointer while it touches, while the others move it with no
// button down (hovering). Once a tablet has hovered, it isn't a finger.
class TouchDetector {
public:
	// Every mouse message: its device subtype, buttons, and screen position.
	void Feed(int32_t subtype, uint32_t buttons, float x, float y, bool moved);
	bool IsTouch() const { return fTablet && !fHovers; }

private:
	bool fTablet = false;
	bool fHovers = false;
	int32_t fHoverMoves = 0;
	float fLastX = -1, fLastY = -1;
};

} // namespace pp
