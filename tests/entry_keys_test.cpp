// SPDX-License-Identifier: GPL-3.0-or-later
// The chat entry sends, and the find field searches, when Enter is pressed
// in them, and only then: moving the focus away (Search → Find…, clicking
// another field) leaves what's typed alone (issue #1). Drives the real main
// window with keyboard messages as the input server sends them.
//   make entry-keys-test   (Haiku; opens a window for a few seconds)
#include <Application.h>
#include <Autolock.h>
#include <Button.h>
#include <Locker.h>
#include <Messenger.h>
#include <OS.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "../src/app/MainWindow.h"
#include "../src/app/Messages.h"

namespace {

const int64 kChat = 2002, kGroup = -1001;
const bigtime_t kSettle = 300000;   // for the window to handle what was posted

int sFailures = 0;


class TestApp : public BApplication {
public:
	TestApp() : BApplication("application/x-vnd.powergram-entry-keys-test") {}

	void ReadyToRun() override
	{
		fMain = new MainWindow(BRect(40, 40, 900, 640), false, {});
		fMain->Show();
		BMessage list(kMsgChatList);
		for (int64 id : {kChat, kGroup}) {
			list.AddInt64("id", id);
			list.AddString("title", id == kGroup ? "Renku testers" : "Mira Okafor");
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
			list.AddInt32("kind", id == kGroup ? 1 : 0);
			list.AddBool("secret", false);
			list.AddString("photo", "");
			AddBytes(list, "photo_mini", "");
		}
		fMain->PostMessage(&list);
		BMessage select(kMsgSelectChat);
		select.AddInt64("id", kChat);
		fMain->PostMessage(&select);
		resume_thread(spawn_thread(_Run, "steps", B_NORMAL_PRIORITY, this));
	}

	void MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case kMsgSendText:
				_Log(std::string("send:") + message->GetString("text", ""));
				break;
			case kMsgFind:
				_Log(std::string("find:") + message->GetString("query", ""));
				break;
			case kMsgMentionSearch:
			{
				// One member, whatever was typed after the "@".
				BMessage results(kMsgMentionResults);
				results.AddInt32("seq", message->GetInt32("seq", 0));
				results.AddInt64("user_id", 11);
				results.AddString("name", "Mira Okafor");
				results.AddString("username", "mira_o");
				fMain->PostMessage(&results);
				break;
			}
			default:
				// Opening the chat, drafts, typing status: not looked at.
				break;
		}
	}

