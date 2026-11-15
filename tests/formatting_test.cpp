// SPDX-License-Identifier: GPL-3.0-or-later
// Formatting in what's sent: the Markdown typed in the message box read by
// TDLib (bold, italic, strikethrough, spoiler, code, code blocks, links),
// with mentions picked from the suggestions kept; text TDLib can't read goes
// as typed. Editing a message puts it back in the box as that Markdown, and
// saving sends the same formatting again (a caption: kept above its media).
// Runs the real main window and TDLib's own parser.
//   make formatting-test   (Haiku; opens a window for a few seconds)
#include <Application.h>
#include <Autolock.h>
#include <Locker.h>
#include <Messenger.h>
#include <OS.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include <td/telegram/td_json_client.h>

#include "../src/app/Formatting.h"
#include "../src/app/MainWindow.h"
#include "../src/app/Messages.h"
#include "../src/core/ChatHistory.h"

using pp::json;

namespace {

const int64 kGroup = -1005;
const int64 kBold = 71, kCaptioned = 72;
const int64 kAnn = 5;
const bigtime_t kSettle = 300000;   // for the window to handle a message

// The window's message to the app for an edit's Markdown (Messages.h),
// spelt out so that the test builds without the change too (red); the
// window's own Edit, Send and Cancel.
const uint32 kEditMarkdown = 'edmd', kEdit = 'edit', kSend = 'send', kCancel = 'ccmp';

int sFailures = 0;


void
Check(bool ok, const char* what, const std::string& detail = "")
{
	printf("%s: %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : " - ",
		detail.c_str());
	fflush(stdout);
	sFailures += !ok;
}


json
Entity(int offset, int length, const char* type)
{
	return {{"@type", "textEntity"}, {"offset", offset}, {"length", length},
		{"type", {{"@type", type}}}};
}


json
MentionOf(int offset, int length, int64 user)
{
	return {{"@type", "textEntity"}, {"offset", offset}, {"length", length},
		{"type", {{"@type", "textEntityTypeMentionName"}, {"user_id", user}}}};
}


// "bold" as kMsgSendText or kMsgEditText carries it.
BMessage
Typed(uint32 what, const char* text, const pp::Mentions& mentions = {})
{
	BMessage message(what);
	message.AddInt64("chat_id", kGroup);
	message.AddInt64("message_id", kBold);
	message.AddString("text", text);
	message.AddInt64("reply_to", 0);
	message.AddBool("silent", false);
	message.AddInt64("send_at", 0);
	message.AddBool("when_online", false);
	for (const auto& mention : mentions) {
		message.AddString("mention_name", mention.first.c_str());
		message.AddInt64("mention_user", mention.second);
	}
	return message;
}


void
CheckText(const char* what, const json& got, const json& expected)
{
	Check(got == expected, what, got == expected ? ""
		: "expected " + expected.dump() + ", got " + got.dump());
}


// The text a sendMessage or edit would send (no entities: an empty list).
json
SentText(const json& request)
{
	json text = request.contains("caption") ? request["caption"]
		: request["input_message_content"]["text"];
	if (!text.contains("entities"))
		text["entities"] = json::array();
	return text;
}


json
Formatted(const char* text, const json& entities)
{
	return {{"@type", "formattedText"}, {"text", text}, {"entities", entities}};
}


// Each Markdown marker, read by TDLib.
void
TestSending()
{
	BMessage send = Typed(kMsgSendText,
		"**bold** __italic__ ~~gone~~ ||secret|| `code` [site](https://example.org)");
	json request = OutgoingText(&send);
	Check(request["@type"] == "sendMessage", "a typed message is a sendMessage");
	json link = Entity(29, 4, "textEntityTypeTextUrl");
	link["type"]["url"] = "https://example.org/";
	CheckText("every marker becomes an entity", SentText(request),
		Formatted("bold italic gone secret code site", json::array({
			Entity(0, 4, "textEntityTypeBold"), Entity(5, 6, "textEntityTypeItalic"),
			Entity(12, 4, "textEntityTypeStrikethrough"), Entity(17, 6, "textEntityTypeSpoiler"),
			Entity(24, 4, "textEntityTypeCode"), link})));

	BMessage block = Typed(kMsgSendText, "```cpp\nint x;\n```");
	json pre = Entity(0, 7, "textEntityTypePreCode");   // its last line break too
	pre["type"]["language"] = "cpp";
	CheckText("a code block keeps its language", SentText(OutgoingText(&block)),
		Formatted("int x;\n", json::array({pre})));

	// A mention by name (a person without a username), after Markdown that
	// TDLib takes out: it moves back with the text.
	BMessage mention = Typed(kMsgSendText, "**Hi** Ann, ok?", {{"Ann", kAnn}});
	CheckText("a mention moves with the text", SentText(OutgoingText(&mention)),
		Formatted("Hi Ann, ok?", json::array({Entity(0, 2, "textEntityTypeBold"),
			MentionOf(3, 3, kAnn)})));

	// A caption for files sent: the same.
	CheckText("a caption is read too", TypedText("Trip, __day one__", {}),
		Formatted("Trip, day one", json::array({Entity(6, 7, "textEntityTypeItalic")})));

	BMessage plain = Typed(kMsgSendText, "nothing to format here");
	CheckText("plain text goes as typed", SentText(OutgoingText(&plain)),
		Formatted("nothing to format here", json::array()));

	BMessage broken = Typed(kMsgSendText, "**never closed, Ann", {{"Ann", kAnn}});
	CheckText("text TDLib can't read goes as typed, with its mentions",
		SentText(OutgoingText(&broken)),
		Formatted("**never closed, Ann", json::array({MentionOf(16, 3, kAnn)})));

	BMessage scheduled = Typed(kMsgSendText, "**later**");
	scheduled.ReplaceInt64("send_at", 2000000000);
	scheduled.ReplaceBool("silent", true);
	json later = OutgoingText(&scheduled);
	Check(later["@extra"] == "send_scheduled"
		&& later["options"]["scheduling_state"]["send_date"] == 2000000000
		&& later["options"]["disable_notification"] == true
		&& SentText(later)["entities"].size() == 1, "send options are kept", later.dump());
}


// The window's Edit, the app's Markdown for it, and Save.
class TestApp : public BApplication {
public:
	TestApp() : BApplication("application/x-vnd.powergram-formatting-test") {}

