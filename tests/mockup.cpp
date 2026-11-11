// SPDX-License-Identifier: GPL-3.0-or-later
// The real main window with made-up chats, for screenshots: no TDLib, no
// account, its own signature (so it runs next to Paper Plane).
//   make mockup && build/PaperPlaneMockup group|media|emoji|stickers|reply MEDIA_DIR
// Prints the main window's content frame, for cropping to just it.
#include <Application.h>
#include <File.h>
#include <MessageRunner.h>
#include <TimeFormat.h>
#include <Window.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "../src/app/MainWindow.h"
#include "../src/app/Messages.h"
#include "../src/core/ChatHistory.h"
#include "../src/core/Format.h"

namespace {

enum {
	kMsgShowEmoji = 'shem',      // MainWindow's own codes
	kMsgShowStickers = 'shst',
	kMsgReply = 'rply',
	kMsgNextStep = 'next'
};

const int64 kGroup = -1001, kPrivate = 2002, kChannel = -1003;

std::string sDir;
std::string sScene;


std::string
Media(const char* name)
{
	return sDir + "/" + name;
}


std::string
ReadFile(const std::string& path)
{
	std::string data;
	if (FILE* f = fopen(path.c_str(), "rb")) {
		char buffer[4096];
		size_t n;
		while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
			data.append(buffer, n);
		fclose(f);
	}
	return data;
}


BString
Clock(time_t when)
{
	// The system's short time, as the app shows it.
	BString text;
	BTimeFormat().Format(text, when, B_SHORT_TIME_FORMAT);
	return text;
}


// One history row with every field the window reads (parallel arrays).
struct Row {
	int64 id = 0;
	BString sender, text, time, forward, replySender, replyText, image, emoji;
	bool outgoing = false, read = false, pending = false, failed = false;
	bool edited = false, service = false, spoiler = false, secret = false;
	int32 mediaKind = 0, mediaWidth = 0, mediaHeight = 0, duration = 0;
	std::string mini;
	bool showAvatar = false;
	int64 senderKey = 0;
	BString avatar, initials;
	std::vector<pp::Span> spans;
	BString reactions;
};


void
Add(BMessage& list, const Row& r)
{
	list.AddInt64("id", r.id);
	list.AddString("sender", r.sender);
	list.AddString("text", r.text);
	list.AddString("time", r.time);
	list.AddBool("outgoing", r.outgoing);
	list.AddBool("read", r.read);
	list.AddBool("pending", r.pending);
	list.AddBool("failed", r.failed);
	list.AddBool("edited", r.edited);
	list.AddBool("service", r.service);
	list.AddString("forward", r.forward);
	list.AddString("reply_sender", r.replySender);
	list.AddString("reply_text", r.replyText);
	list.AddInt32("media_kind", r.mediaKind);
	list.AddString("image", r.image);
	AddBytes(list, "mini", r.mini);
	list.AddInt32("media_w", r.mediaWidth);
	list.AddInt32("media_h", r.mediaHeight);
	list.AddBool("spoiler", r.spoiler);
	list.AddBool("secret", r.secret);
	list.AddString("emoji", r.emoji);
	list.AddInt32("duration", r.duration);
	list.AddBool("show_avatar", r.showAvatar);
	list.AddInt64("sender_key", r.senderKey);
	list.AddString("avatar", r.avatar);
	AddBytes(list, "avatar_mini", "");
	list.AddString("initials", r.initials);
	list.AddString("spans", pp::EncodeSpans(r.spans).c_str());
	list.AddString("reactions", r.reactions);
}


struct Person {
	const char* name;
	const char* initials;
	int64 key;
	const char* avatar;   // nullptr: initials
};

const Person kMira = {"Mira Okafor", "MO", 11, "avatar0.jpg"};
const Person kJonas = {"Jonas Lindqvist", "JL", 12, "avatar1.jpg"};
const Person kPriya = {"Priya Raman", "PR", 13, nullptr};
const Person kTeo = {"Teo Marchetti", "TM", 14, "avatar3.jpg"};


class MockApp : public BApplication {
public:
	MockApp() : BApplication("application/x-vnd.paper-plane-mockup") {}

