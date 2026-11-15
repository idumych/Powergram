// SPDX-License-Identifier: GPL-3.0-or-later
// A channel's sponsored message in the real main window: its view is
// reported once, when all of it is on screen; a click on its title, button,
// photo or a link in its text goes to the app as a sponsored click (which
// Telegram counts before the link opens) rather than opening the link
// straight away; and its menu is its own (about, report), with the report's
// question asked in a second menu. Drives the window with mouse messages as
// app_server sends them.
//   make sponsored-test   (Haiku; opens a window for a few seconds)
#include <Application.h>
#include <Autolock.h>
#include <Locker.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <OS.h>
#include <Window.h>

#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "../src/app/MainWindow.h"
#include "../src/app/MessageView.h"
#include "../src/app/Messages.h"

namespace {

const int64 kChannel = -1003;
const int64 kAd = 1048577, kPhotoAd = 1048578;
const int64 kFirstPost = 301, kLinkPost = 340;   // the newest post has a link
const bigtime_t kSettle = 400000;   // for the window to handle and draw

// What the window sends the app for a sponsored message (Messages.h);
// spelt out so that the test builds without the change too (red).
const uint32 kClick = 'spcl', kViewed = 'spvw', kReport = 'sprp', kOptions = 'spop';

int sFailures = 0;


void
Check(bool ok, const char* what, const std::string& detail = "")
{
	printf("%s: %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : " - ",
		detail.c_str());
	fflush(stdout);
	sFailures += !ok;
}


class TestApp : public BApplication {
public:
	TestApp() : BApplication("application/x-vnd.powergram-sponsored-test") {}

	void ReadyToRun() override
	{
		fMain = new MainWindow(BRect(40, 40, 900, 680), false, {});
		fMain->Show();
		BMessage list(kMsgChatList);
		list.AddInt64("id", kChannel);
		list.AddString("title", "Powergram news");
		list.AddString("text", "");
		list.AddString("sender", "");
		list.AddInt64("date", time(nullptr));
		list.AddInt32("unread", 0);
		list.AddInt32("mentions", 0);
		list.AddBool("pinned", false);
		list.AddBool("read", true);
		list.AddString("typing", "");
		list.AddString("draft", "");
		list.AddBool("muted", false);
		list.AddBool("marked_unread", false);
		list.AddInt32("kind", 2);
		list.AddBool("secret", false);
		list.AddString("photo", "");
		AddBytes(list, "photo_mini", "");
		fMain->PostMessage(&list);
		BMessage select(kMsgSelectChat);
		select.AddInt64("id", kChannel);
		fMain->PostMessage(&select);
		resume_thread(spawn_thread(_Run, "steps", B_NORMAL_PRIORITY, this));
	}

	void MessageReceived(BMessage* message) override
	{
		std::string id = std::to_string(message->GetInt64("message_id", 0));
		switch (message->what) {
			case kMsgOpenChat:
				_Log("open:" + std::to_string(message->GetInt64("id", 0)));
				break;
			case kClick:
				_Log("click:" + id + ":" + message->GetString("url", "")
					+ (message->GetBool("media", false) ? ":media" : ""));
				break;
			case kViewed:
				_Log("viewed:" + id);
				break;
			case kReport:
			{
				std::string option = message->GetString("option", "");
				_Log("report:" + id + ":" + option);
				if (!option.empty())
					break;
				// Telegram's question, as the app passes it on.
				BMessage options(kOptions);
				options.AddInt64("chat_id", message->GetInt64("chat_id", 0));
				options.AddInt64("message_id", message->GetInt64("message_id", 0));
				options.AddString("title", "What's wrong with this ad?");
				options.AddString("option", "AQA=");
				options.AddString("text", "I don't like it");
				options.AddString("option", "AQE=");
				options.AddString("text", "It's misleading");
				fMain->PostMessage(&options);
				break;
			}
			case kMsgOpenLink:
				_Log(std::string("link:") + message->GetString("url", ""));
				break;
			case kMsgOpenMedia:
				_Log("media:" + id);
				break;
			case kMsgMessageProperties:
				_Log("properties:" + id);
				break;
			default:
				break;
		}
	}

private:
	static status_t _Run(void* self)
	{
		TestApp* app = static_cast<TestApp*>(self);
		app->_Steps();
		// The window's own quit asks the app; this closes it outright.
		if (app->fMain->Lock())
			app->fMain->Quit();
		app->PostMessage(B_QUIT_REQUESTED);
		return B_OK;
	}

