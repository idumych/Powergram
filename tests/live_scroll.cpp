// SPDX-License-Identifier: GPL-3.0-or-later
// Scroll position through real resizes (Haiku only): a visible window laid
// out like the main window, resized through app_server, checked after the
// events have arrived.   make live-scroll && build/live_scroll
#include <Application.h>
#include <Button.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <OS.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <StringView.h>
#include <Window.h>

#include <cmath>
#include <cstdio>

#include "../src/app/MessageView.h"

static int sFailures = 0;

struct State {
	int64 id;
	float fromBottom;
	bool atEnd;
	float top;        // the view's scroll offset
	float barValue;   // the scroll bar's value
	float barMax;
};


static State
Read(BWindow* window, MessageView* view)
{
	State s;
	window->Lock();
	s.id = view->BottomMessage(&s.fromBottom, &s.atEnd);
	s.top = view->Bounds().top;
	BScrollBar* bar = view->ScrollBar(B_VERTICAL);
	float min;
	bar->GetRange(&min, &s.barMax);
	s.barValue = bar->Value();
	window->Unlock();
	return s;
}


static void
Check(const char* what, const State& now, const State& want)
{
	bool ok = now.atEnd == want.atEnd
		&& (want.atEnd || (now.id == want.id && fabsf(now.fromBottom - want.fromBottom) <= 1))
		&& fabsf(now.barValue - now.top) <= 1;
	printf("%s %-40s bottom %lld (%.0f px)%s, view top %.0f, bar %.0f of %.0f\n",
		ok ? "PASS" : "FAIL", what, (long long)now.id, now.fromBottom,
		now.atEnd ? " at end" : "", now.top, now.barValue, now.barMax);
	sFailures += !ok;
}


int
main()
{
	BApplication app("application/x-vnd.paper-plane-live-scroll");
	BWindow* window = new BWindow(BRect(20, 40, 700, 700), "live scroll",
		B_DOCUMENT_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS);
	MessageView* view = new MessageView();
	BScrollView* scroll = new BScrollView("scroll", view, 0, false, true, B_NO_BORDER);
	BGroupView* bar = new BGroupView(B_HORIZONTAL);
	BLayoutBuilder::Group<>(bar)
		.SetInsets(B_USE_SMALL_SPACING)
		.Add(new BStringView("label", "Edit message"))
		.AddGlue()
		.Add(new BButton("cancel", "Cancel", nullptr));
	bar->Hide();
	BLayoutBuilder::Group<>(window, B_VERTICAL, 0)
		.Add(scroll, 1.0f)
		.Add(bar)
		.Add(new BButton("send", "Send", nullptr));

	const char* words[] = {"Renku", "build", "the", "driver", "package",
		"works", "on", "my", "machine", "nightly", "kernel", "log",
		"https://github.com/RenkuOS/Source/pull/1234", "and", "it", "boots"};
	std::vector<MessageRow> rows;
	for (int i = 0; i < 300; i++) {
		MessageRow row = {};
		row.id = i + 1;
		for (int w = 0; w < 10 + (i * 7) % 90; w++)
			row.text << words[(i + w) % 16] << " ";
		row.time = "12:34";
		row.outgoing = i % 4 == 0;
		rows.push_back(row);
	}

	window->Show();
	snooze(300000);
	window->Lock();
	view->SetMessages(rows, true);
	window->Unlock();
	snooze(300000);
	State end = Read(window, view);
	end.atEnd = true;
	Check("at end after first layout", end, end);

	auto step = [&](const char* what, auto action, const State& want) {
		window->Lock();
		action();
		window->Unlock();
		snooze(400000);   // let app_server's resize and the layout arrive
		Check(what, Read(window, view), want);
	};

	step("end: window narrower", [&] { window->ResizeBy(-200, 0); }, end);
	step("end: window wider", [&] { window->ResizeBy(200, 0); }, end);
	step("end: edit bar shown", [&] { bar->Show(); }, end);
	step("end: edit bar hidden", [&] { bar->Hide(); }, end);

	// The middle, scrolled with the scroll bar as a reader would.
	window->Lock();
	view->ScrollBar(B_VERTICAL)->SetValue(3000);
	window->Unlock();
	snooze(200000);
	State middle = Read(window, view);
	Check("middle: scrolled with the bar", middle, middle);
	step("middle: window narrower", [&] { window->ResizeBy(-200, 0); }, middle);
	step("middle: window wider", [&] { window->ResizeBy(200, 0); }, middle);
	step("middle: window shorter", [&] { window->ResizeBy(0, -150); }, middle);
	step("middle: window taller", [&] { window->ResizeBy(0, 150); }, middle);
	step("middle: edit bar shown", [&] { bar->Show(); }, middle);
	step("middle: edit bar hidden", [&] { bar->Hide(); }, middle);
	step("middle: message above edited", [&] {
		for (MessageRow& row : rows)
			if (row.id == middle.id - 2)
				row.text << " now edited to be several lines longer than it was before, "
					"so that its height changes";
		view->SetMessages(rows, false);
	}, middle);

	printf("%s\n", sFailures ? "LIVE SCROLL TESTS FAILED" : "live scroll tests passed");
	window->Lock();
	window->Quit();
	return sFailures ? 1 : 0;
}