	void ReadyToRun() override
	{
		fMain = new MainWindow(BRect(20, 40, 1000, 740), false,
			{"\xF0\x9F\x91\x8D", "\xF0\x9F\x8E\x89", "\xE2\x9D\xA4", "\xF0\x9F\x98\x82"});
		fMain->Show();
		_SendChats();
		BMessage lists(kMsgChatLists);
		const char* keys[] = {"main", "folder:3", "folder:5", "archive"};
		const char* names[] = {"All chats", "Work", "Family", "Archive"};
		for (int i = 0; i < 4; i++) {
			lists.AddString("key", keys[i]);
			lists.AddString("name", names[i]);
		}
		fMain->PostMessage(&lists);
		snooze(300000);
		BMessage select(kMsgSelectChat);
		select.AddInt64("id", sScene == "media" || sScene == "media2" || sScene == "stickers"
			? kPrivate : kGroup);
		fMain->PostMessage(&select);
		BMessage next(kMsgNextStep);
		BMessageRunner::StartSending(this, &next, 1200000, 1);
	}

	void MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case kMsgOpenChat:
			{
				int64 id = message->GetInt64("id", 0);
				_SendHistory(id);
				BMessage status(kMsgChatStatus);
				status.AddInt64("chat_id", id);
				status.AddString("status", id == kGroup ? "Jonas is typing…" : "last seen today at 22:41");
				status.AddBool("active", id == kGroup);
				fMain->PostMessage(&status);
				break;
			}
			case kMsgNextStep:
				if (sScene == "emoji")
					fMain->PostMessage(kMsgShowEmoji);
				else if (sScene == "stickers")
					fMain->PostMessage(kMsgShowStickers);
				else if (sScene == "reply") {
					BMessage reply(kMsgReply);
					reply.AddInt64("message_id", 106);
					fMain->PostMessage(&reply);
				}
				snooze(700000);
				if (fMain->Lock()) {
					BRect f = fMain->Frame();  // content only
					printf("frame %.0f %.0f %.0f %.0f\n", f.left, f.top, f.right, f.bottom);
					fflush(stdout);
					fMain->Unlock();
				}
				break;
			case kMsgMessageProperties:
			{
				// Everything allowed, for the message menu.
				BMessage props(kMsgMessageProperties);
				props.AddInt64("chat_id", message->GetInt64("chat_id", 0));
				props.AddInt64("message_id", message->GetInt64("message_id", 0));
				for (const char* name : {"can_reply", "can_edit", "can_delete_self",
						"can_delete_all", "can_copy", "can_forward"})
					props.AddBool(name, true);
				fMain->PostMessage(&props);
				break;
			}
			case kMsgFind:
			{
				if (BString(message->GetString("query", "")).IsEmpty())
					break;
				BMessage result(kMsgFindResult);
				result.AddInt32("index", 0);
				result.AddInt32("count", 3);
				fMain->PostMessage(&result);
				fHighlight = 106;
				_SendHistory(kGroup);
				fHighlight = 0;
				break;
			}
			case kMsgStickerSetsWanted:
			{
				BMessage sets(kMsgStickerSets);
				const char* titles[] = {"Paper Plane", "Smiles", "Haiku friends"};
				for (int i = 0; i < 3; i++) {
					sets.AddInt64("id", i + 1);
					sets.AddString("title", titles[i]);
				}
				fMain->PostMessage(&sets);
				break;
			}
			case kMsgStickerSetWanted:
			{
				BMessage list(kMsgStickers);
				list.AddInt64("set_id", message->GetInt64("set_id", 0));
				for (int i = 0; i < 11; i++) {
					list.AddInt32("file_id", i + 1);
					list.AddInt32("width", 512);
					list.AddInt32("height", 512);
					list.AddString("emoji", "\xF0\x9F\x98\x80");
					std::string sticker = Media(i % 3 == 0 ? "sticker2.webp"
						: i % 3 == 1 ? "sticker0.webp" : "sticker1.webp");
					list.AddString("image", sticker.c_str());
				}
				fMain->PostMessage(&list);
				break;
			}
			default:
				BApplication::MessageReceived(message);
		}
	}

