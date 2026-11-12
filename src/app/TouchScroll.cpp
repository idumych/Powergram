// SPDX-License-Identifier: GPL-3.0-or-later
#include "TouchScroll.h"

#include <MessageFilter.h>
#include <MessageRunner.h>
#include <OS.h>
#include <ScrollBar.h>
#include <View.h>
#include <Window.h>

#include <atomic>
#include <cmath>

#include "../core/Kinetic.h"

namespace {

std::atomic<int32> sMode(kDragScrollTouch);
// One detector for the app: the device doesn't change between windows.
pp::TouchDetector sDetector;

const uint32 kMsgTouchTick = 'ttck';
// Marks a press and release replayed after a tap, for the filter to let by.
const char* kReplayed = "pp:touch_replayed";
// A press that moves less than this is a tap.
const float kSlop = 8;


class TouchFilter : public BMessageFilter {
public:
	TouchFilter() : BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE) {}

	~TouchFilter() override
	{
		delete fRunner;
		delete fPendingDown;
	}

	filter_result Filter(BMessage* message, BHandler** target) override
	{
		BWindow* window = dynamic_cast<BWindow*>(Looper());
		if (window == nullptr)
			return B_DISPATCH_MESSAGE;
		uint32 what = message->what;
		if (what == kMsgTouchTick) {
			_Tick(window);
			return B_SKIP_MESSAGE;
		}
		if (what != B_MOUSE_DOWN && what != B_MOUSE_MOVED && what != B_MOUSE_UP)
			return B_DISPATCH_MESSAGE;
		if (message->HasBool(kReplayed))
			return B_DISPATCH_MESSAGE;

		BPoint screen;
		if (message->FindPoint("screen_where", &screen) != B_OK)
			return B_DISPATCH_MESSAGE;
		int32 buttons = message->GetInt32("buttons", 0);
		sDetector.Feed(message->GetInt32("be:device_subtype", 0), (uint32)buttons, screen.x,
			screen.y, what == B_MOUSE_MOVED);
		double now = system_time() / 1e6;

		switch (what) {
			case B_MOUSE_DOWN:
			{
				int32 mode = sMode;
				bool touch = mode == kDragScrollAlways
					|| (mode == kDragScrollTouch && sDetector.IsTouch());
				if (!touch || (buttons & ~B_PRIMARY_MOUSE_BUTTON) != 0)
					return B_DISPATCH_MESSAGE;
				BView* view = _ScrollTarget(window, window->ConvertFromScreen(screen));
				if (view == nullptr)
					return B_DISPATCH_MESSAGE;
				// A finger on a moving list stops it, and may drag it on.
				bool caught = fScroll.Moving() && view == fView;
				_Stop(caught);
				fView = view;
				fTarget = *target;
				delete fPendingDown;
				fPendingDown = new BMessage(*message);
				fStart = screen;
				fState = caught ? kDragging : kPending;
				if (caught)
					fScroll.Begin(fScroll.Position(), screen.y, now);
				// Keep getting the pointer's moves outside the view.
				view->SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
				return B_SKIP_MESSAGE;
			}
			case B_MOUSE_MOVED:
				if (fState == kPending && (std::fabs(screen.y - fStart.y) > kSlop
						|| std::fabs(screen.x - fStart.x) > kSlop)) {
					fState = kDragging;
					_BeginDrag(fStart.y, now);
				}
				if (fState == kDragging) {
					_ScrollTo(fScroll.Drag(screen.y, now));
					return B_SKIP_MESSAGE;
				}
				return fState == kPending ? B_SKIP_MESSAGE : B_DISPATCH_MESSAGE;
			case B_MOUSE_UP:
				if (fState == kPending) {
					// A tap: the press and release go where they were meant to.
					fState = kIdle;
					if (fPendingDown != nullptr) {
						fPendingDown->AddBool(kReplayed, true);
						window->PostMessage(fPendingDown, fTarget);
						BMessage up(*message);
						up.AddBool(kReplayed, true);
						window->PostMessage(&up, fTarget);
					}
					return B_SKIP_MESSAGE;
				}
				if (fState == kDragging) {
					fState = kIdle;
					fScroll.Release(now);
					if (fScroll.Moving()) {
						BMessage tick(kMsgTouchTick);
						delete fRunner;
						fRunner = new BMessageRunner(BMessenger(window), &tick, 16667);
					} else
						_Stop(false);
					return B_SKIP_MESSAGE;
				}
				return B_DISPATCH_MESSAGE;
		}
		return B_DISPATCH_MESSAGE;
	}

private:
	enum State { kIdle, kPending, kDragging };

