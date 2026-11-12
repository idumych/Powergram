// SPDX-License-Identifier: GPL-3.0-or-later
// Scrolling by dragging with a finger, in every scrolling view of a window
// (the chat list, the chat, the pickers, other lists): a message filter
// holds a press back until it's clear whether it's a tap (then replayed as
// a click) or a drag, which scrolls the view under it with rubber banding
// and keeps going after a flick (pp::KineticScroll).
#pragma once

#include <SupportDefs.h>

class BWindow;

enum DragScrollMode {
	kDragScrollTouch = 0,   // touch screens only (pp::TouchDetector)
	kDragScrollAlways = 1,  // any pointer, the mouse too
	kDragScrollNever = 2
};

// Adds the filter to a window (once).
void AddTouchScrolling(BWindow* window);
// The Settings choice, for every window.
void SetDragScrollMode(int32 mode);