private:
	void _SendChats()
	{
		time_t now = time(nullptr);
		struct Chat {
			int64 id;
			const char* title;
			const char* text;
			const char* sender;
			int64 ago;
			int32 unread, mentions;
			bool pinned, read;
			const char* photo;
		} chats[] = {
			{kGroup, "Renku testers", "Nightly build00079 boots on my ThinkPad", "Jonas", 120, 3, 1, true, false, "renku.png"},
			{kPrivate, "Mira Okafor", "Photo", "", 600, 0, 0, true, true, "avatar0.jpg"},
			{kChannel, "Paper Plane news", "Stickers and emoji have landed", "", 3600, 12, 0, false, false, "plane.jpg"},
			{2004, "Teo Marchetti", "See you at BeGeistert!", "You", 3 * 3600, 0, 0, false, true, "avatar3.jpg"},
			{2005, "Priya Raman", "The driver builds now", "", 26 * 3600, 0, 0, false, false, nullptr},
			{2006, "Saved Messages", "notes.pdf", "", 2 * 86400, 0, 0, false, false, nullptr},
			{2007, "Jonas Lindqvist", "Sticker", "", 3 * 86400, 0, 0, false, false, "avatar1.jpg"},
			{-1008, "Haiku app makers", "Who has a WebP sample?", "Ana", 4 * 86400, 0, 0, false, false, nullptr},
			{2009, "Lena Hoffmann", "Thanks for the port!", "", 9 * 86400, 0, 0, false, false, "avatar4.jpg"},
			{2010, "Kofi Mensah", "Voice message", "", 20 * 86400, 0, 0, false, false, "avatar5.jpg"},
			{2011, "Sam Ito", "Ok", "You", 40 * 86400, 0, 0, false, true, "avatar2.jpg"},
		};
		BMessage list(kMsgChatList);
		for (const Chat& c : chats) {
			list.AddInt64("id", c.id);
			list.AddString("title", c.title);
			list.AddString("text", c.text);
			list.AddString("sender", c.sender);
			list.AddInt64("date", now - c.ago);
			list.AddInt32("unread", c.unread);
			list.AddInt32("mentions", c.mentions);
			list.AddBool("pinned", c.pinned);
			list.AddBool("read", c.read);
			list.AddString("typing", c.id == 2004 ? "typing…" : "");
			list.AddString("draft", c.id == 2005 ? "I'll test it tonight and" : "");
			list.AddBool("muted", c.id == kChannel);
			list.AddBool("marked_unread", c.id == 2009);
			list.AddInt32("kind", c.id < 0 ? (c.id == kChannel ? 2 : 1) : 0);
			list.AddString("photo", c.photo != nullptr ? Media(c.photo).c_str() : "");
			AddBytes(list, "photo_mini", "");
		}
		fMain->PostMessage(&list);
	}

	Row _From(const Person& p, int64 id, const char* text, time_t when)
	{
		Row r;
		r.id = id;
		r.sender = p.name;
		r.text = text;
		r.time = Clock(when);
		r.showAvatar = true;
		r.senderKey = p.key;
		r.initials = p.initials;
		if (p.avatar != nullptr)
			r.avatar = Media(p.avatar).c_str();
		return r;
	}

	Row _Mine(int64 id, const char* text, time_t when, bool read = true)
	{
		Row r;
		r.id = id;
		r.text = text;
		r.time = Clock(when);
		r.outgoing = true;
		r.read = read;
		return r;
	}

	Row _Date(const char* text)
	{
		Row r;
		r.text = text;
		r.service = true;
		return r;
	}

	void _SendHistory(int64 chatId)
	{
		time_t now = time(nullptr);
		BMessage list(kMsgHistory);
		list.AddInt64("chat_id", chatId);
		list.AddBool("scroll_end", fHighlight == 0);
		if (fHighlight != 0) {
			list.AddInt64("highlight", fHighlight);
			list.AddInt64("scroll_to", fHighlight);
		}

		if (chatId == kGroup) {
			Add(list, _Date("Yesterday"));
			Add(list, _From(kTeo, 101, "Has anyone tried the new Paper Plane build on real hardware?", now - 86400));
			Row r = _Mine(102, "Running it on my desktop since this morning, no crashes so far", now - 86000);
			Add(list, r);
			Add(list, _Date("Today"));
			r = _From(kMira, 103, "Here's the boot screen on the T480", now - 5400);
			r.mediaKind = 1;
			r.image = Media("renku.jpg").c_str();
			r.mediaWidth = 1920;
			r.mediaHeight = 1080;
			Add(list, r);
			Add(list, _From(kMira, 104, "Nightly build00079, fresh install", now - 5350));
			if (sScene == "unread") {
				Row marker = _Date("Unread messages");
				Add(list, marker);
				list.ReplaceBool("scroll_end", false);
				list.AddInt64("scroll_to", 105);
				list.AddBool("more_below", true);
			}
			r = _From(kJonas, 105, "Nightly build00079 boots on my ThinkPad too", now - 3000);
			r.replySender = "Mira Okafor";
			r.replyText = "Here's the boot screen on the T480";
			Add(list, r);
			r = _From(kJonas, 106, "The WiFi driver picked up my network straight away. "
				"Suspend still doesn't come back, I'll attach the syslog later.", now - 2900);
			r.edited = true;
			Add(list, r);
			r = _From(kPriya, 107, "Release notes for this week", now - 1800);
			r.forward = "Forwarded from Paper Plane news";
			Add(list, r);
			r = _From(kPriya, 108, "", now - 1700);
			r.mediaKind = 2;
			r.image = Media("sticker2.webp").c_str();
			r.mediaWidth = r.mediaHeight = 512;
			r.sender = "";
			Add(list, r);
			r = _From(kTeo, 111, "Build with make -j4 first, then see the guide at "
				"https://www.haiku-os.org/guides. The release is secret until Friday.", now - 900);
			{
				std::string t = r.text.String();
				auto span = [&](const char* word, uint32_t style, const char* url = "") {
					pp::Span sp;
					sp.start = (int32_t)t.find(word);
					sp.length = (int32_t)strlen(word);
					sp.style = style;
					sp.url = url;
					r.spans.push_back(sp);
				};
				span("make -j4", pp::kCode);
				span("first", pp::kBold);
				span("https://www.haiku-os.org/guides", pp::kLink, "https://www.haiku-os.org/guides");
				span("secret until Friday", pp::kSpoiler);
				span("release", pp::kItalic);
			}
			Add(list, r);
			r = _Mine(109, "Great, thanks everyone! I'll look at suspend next.", now - 600);
			r.reactions = "\xF0\x9F\x91\x8D\t3\t0\n\xE2\x9D\xA4\t1\t1\n";
			Add(list, r);
			r = _Mine(110, "Sending the logs in a minute", now - 120, false);
			Add(list, r);
		} else {
			// "media": photo, video, round video; "media2" (and the sticker
			// scene): spoiler, sticker, GIF.
			bool first = sScene == "media";
			Add(list, _Date("Today"));
			Row r;
			if (first) {
			r.id = 201; r.time = Clock(now - 7200); r.mediaKind = 1;
			r.image = Media("moon.jpg").c_str(); r.mediaWidth = 1920; r.mediaHeight = 1080;
			r.text = "Took this on the way home";
			Add(list, r);
			r = _Mine(202, "Wow, what a sky", now - 7000);
			Add(list, r);
			r = Row(); r.id = 203; r.time = Clock(now - 6000); r.mediaKind = 3;
			r.image = Media("light.jpg").c_str(); r.mediaWidth = 1280; r.mediaHeight = 720;
			r.duration = 142; r.text = "And a short clip";
			Add(list, r);
			r = _Mine(204, "", now - 5000);
			r.mediaKind = 5; r.image = Media("note.jpg").c_str();
			r.mediaWidth = r.mediaHeight = 384; r.duration = 9;
			Add(list, r);
			} else {
			r = Row(); r.id = 205; r.time = Clock(now - 4000); r.mediaKind = 1;
			r.mini = ReadFile(Media("renku_mini.jpg")); r.mediaWidth = 1920; r.mediaHeight = 1080;
			r.spoiler = true; r.text = "Don't look before the release!";
			Add(list, r);
			r = _Mine(206, "", now - 3000);
			r.mediaKind = 2; r.image = Media("sticker0.webp").c_str();
			r.mediaWidth = r.mediaHeight = 512;
			Add(list, r);
			r = Row(); r.id = 207; r.time = Clock(now - 900); r.mediaKind = 4;
			r.image = Media("light.jpg").c_str(); r.mediaWidth = 1280; r.mediaHeight = 720;
			Add(list, r);
			r = _Mine(208, "Ha, perfect", now - 600);
			Add(list, r);
			// A poll and a location, as the core renders them.
			pp::json poll = {{"@type", "messagePoll"}, {"poll", {{"question", {{"text", "Release on Friday?"}}},
				{"options", pp::json::array({
					{{"text", {{"text", "Yes"}}}, {"vote_percentage", 67}, {"is_chosen", true}},
					{{"text", {{"text", "Next week"}}}, {"vote_percentage", 33}}})},
				{"total_voter_count", 3}, {"can_see_results", true}}}};
			r = Row(); r.id = 209; r.time = Clock(now - 500);
			r.text = pp::MessageText(poll).c_str();
			Add(list, r);
			pp::json place = {{"@type", "messageLocation"},
				{"location", {{"latitude", 45.4642}, {"longitude", 9.19}}}};
			r = _Mine(210, "", now - 400);
			r.text = pp::MessageText(place).c_str();
			r.spans = pp::FindLinks(r.text.String());
			Add(list, r);
			}
		}
		fMain->PostMessage(&list);
	}

	MainWindow* fMain = nullptr;
	int64 fHighlight = 0;
};

} // namespace


int
main(int argc, char** argv)
{
	sScene = argc > 1 ? argv[1] : "group";
	sDir = argc > 2 ? argv[2] : "/build/mock-media";
	MockApp app;
	app.Run();
	return 0;
}