private:
	static status_t _Run(void* self)
	{
		static_cast<TestApp*>(self)->_Steps();
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
		BAutolock lock(fLock);
		std::vector<std::string> events;
		events.swap(fEvents);
		return events;
	}

	void _Expect(const char* what, const std::vector<std::string>& expected)
	{
		std::vector<std::string> got = _Take();
		bool ok = got == expected;
		printf("%s: %s", ok ? "PASS" : "FAIL", what);
		if (!ok) {
			printf("\n  expected:");
			for (const std::string& e : expected)
				printf(" [%s]", e.c_str());
			if (expected.empty())
				printf(" nothing");
			printf("\n  got:");
			for (const std::string& e : got)
				printf(" [%s]", e.c_str());
			if (got.empty())
				printf(" nothing");
			sFailures++;
		}
		printf("\n");
		fflush(stdout);
	}

	void _ExpectText(const char* view, const char* expected)
	{
		BString text;
		_Sync();
		if (fMain->Lock()) {
			if (BTextControl* control = dynamic_cast<BTextControl*>(fMain->FindView(view)))
				text = control->Text();
			fMain->Unlock();
		}
		bool ok = text == expected;
		printf("%s: \"%s\" holds \"%s\"", ok ? "PASS" : "FAIL", view, expected);
		if (!ok) {
			printf(" (it holds \"%s\")", text.String());
			sFailures++;
		}
		printf("\n");
		fflush(stdout);
	}

	// One key, as the input server sends it: to the window's preferred
	// handler, which the window turns into its focus view.
	void _Key(const char* bytes, int32 key, uint32 modifiers = 0)
	{
		BMessage down(B_KEY_DOWN);
		down.AddInt64("when", system_time());
		down.AddInt32("modifiers", modifiers);
		down.AddInt32("key", key);
		down.AddInt8("byte", bytes[0]);
		down.AddString("bytes", bytes);
		down.AddInt32("raw_char", (uint8)bytes[0]);
		BMessenger(nullptr, fMain).SendMessage(&down);
	}

	void _Type(const char* text)
	{
		for (const char* c = text; *c != '\0'; c++) {
			char bytes[2] = {*c, '\0'};
			_Key(bytes, 0);
		}
	}

	void _Enter() { _Key("\n", 0x47); }

	// What a click into another field does to the focus.
	void _Focus(const char* view)
	{
		_Sync();
		if (fMain->Lock()) {
			BTextControl* control = dynamic_cast<BTextControl*>(fMain->FindView(view));
			if (control != nullptr) {
				control->MakeFocus(true);
				// The caret after the text, where a click there puts it
				// (focus from the keyboard selects it all).
				int32 end = control->TextView()->TextLength();
				control->TextView()->Select(end, end);
			} else {
				printf("FAIL: no text field \"%s\"\n", view);
				sFailures++;
			}
			fMain->Unlock();
		}
	}

	// Keys are posted; the window handles everything posted before this
	// returns (an unknown message is answered once it's reached).
	void _Sync()
	{
		BMessage ping('ping'), reply;
		BMessenger(fMain).SendMessage(&ping, &reply);
	}

	void _Steps()
	{
		snooze(4 * kSettle);   // the chat opens and the entry takes the focus
		_Take();

		_Type("hello");
		_Expect("typing in the entry sends nothing", {});

		// Search → Find… by its shortcut: the find field takes the focus.
		_Key("f", 0x3f, B_COMMAND_KEY);
		_Expect("Find… with text in the entry sends nothing", {});
		_ExpectText("entry", "hello");

		_Focus("find");   // where Find… put it, unless something took it back
		_Type("abc");
		_Focus("entry");
		_Expect("leaving the find field doesn't search", {});

		_Type(" there");
		_Focus("chat search");
		_Expect("clicking the chat search with text in the entry sends nothing", {});
		_ExpectText("entry", "hello there");

		_Focus("entry");
		_Enter();
		_Expect("Enter sends the entry's text, even unchanged since it took the focus",
			{"send:hello there"});
		_ExpectText("entry", "");

		_Type("two");
		_Enter();
		_Expect("Enter sends what was typed", {"send:two"});

		_Enter();
		_Expect("Enter in an empty entry sends nothing", {});

		_Type("three");
		_Sync();
		if (fMain->Lock()) {
			if (BButton* send = dynamic_cast<BButton*>(fMain->FindView("send")))
				send->Invoke();
			fMain->Unlock();
		}
		_Expect("the Send button sends", {"send:three"});

		_Focus("find");
		_Enter();
		_Expect("Enter in the find field searches, even unchanged since it took the focus",
			{"find:abc"});
		_Enter();
		_Expect("Enter in the find field searches again", {"find:abc"});

		// In a group, Enter picks the @-suggestion shown, and doesn't send.
		BMessage select(kMsgSelectChat);
		select.AddInt64("id", kGroup);
		fMain->PostMessage(&select);
		snooze(2 * kSettle);
		_Take();
		_Type("hi @mi");
		snooze(2 * kSettle);   // the suggestions open
		_Enter();
		_Expect("Enter with the mention suggestions open sends nothing", {});
		_ExpectText("entry", "hi @mira_o ");
		_Enter();
		_Expect("then Enter sends", {"send:hi @mira_o "});

		printf("%s\n", sFailures == 0 ? "all passed" : "FAILED");
		fflush(stdout);
		if (fMain->Lock())
			fMain->Quit();
		PostMessage(B_QUIT_REQUESTED);
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