	void _Log(const std::string& event)
	{
		BAutolock lock(fLock);
		fEvents.push_back(event);
	}

	// What the app was asked to do since the last call.
	std::vector<std::string> _Take()
	{
		snooze(kSettle);
		_Sync();
		BAutolock lock(fLock);
		std::vector<std::string> events;
		events.swap(fEvents);
		return events;
	}

	void _Expect(const char* what, const std::vector<std::string>& expected)
	{
		std::vector<std::string> got = _Take();
		std::string detail;
		if (got != expected) {
			detail = "expected";
			for (const std::string& e : expected)
				detail += " [" + e + "]";
			if (expected.empty())
				detail += " nothing";
			detail += ", got";
			for (const std::string& e : got)
				detail += " [" + e + "]";
			if (got.empty())
				detail += " nothing";
		}
		Check(got == expected, what, detail);
	}

	// A round trip through the window: what was posted before is handled.
	void _Sync()
	{
		BMessage reply;
		BMessenger(fMain).SendMessage('sync', &reply);
	}

	void _History(bool photoAd, bool scrollEnd)
	{
		BMessage history(kMsgHistory);
		history.AddInt64("chat_id", kChannel);
		history.AddBool("scroll_end", scrollEnd);
		if (!scrollEnd)
			history.AddInt64("scroll_to", kFirstPost);
		auto add = [&](int64 id, const char* sender, const char* text, const char* time,
				const char* link, bool sponsored, int32 mediaKind) {
			history.AddInt64("id", id);
			history.AddString("sender", sender);
			history.AddString("text", text);
			history.AddString("time", time);
			for (const char* name : {"outgoing", "read", "pending", "failed", "edited",
					"service", "spoiler", "secret", "show_avatar"})
				history.AddBool(name, false);
			for (const char* name : {"forward", "reply_sender", "reply_text", "image", "emoji",
					"animation", "avatar", "initials", "reactions", "file_name", "file_title",
					"file_type"})
				history.AddString(name, "");
			for (const char* name : {"mini", "avatar_mini", "waveform"})
				AddBytes(history, name, "");
			history.AddInt32("media_kind", mediaKind);
			history.AddInt32("media_w", mediaKind != 0 ? 1600 : 0);
			history.AddInt32("media_h", mediaKind != 0 ? 1200 : 0);
			history.AddInt32("animation_format", 0);
			history.AddInt32("duration", 0);
			history.AddInt64("sender_key", 0);
			history.AddInt64("file_size", 0);
			history.AddInt64("file_done", -1);
			// A link at the very start, where a click can find it.
			pp::Span span;
			span.start = 0;
			span.length = link != nullptr ? (int32_t)strlen(link) : 0;
			span.style = pp::kLink;
			span.url = link != nullptr ? std::string("https://") + link : "";
			history.AddString("spans", link != nullptr
				? pp::EncodeSpans({span}).c_str() : "");
			history.AddBool("sponsored", sponsored);
			history.AddString("button", sponsored ? "Join channel" : "");
			history.AddBool("can_report", sponsored);
			history.AddString("about", sponsored ? "Haiku app makers, t.me/haiku_apps" : "");
		};
		for (int64 id = kFirstPost; id < kLinkPost; id++)
			add(id, "", "A channel post, one of many before the newest.", "10:00", nullptr,
				false, 0);
		add(kLinkPost, "", "t.me/renku_os has the release notes.", "10:30", "t.me/renku_os",
			false, 0);
		if (photoAd)
			add(kPhotoAd, "Haiku app makers", "", "Sponsored", nullptr, true, 1);
		else {
			add(kAd, "Haiku app makers", "t.me/haiku_apps: native apps for Haiku, built together.",
				"Sponsored", "t.me/haiku_apps", true, 0);
		}
		fMain->PostMessage(&history);
	}