	void ReadyToRun() override
	{
		fMain = new MainWindow(BRect(40, 40, 900, 680), false, {});
		fMain->Show();
		BMessage list(kMsgChatList);
		list.AddInt64("id", kGroup);
		list.AddString("title", "Haiku porters");
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
		list.AddInt32("kind", 1);
		list.AddBool("secret", false);
		list.AddString("photo", "");
		AddBytes(list, "photo_mini", "");
		fMain->PostMessage(&list);
		BMessage select(kMsgSelectChat);
		select.AddInt64("id", kGroup);
		fMain->PostMessage(&select);
		resume_thread(spawn_thread(_Run, "steps", B_NORMAL_PRIORITY, this));
	}

	void MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case kEditMarkdown:
			{
				int64 id = message->GetInt64("message_id", 0);
				_Log("markdown:" + std::to_string(id));
				if (fHoldDraft) {
					fHeld = _Message(id);
					break;
				}
				// As the app does once TDLib has the message.
				BMessage draft = EditDraftMessage(_Message(id));
				fMain->PostMessage(&draft);
				break;
			}
			case kMsgEditText:
			{
				int64 id = message->GetInt64("message_id", 0);
				const json& sent = _Content(id);
				json request = OutgoingEdit(message, sent["@type"] != "messageText",
					sent.value("show_caption_above_media", false));
				BAutolock lock(fLock);
				fEvents.push_back("edit:" + std::to_string(id) + ":"
					+ message->GetString("text", ""));
				fEdits.push_back(request);
				break;
			}
			case kMsgSendText:
			{
				std::string event = std::string("send:") + message->GetString("text", "");
				const char* name;
				for (int32 i = 0; message->FindString("mention_name", i, &name) == B_OK; i++)
					event += std::string(":") + name;
				_Log(event);
				break;
			}
			default:
				break;
		}
	}