	// The view that scrolls under `where`: the innermost one with a
	// vertical scroll bar that has somewhere to go.
	static BView* _ScrollTarget(BWindow* window, BPoint where)
	{
		// FindView can stop at a container (the chat's BScrollView): go
		// down to the innermost view under the point first.
		BView* hit = window->FindView(where);
		for (bool deeper = hit != nullptr; deeper;) {
			deeper = false;
			for (int32 i = 0; BView* child = hit->ChildAt(i); i++) {
				if (!child->IsHidden(child) && child->ConvertToScreen(child->Bounds())
						.Contains(window->ConvertToScreen(where))) {
					hit = child;
					deeper = true;
					break;
				}
			}
		}
		for (BView* view = hit; view != nullptr; view = view->Parent()) {
			BScrollBar* bar = view->ScrollBar(B_VERTICAL);
			if (bar == nullptr)
				continue;
			float min, max;
			bar->GetRange(&min, &max);
			if (max > min)
				return view;
		}
		return nullptr;
	}

	void _BeginDrag(float pointer, double now)
	{
		BScrollBar* bar = fView->ScrollBar(B_VERTICAL);
		if (bar == nullptr)
			return;
		bar->GetRange(&fMin, &fMax);
		float height = fView->Bounds().Height();
		// Room to stretch past the ends (the bar clamps scrolling to its
		// range); put back when it comes to rest.
		fStretchMin = fMin - height;
		fStretchMax = fMax + height;
		bar->SetRange(fStretchMin, fStretchMax);
		fStretched = true;
		fScroll.SetRange(fMin, fMax, height);
		fScroll.Begin(fView->Bounds().top, pointer, now);
	}

	void _ScrollTo(float position)
	{
		if (fView != nullptr)
			fView->ScrollTo(BPoint(fView->Bounds().left, roundf(position)));
	}

	void _Tick(BWindow*)
	{
		float position;
		bool moving = fScroll.Step(system_time() / 1e6, &position);
		_ScrollTo(position);
		if (!moving)
			_Stop(false);
	}

	// Ends any movement; `keep`: a finger caught it, the stretch stays.
	void _Stop(bool keep)
	{
		delete fRunner;
		fRunner = nullptr;
		if (keep || !fStretched || fView == nullptr)
			return;
		fStretched = false;
		BScrollBar* bar = fView->ScrollBar(B_VERTICAL);
		if (bar == nullptr)
			return;
		// Unless the view set a new range meanwhile (new messages).
		float min, max;
		bar->GetRange(&min, &max);
		if (min == fStretchMin && max == fStretchMax)
			bar->SetRange(fMin, fMax);
	}

	pp::KineticScroll fScroll;
	State fState = kIdle;
	BView* fView = nullptr;
	BHandler* fTarget = nullptr;
	BMessage* fPendingDown = nullptr;
	BPoint fStart;
	BMessageRunner* fRunner = nullptr;
	float fMin = 0, fMax = 0, fStretchMin = 0, fStretchMax = 0;
	bool fStretched = false;
};

} // namespace


void
AddTouchScrolling(BWindow* window)
{
	window->AddCommonFilter(new TouchFilter);
}


void
SetDragScrollMode(int32 mode)
{
	sMode = mode;
}