	MessageView* _View()
	{
		return dynamic_cast<MessageView*>(fMain->FindView("messages"));
	}

	// Message `id`'s bubble, and the view's line height and padding.
	BRect _Frame(int64 id, float* line, float* pad)
	{
		BRect frame;
		if (fMain->Lock()) {
			MessageView* view = _View();
			frame = view->Frame(id);
			font_height fh;
			view->GetFontHeight(&fh);
			*line = ceilf(fh.ascent + fh.descent + fh.leading);
			*pad = roundf(*line * 0.5f);
			fMain->Unlock();
		}
		return frame;
	}

	void _ScrollTo(int64 id)
	{
		if (fMain->Lock()) {
			_View()->ScrollToMessage(id);
			fMain->Unlock();
		}
	}

	// A press and release at `where` (view coordinates), as app_server
	// sends them.
	void _Click(BPoint where, int32 buttons = B_PRIMARY_MOUSE_BUTTON)
	{
		BPoint screen = where;
		BPoint windowWhere = where;
		if (fMain->Lock()) {
			MessageView* view = _View();
			screen = view->ConvertToScreen(where);
			windowWhere = fMain->ConvertFromScreen(screen);
			fMain->Unlock();
		}
		for (uint32 what : {(uint32)B_MOUSE_DOWN, (uint32)B_MOUSE_UP}) {
			BMessage event(what);
			event.AddInt64("when", system_time());
			event.AddPoint("where", windowWhere);
			event.AddPoint("screen_where", screen);
			event.AddPoint("be:view_where", where);
			event.AddInt32("modifiers", 0);
			if (what == B_MOUSE_DOWN) {
				event.AddInt32("buttons", buttons);
				event.AddInt32("clicks", 1);
			} else
				event.AddInt32("buttons", 0);
			BMessenger(_ViewHandler(), fMain).SendMessage(&event);
		}
	}

	BHandler* _ViewHandler()
	{
		BAutolock lock(fMain);
		return _View();
	}

	// The open pop-up menu item labelled `label`, and its menu's window.
	BMenuItem* _MenuItem(const char* label, BLooper** looper)
	{
		int32 cookie = 0;
		thread_info info;
		while (get_next_thread_info(0, &cookie, &info) == B_OK) {
			BWindow* window = dynamic_cast<BWindow*>(BLooper::LooperForThread(info.thread));
			if (window == nullptr || window == fMain || !window->Lock())
				continue;
			BMenuItem* found = nullptr;
			for (int32 i = 0; found == nullptr && i < window->CountChildren(); i++)
				found = _FindItem(window->ChildAt(i), label);
			window->Unlock();
			if (found != nullptr) {
				*looper = window;
				return found;
			}
		}
		return nullptr;
	}

	static BMenuItem* _FindItem(BView* view, const char* label)
	{
		if (BMenu* menu = dynamic_cast<BMenu*>(view)) {
			for (int32 i = 0; i < menu->CountItems(); i++)
				if (strcmp(menu->ItemAt(i)->Label(), label) == 0)
					return menu->ItemAt(i);
		}
		for (int32 i = 0; i < view->CountChildren(); i++)
			if (BMenuItem* item = _FindItem(view->ChildAt(i), label))
				return item;
		return nullptr;
	}

	// Picks `label` from the pop-up menu on screen, as a click on it would.
	bool _Pick(const char* label)
	{
		BLooper* looper = nullptr;
		BMenuItem* item = nullptr;
		for (int i = 0; i < 20 && item == nullptr; i++) {
			item = _MenuItem(label, &looper);
			if (item == nullptr)
				snooze(100000);
		}
		if (item == nullptr)
			return false;
		if (looper->Lock()) {
			// What invoking it sends, where it sends it.
			BMessage chosen(*item->Message());
			item->Messenger().SendMessage(&chosen);
			// Closed with Escape, as the user would leave it.
			BMenu* menu = item->Menu();
			BMessage key(B_KEY_DOWN);
			key.AddInt64("when", system_time());
			key.AddInt32("modifiers", 0);
			key.AddInt32("key", 1);
			key.AddInt8("byte", B_ESCAPE);
			key.AddString("bytes", "\x1b");
			key.AddInt32("raw_char", B_ESCAPE);
			looper->PostMessage(&key, menu);
			looper->Unlock();
		}
		return true;
	}