private:
	// The messages as TDLib has them: "Ann" (no username) mentioned by
	// name in one, a photo's formatted caption above it in the other.
	static json _Content(int64 id)
	{
		if (id == kCaptioned) {
			return {{"@type", "messagePhoto"}, {"show_caption_above_media", true},
				{"caption", Formatted("Look at this", json::array({
					Entity(8, 4, "textEntityTypeItalic")}))}};
		}
		return {{"@type", "messageText"}, {"text", Formatted("bold, Ann and code",
			json::array({Entity(0, 4, "textEntityTypeBold"), MentionOf(6, 3, kAnn),
				Entity(14, 4, "textEntityTypeCode")}))}};
	}

	static json _Message(int64 id)
	{
		return {{"@type", "message"}, {"id", id}, {"chat_id", kGroup},
			{"is_outgoing", true}, {"content", _Content(id)}};
	}

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

	// A round trip through the window: what was posted before is handled.
	void _Sync()
	{
		BMessage reply;
		BMessenger(fMain).SendMessage('sync', &reply);
	}

	// What the app was asked to do since the last call; the app's own
	// answers to the window are handled by then.
	std::vector<std::string> _Take()
	{
		snooze(kSettle);
		_Sync();
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

	std::string _Entry(bool* allSelected = nullptr)
	{
		std::string text;
		if (fMain->Lock()) {
			BTextControl* entry = dynamic_cast<BTextControl*>(fMain->FindView("entry"));
			text = entry->Text();
			if (allSelected != nullptr) {
				int32 from, to;
				entry->TextView()->GetSelection(&from, &to);
				*allSelected = from == 0 && to == entry->TextView()->TextLength();
			}
			fMain->Unlock();
		}
		return text;
	}

	void _SetEntry(const char* text)
	{
		if (fMain->Lock()) {
			dynamic_cast<BTextControl*>(fMain->FindView("entry"))->SetText(text);
			fMain->Unlock();
		}
	}

	void _Post(uint32 what, int64 id = 0)
	{
		BMessage message(what);
		if (id != 0)
			message.AddInt64("message_id", id);
		fMain->PostMessage(&message);
	}

	// The two messages as the app lists them: plain text, the caption.
	void _History()
	{
		BMessage history(kMsgHistory);
		history.AddInt64("chat_id", kGroup);
		history.AddBool("scroll_end", true);
		auto add = [&](int64 id, const char* text, int32 mediaKind) {
			history.AddInt64("id", id);
			history.AddString("sender", "");
			history.AddString("text", text);
			history.AddString("time", "10:00");
			history.AddBool("outgoing", true);
			history.AddBool("read", true);
			for (const char* name : {"pending", "failed", "edited", "service", "spoiler",
					"secret", "show_avatar"})
				history.AddBool(name, false);
			for (const char* name : {"forward", "reply_sender", "reply_text", "image", "emoji",
					"animation", "avatar", "initials", "reactions", "file_name", "file_title",
					"file_type", "spans"})
				history.AddString(name, "");
			for (const char* name : {"mini", "avatar_mini", "waveform"})
				AddBytes(history, name, "");
			history.AddInt32("media_kind", mediaKind);
			history.AddInt32("media_w", mediaKind != 0 ? 800 : 0);
			history.AddInt32("media_h", mediaKind != 0 ? 600 : 0);
			history.AddInt32("animation_format", 0);
			history.AddInt32("duration", 0);
			history.AddInt64("sender_key", 0);
			history.AddInt64("file_size", 0);
			history.AddInt64("file_done", -1);
		};
		add(kBold, "bold, Ann and code", 0);
		add(kCaptioned, "Look at this", 1);
		fMain->PostMessage(&history);
	}

	json _LastEdit()
	{
		BAutolock lock(fLock);
		return fEdits.empty() ? json() : fEdits.back();
	}

	void _Steps()
	{
		_Take();
		_History();
		_Take();

		// Edit: the text at once, then its Markdown from the app.
		_Post(kEdit, kBold);
		_Expect("Edit asks the app for the message's Markdown", {"markdown:71"});
		bool selected = false;
		std::string entry = _Entry(&selected);
		Check(entry == "**bold**, Ann and `code`" && selected,
			"the box shows the message as Markdown, all selected", entry);

		// Save: the same formatting again, the mention by name too.
		_Post(kSend);
		_Expect("Save sends the edit", {"edit:71:**bold**, Ann and `code`"});
		json edit = _LastEdit();
		Check(edit["@type"] == "editMessageText", "a text message's edit is editMessageText",
			edit.dump());
		CheckText("the edit keeps the formatting and the mention", SentText(edit),
			_Content(kBold)["text"]);
		Check(_Entry().empty(), "the box is empty after Save");

		// A caption: edited as a caption, kept above the photo.
		_Post(kEdit, kCaptioned);
		_Expect("Edit asks for a caption's Markdown", {"markdown:72"});
		entry = _Entry();
		Check(entry == "Look at __this__", "the box shows the caption as Markdown", entry);
		_SetEntry("Look at **this**");
		_Post(kSend);
		_Expect("Save sends the caption", {"edit:72:Look at **this**"});
		edit = _LastEdit();
		Check(edit["@type"] == "editMessageCaption" && edit["show_caption_above_media"] == true,
			"a caption's edit is editMessageCaption, still above the photo", edit.dump());
		CheckText("the caption's new formatting", SentText(edit),
			Formatted("Look at this", json::array({Entity(8, 4, "textEntityTypeBold")})));

		// Typing before the Markdown comes: what was typed stays.
		fHoldDraft = true;
		_Post(kEdit, kBold);
		_Expect("Edit asks again", {"markdown:71"});
		_SetEntry("bold, Ann and code, edited");
		BMessage late = EditDraftMessage(fHeld);
		fMain->PostMessage(&late);
		_Take();
		entry = _Entry();
		Check(entry == "bold, Ann and code, edited", "a late Markdown doesn't replace typing",
			entry);

		// Nor after the edit was given up, in the next message.
		_Post(kEdit, kBold);
		_Take();
		_Post(kCancel);
		_Take();
		_SetEntry("a new message");
		fMain->PostMessage(&late);
		_Take();
		entry = _Entry();
		Check(entry == "a new message", "a Markdown after Cancel is dropped", entry);
		_Post(kSend);
		_Expect("the next message has none of the edit's mentions",
			{"send:a new message"});
	}

	MainWindow* fMain = nullptr;
	BLocker fLock;
	std::vector<std::string> fEvents;
	std::vector<json> fEdits;
	bool fHoldDraft = false;
	json fHeld;
};

} // namespace


int
main()
{
	// TDLib's log: errors only.
	td_execute(R"({"@type":"setLogVerbosityLevel","new_verbosity_level":1})");
	TestSending();
	TestApp app;
	app.Run();
	printf("%s: %d failure(s)\n", sFailures == 0 ? "ALL PASSED" : "FAILED", sFailures);
	return sFailures == 0 ? 0 : 1;
}