	void _Steps()
	{
		snooze(kSettle);
		_Expect("opening the channel asks for its history",
			{"open:" + std::to_string(kChannel)});

		// Opened at the oldest post: the sponsored message is far below.
		_History(false, false);
		float line = 0, pad = 0;
		_Expect("no view while it's off screen", {});
		BRect ad = _Frame(kAd, &line, &pad);
		Check(ad.IsValid(), "the sponsored message is the last row");

		_ScrollTo(kAd);
		_Expect("scrolled to: viewed once", {"viewed:" + std::to_string(kAd)});
		_ScrollTo(kFirstPost);
		_Take();
		_ScrollTo(kLinkPost);
		_Expect("scrolled away and back: not again", {});

		// Clicks, with the newest post and the sponsored message on screen.
		BRect post = _Frame(kLinkPost, &line, &pad);
		_Click(BPoint(post.left + pad + 4, post.top + pad + line / 2));
		_Expect("a link in a post opens as before", {"link:https://t.me/renku_os"});

		ad = _Frame(kAd, &line, &pad);
		std::string click = "click:" + std::to_string(kAd) + ":";
		_Click(BPoint(ad.left + pad + 4, ad.top + pad + line / 2));
		_Expect("its title is a sponsored click", {click});
		_Click(BPoint(ad.left + pad + 4, ad.top + pad + line * 1.5f));
		_Expect("a link in it is a sponsored click with that link",
			{click + "https://t.me/haiku_apps"});
		float button = ad.bottom - line - pad * 0.5f - roundf(line * 1.6f) / 2;
		_Click(BPoint((ad.left + ad.right) / 2, button));
		_Expect("its button is a sponsored click", {click});
		_Click(BPoint(ad.right - pad - 2, ad.top + pad + line * 1.5f));
		_Expect("text that isn't a link does nothing", {});

		// Its own menu: about and report, nothing of a chat message's.
		_Click(BPoint(ad.left + pad + 4, ad.top + pad + line * 1.5f), B_SECONDARY_MOUSE_BUTTON);
		snooze(kSettle);
		BLooper* looper = nullptr;
		Check(_MenuItem("About this ad" B_UTF8_ELLIPSIS, &looper) != nullptr,
			"its menu has About this ad");
		Check(_MenuItem("Reply", &looper) == nullptr, "and not Reply");
		bool picked = _Pick("Report ad" B_UTF8_ELLIPSIS);
		Check(picked, "and has Report ad");
		_Expect("reporting asks the app", {"report:" + std::to_string(kAd) + ":"});
		Check(picked && _Pick("It's misleading"), "the app's question is a menu");
		_Expect("the answer goes back", {"report:" + std::to_string(kAd) + ":AQE="});

		// One with a photo: a click on the photo counts as a media click.
		_History(true, true);
		_Expect("a new sponsored message on screen: viewed",
			{"viewed:" + std::to_string(kPhotoAd)});
		BRect photo = _Frame(kPhotoAd, &line, &pad);
		_Click(BPoint((photo.left + photo.right) / 2, photo.top + pad + line * 2.5f));
		_Expect("a click on its photo is a media click",
			{"click:" + std::to_string(kPhotoAd) + "::media"});

		printf("%s\n", sFailures == 0 ? "all sponsored message tests passed"
			: "sponsored message tests FAILED");
		fflush(stdout);
	}

	MainWindow* fMain = nullptr;
	BLocker fLock;
	std::vector<std::string> fEvents;
};

} // namespace


int
main()
{
	TestApp app;
	app.Run();
	return sFailures == 0 ? 0 : 1;
}
