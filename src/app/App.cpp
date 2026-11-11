// SPDX-License-Identifier: GPL-3.0-or-later
// Powergram: the application. Owns the TDLib client, the login
// state machine and the chat list; the windows only show what it sends.
#include <Alert.h>
#include <Application.h>
#include <Invoker.h>
#include <MessageRunner.h>
#include <Beep.h>
#include <Bitmap.h>
#include <Entry.h>
#include <Path.h>
#include <TranslationUtils.h>
#include <Roster.h>
#include <Screen.h>
#include <TimeFormat.h>
#include <DateFormat.h>
#include <Notification.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <map>
#include <set>
#include <sys/utsname.h>

#if __has_include(<AboutWindow.h>)
#	include <AboutWindow.h>
#else
#	include <private/interface/AboutWindow.h>
#endif

#include "../core/AuthFlow.h"
#include "../core/ChatHistory.h"
#include "../core/ChatInfo.h"
#include "../core/ChatList.h"
#include "../core/Files.h"
#include "../core/Presence.h"
#include "../core/Stickers.h"
#include "ChatInfoWindow.h"
#include "LoginWindow.h"
#include "MainWindow.h"
#include "Messages.h"
#include "Settings.h"
#include "SettingsWindow.h"
#include "TdClient.h"
#include "VoicePlayer.h"

using pp::AuthStep;
using pp::json;

class App : public BApplication {
public:
	App();
	~App() override;

	void ArgvReceived(int32 argc, char** argv) override;
	void ReadyToRun() override;
	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;
	void AboutRequested() override;

private:
	pp::TdParams _Params() const;
	void _HandleObject(const json& object);
	void _HandleAuth();
	void _ScheduleChatFlush();
	void _FlushChats();
	void _Send(const json& request, bool urgent = false)
		{ fClient->Send(request, urgent); }
	void _CloseWindows();
	// Opens a chat at its first unread message, or with `latest` at the end.
	void _OpenChat(int64 id, bool latest = false);
	void _HandleHistoryReply(const json& messages);
	// Schedules a chat view update; at most ten a second (a busy chat
	// sends many updates, and each one is a full re-layout).
	void _FlushHistory(bool scrollToEnd = false);
	void _SendHistory();
	void _StartDownloads();
	void _OpenMedia(int64 chatId, int64 messageId);
	void _Launch(const std::string& path);
	void _SendFiles(const BMessage* message);
	void _SendStickerSets();
	void _SendChatStatus();
	std::string _TypingText(int64 chatId);
	void _SendStickers(bool update);
	void _ViewVisible();
	void _SetOnline(bool online);
	void _Notify(const json& message);
	void _ShowError(const char* text);

	Settings fSettings;
	bool fTestDc;
	std::unique_ptr<TdClient> fClient;
	std::unique_ptr<pp::AuthFlow> fAuth;
	pp::ChatList fChats;
	std::string fShownList = "main";   // the chat list on screen
	std::set<std::string> fLoadedLists;
	void _SendChatLists();
	pp::Names fNames;
	pp::FileStore fFiles;
	pp::Presence fPresence;
	AppSettings fPrefs;
	SettingsWindow* fSettingsWindow = nullptr;
	BMessageRunner* fPresenceRunner = nullptr;
	std::set<int32_t> fOpenWhenDownloaded;  // clicked photos on their way

	// Voice messages play here rather than in MediaPlayer.
	void _PlayVoice(const std::string& path, int64 messageId);
	void _StopVoice();
	VoicePlayer fVoice;
	int64 fVoiceMessage = 0;   // playing, or 0
	BMessageRunner* fVoiceRunner = nullptr;
	std::map<int32_t, int64> fPlayWhenDownloaded;  // file → message

	// The info window, for one chat at a time.
	void _ShowChatInfo(int64 chatId);
	void _SendChatInfo();
	BString _StatusText(int64 chatId, bool withTyping, bool* active = nullptr);
	ChatInfoWindow* fInfoWindow = nullptr;   // only touched through fInfoMessenger
	BMessenger fInfoMessenger;
	pp::ChatInfo fInfo;

	// The sticker picker: installed sets, stickers by set (0: recently
	// used), and the set it shows.
	std::vector<pp::StickerSetInfo> fStickerSets;
	std::map<int64, std::vector<pp::StickerItem>> fStickers;
	int64 fPickerSet = -1;
	std::unique_ptr<pp::ChatHistory> fHistory;   // the open chat, if any
	int fHistoryPages;        // history requests for the current page
	int64 fFirstUnread = 0;   // read up to here when the chat was opened
	int64 fScrollTo = 0;      // message to show first, once
	std::vector<int64_t> fFound;  // search results in the open chat, newest first
	int32 fFoundIndex = -1;
	int64 fHighlight = 0;     // the search result on screen
	void _ShowMessage(int64 messageId);
	void _SendFindResult();
	bool fLoadingHistory;
	pp::TypingThrottle fTyping;
	bool fActive;             // a window of ours is active
	int fOnline;              // last online status sent: -1 none, 0, 1
	BMessageRunner* fOnlineRunner;
	BMessageRunner* fHistoryRunner;
	bool fHistoryScrollEnd;
	std::string fBadApiKey;   // error to show once the key is asked again

	LoginWindow* fLogin;
	MainWindow* fMain;
	BMessageRunner* fFlushRunner;
	bool fQuitting;
	bool fClosed;
};


App::App()
	:
	BApplication(APP_SIGNATURE),
	fTestDc(false),
	fHistoryPages(0),
	fLoadingHistory(false),
	fActive(false),
	fOnline(-1),
	fOnlineRunner(nullptr),
	fHistoryRunner(nullptr),
	fHistoryScrollEnd(false),
	fLogin(nullptr),
	fMain(nullptr),
	fFlushRunner(nullptr),
	fQuitting(false),
	fClosed(false)
{
	fSettings.Load();
	fPrefs = AppSettings::FromMessage(&fSettings.preferences);
	// The new-message sound, chosen by the user in the Sounds preferences.
	add_system_beep_event("Powergram new message");
	const char* env = getenv("POWERGRAM_TEST_DC");
	fTestDc = env != nullptr && strcmp(env, "1") == 0;
}


App::~App()
{
	delete fFlushRunner;
	delete fOnlineRunner;
	delete fHistoryRunner;
	delete fPresenceRunner;
	delete fVoiceRunner;
}


void
App::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--test-dc") == 0)
			fTestDc = true;
		else if (strncmp(argv[i], "--open-chat=", 12) == 0 && fMain != nullptr) {
			// From a notification click (B_SINGLE_LAUNCH routes it here).
			BMessage select(kMsgSelectChat);
			select.AddInt64("id", atoll(argv[i] + 12));
			fMain->PostMessage(&select);
		}
	}
}


pp::TdParams
App::_Params() const
{
	pp::TdParams p;
	p.apiId = fSettings.apiId;
	p.apiHash = fSettings.apiHash.String();
	p.testDc = fTestDc;
	BPath db = Settings::Directory();
	db.Append(fTestDc ? "tdlib-test" : "tdlib");
	p.databaseDir = db.Path();

	struct utsname u;
	if (uname(&u) == 0) {
		p.systemVersion = std::string(u.sysname) + " " + u.release;
		p.deviceModel = u.machine;
	}
	p.appVersion = "0.1";
	return p;
}


void
App::ReadyToRun()
{
	fLogin = new LoginWindow(fTestDc);

	BRect frame = fSettings.mainFrame;
	bool placed = frame.IsValid()
		&& BScreen().Frame().Contains(frame.LeftTop() + BPoint(20, 20));
	if (!placed)
		frame.Set(0, 0, 380, 640);
	fMain = new MainWindow(frame, fTestDc, fSettings.recentEmoji);
	{
		BMessage apply(kMsgSettingsChanged);
		fPrefs.ToMessage(&apply);
		fMain->PostMessage(&apply);
	}
	if (!placed)
		fMain->CenterOnScreen();

	fAuth = std::make_unique<pp::AuthFlow>(_Params());
	fClient = std::make_unique<TdClient>(BMessenger(this));
	if (fClient->Start() != B_OK) {
		(new BAlert(APP_NAME, "Powergram could not start its Telegram "
			"connection.", "Quit", nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_STOP_ALERT))->Go();
		Quit();
	}
}


void
App::_HandleObject(const json& object)
{
	std::string type = pp::Type(object);

	// Files: TDLib reports progress with updateFile, and messages carry the
	// paths of what's already downloaded.
	std::vector<int32_t> completed = fFiles.Apply(object);
	if (!completed.empty()) {
		bool shown = false;
		for (int32_t id : completed) {
			if (fOpenWhenDownloaded.erase(id))
				_Launch(fFiles.Path(id));
			auto voice = fPlayWhenDownloaded.find(id);
			if (voice != fPlayWhenDownloaded.end()) {
				_PlayVoice(fFiles.Path(id), voice->second);
				fPlayWhenDownloaded.erase(voice);
			}
			shown = true;
		}
		if (shown && fHistory != nullptr)
			_FlushHistory();
		if (shown)
			_ScheduleChatFlush();
		if (shown && fPickerSet >= 0)
			_SendStickers(true);
		if (shown && fInfoWindow != nullptr)
			_SendChatInfo();
		_StartDownloads();
		if (type == "updateFile")
			return;
	}
	if (type == "updateFile")
		return;

	std::vector<json> requests;
	if (fAuth->Handle(object, requests)) {
		for (const json& r : requests)
			_Send(r, true);
		_HandleAuth();
		return;
	}

	fNames.Apply(object);
	if (fPresence.Apply(object, time(nullptr))) {
		int64 open = fHistory != nullptr ? fHistory->ChatId() : 0;
		bool relevant = open != 0 && (pp::Int(object, "chat_id") == open
			|| pp::Int(object, "user_id") == fPresence.PrivateUser(open)
			|| type == "updateBasicGroup" || type == "updateSupergroup"
			|| (type == "updateUser"
				&& pp::Int(pp::Field(object, "user"), "id") == fPresence.PrivateUser(open)));
		if (relevant)
			_SendChatStatus();
		if (type == "updateChatAction")
			_ScheduleChatFlush();
	}

	std::string extra = pp::Str(object, "@extra");
	if (extra == "sticker_sets") {
		if (type == "stickerSets") {
			fStickerSets = pp::ParseStickerSets(object);
			_SendStickerSets();
		}
		return;
	}
	if (extra.rfind("sticker_set:", 0) == 0) {
		int64 id = extra == "sticker_set:recent" ? 0 : atoll(extra.c_str() + 12);
		if (type == "stickers" || type == "stickerSet") {
			fStickers[id] = pp::ParseStickers(object);
			if (id == fPickerSet)
				_SendStickers(false);
		}
		return;
	}
	if (extra.rfind("download:", 0) == 0) {
		if (type == "error") {
			int32 id = atoi(extra.c_str() + 9);
			fFiles.DownloadFailed(id);
			fOpenWhenDownloaded.erase(id);
			_StartDownloads();
		}
		return;
	}
	if (extra.rfind("history:", 0) == 0) {
		_HandleHistoryReply(object);
		return;
	}
	if (extra.rfind("info:", 0) == 0) {
		if (fInfoWindow != nullptr && atoll(extra.c_str() + 5) == fInfo.chatId
			&& fInfo.Apply(object))
			_SendChatInfo();
		return;
	}
	if (extra.rfind("search:", 0) == 0) {
		if (fHistory != nullptr && atoll(extra.c_str() + 7) == fHistory->ChatId()) {
			fFound = type == "error" ? std::vector<int64_t>() : pp::FoundIds(object);
			fFoundIndex = fFound.empty() ? -1 : 0;
			_SendFindResult();
			if (!fFound.empty())
				_ShowMessage(fFound[0]);
		}
		return;
	}
	if (extra.rfind("reply:", 0) == 0) {
		if (fHistory != nullptr && type == "messages") {
			fHistory->AddReplyTargets(object);
			_FlushHistory();
		}
		return;
	}
	if (extra.rfind("props:", 0) == 0) {
		// "props:<chat>:<message>"
		int64 chatId = atoll(extra.c_str() + 6);
		int64 messageId = atoll(strchr(extra.c_str() + 6, ':') + 1);
		BMessage props(kMsgMessageProperties);
		props.AddInt64("chat_id", chatId);
		props.AddInt64("message_id", messageId);
		props.AddBool("can_reply", pp::Bool(object, "can_be_replied"));
		props.AddBool("can_edit", pp::Bool(object, "can_be_edited"));
		props.AddBool("can_delete_self", pp::Bool(object, "can_be_deleted_only_for_self"));
		props.AddBool("can_delete_all", pp::Bool(object, "can_be_deleted_for_all_users"));
		props.AddBool("can_copy", pp::Bool(object, "can_be_copied", true));
		props.AddBool("can_forward", pp::Bool(object, "can_be_forwarded"));
		fMain->PostMessage(&props);
		return;
	}
	if ((extra == "edit" || extra == "delete" || extra == "forward") && type == "error") {
		BString text(extra == "edit" ? "Could not edit the message. "
			: extra == "forward" ? "Could not forward the message. "
			: "Could not delete the message. ");
		text << pp::AuthFlow::ErrorText(object).c_str();
		_ShowError(text);
		return;
	}

	if (type == "error" && pp::AuthFlow::IsAuthReply(object)) {
		if (pp::AuthFlow::IsApiKeyError(object)) {
			// TDLib can't change its parameters: forget the key, restart
			// TDLib and ask again, with the error on the API key page.
			fBadApiKey = pp::AuthFlow::ErrorText(object);
			fSettings.apiId = 0;
			fSettings.apiHash = "";
			fSettings.Save();
			_Send({{"@type", "close"}}, true);
			return;
		}
		BMessage error(kMsgAuthError);
		error.AddString("text", pp::AuthFlow::ErrorText(object).c_str());
		fLogin->PostMessage(&error);
		return;
	}
	if (type == "error") {
		fprintf(stderr, "TDLib error: %s\n", object.dump().c_str());
		return;
	}

	if (type == "updateNewMessage") {
		int64 visible = fActive && fHistory != nullptr ? fHistory->ChatId() : 0;
		const json& message = pp::Field(object, "message");
		if (fChats.ShouldNotify(message, visible, time(nullptr)))
			_Notify(message);
	}

	if (fChats.Apply(object))
		_ScheduleChatFlush();
	if (type == "updateChatFolders")
		_SendChatLists();
	if (fHistory != nullptr && fHistory->Apply(object)) {
		bool newMessage = type == "updateNewMessage";
		_FlushHistory(newMessage
			&& pp::Bool(pp::Field(object, "message"), "is_outgoing"));
		if (newMessage)
			_ViewVisible();
	}
}


void
App::_OpenChat(int64 id, bool latest)
{
	// openChat/closeChat tell Telegram which chat is on screen, as the
	// official apps do; it affects read state and notifications.
	if (fHistory != nullptr) {
		_Send(pp::request::Typing(fHistory->ChatId(), false));
		_Send(pp::request::CloseChat(fHistory->ChatId()));
	}
	fHistory.reset();
	fTyping.Reset();
	fLoadingHistory = false;
	fFiles.ForgetWanted();  // the old chat's photos that haven't started
	fPlayWhenDownloaded.clear();
	if (id == 0)
		return;

	fHistory = std::make_unique<pp::ChatHistory>(id);
	fFirstUnread = 0;
	fScrollTo = 0;
	fFound.clear();
	fFoundIndex = -1;
	fHighlight = 0;
	const pp::Chat* openChat = fChats.Find(id);
	if (openChat != nullptr)
		fHistory->SetLastReadOutbox(openChat->lastReadOutbox);
	_Send(pp::request::OpenChat(id));
	_SendChatStatus();
	if (const pp::Chat* chat = fChats.Find(id)) {
		BMessage draft(kMsgDraft);
		draft.AddInt64("chat_id", id);
		draft.AddString("text", chat->draft.c_str());
		fMain->PostMessage(&draft);
	}
	fHistoryPages = 0;
	fLoadingHistory = true;
	if (!latest && openChat != nullptr && openChat->unread > 0 && openChat->lastReadInbox > 0
		&& openChat->lastReadInbox < openChat->lastMessageId) {
		// Unread messages: open at the first one, with what came before it.
		fFirstUnread = openChat->lastReadInbox;
		fHistory->SetAtLatest(false);
		_Send(pp::request::HistoryAround(id, openChat->lastReadInbox, 20, 39));
	} else {
		// One page; TDLib may answer with fewer at first (see _HandleHistoryReply).
		_Send(pp::request::History(id, 0, 40));
	}
}


void
App::_HandleHistoryReply(const json& messages)
{
	int64 chatId = std::atoll(pp::Str(messages, "@extra").c_str() + strlen("history:"));
	if (fHistory == nullptr || fHistory->ChatId() != chatId)
		return;
	if (pp::Type(messages) == "error") {
		fLoadingHistory = false;
		return;
	}
	int added = fHistory->AddHistory(messages);
	fHistoryPages++;
	if (const pp::Chat* chat = fChats.Find(chatId))
		if (!fHistory->AtLatest() && fHistory->NewestId() >= chat->lastMessageId)
			fHistory->SetAtLatest(true);
	if (fHistoryPages == 1 && fFirstUnread != 0) {
		// Scroll to the first unread message, once.
		for (const pp::Message& m : fHistory->Sorted())
			if (m.id > fFirstUnread && !m.outgoing) {
				fScrollTo = m.id;
				break;
			}
	}
	// TDLib's first answer often holds only what it has cached; ask again
	// from the oldest message, a few times at most, through the throttle.
	if (added > 0 && fHistory->Size() < 20 && fHistoryPages < 4) {
		_Send(pp::request::History(chatId, fHistory->OldestId(), 40));
	} else
		fLoadingHistory = false;
	_FlushHistory(fHistoryPages == 1 && fScrollTo == 0);
	_ViewVisible();
}


void
App::_FlushHistory(bool scrollToEnd)
{
	fHistoryScrollEnd |= scrollToEnd;
	if (fHistoryRunner != nullptr)
		return;
	BMessage flush(kMsgFlushHistory);
	fHistoryRunner = new BMessageRunner(BMessenger(this), &flush, 100000, 1);
}


void
App::_SendHistory()
{
	delete fHistoryRunner;
	fHistoryRunner = nullptr;
	bool scrollToEnd = fHistoryScrollEnd;
	fHistoryScrollEnd = false;
	if (fHistory == nullptr)
		return;
	int64 chatId = fHistory->ChatId();
	bool group = chatId < 0;  // TDLib: users' chats have positive ids

	BMessage list(kMsgHistory);
	list.AddInt64("chat_id", chatId);
	list.AddBool("scroll_end", scrollToEnd);
	list.AddBool("more_below", !fHistory->AtLatest());
	list.AddInt64("highlight", fHighlight);
	if (fScrollTo != 0) {
		list.AddInt64("scroll_to", fScrollTo);
		fScrollTo = 0;
	}

	// Every row adds every field: the window reads them as parallel arrays.
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
		std::string avatarMini;
		std::vector<pp::Span> spans;
		BString reactions;   // "emoji\tcount\tchosen" per line
		std::string waveform;
	};
	auto add = [&](const Row& r) {
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
		AddBytes(list, "avatar_mini", r.avatarMini);
		list.AddString("initials", r.initials);
		list.AddString("spans", pp::EncodeSpans(r.spans).c_str());
		list.AddString("reactions", r.reactions);
		AddBytes(list, "waveform", r.waveform);
	};
	auto summary = [](const pp::Message& m) {
		if (!m.text.empty())
			return BString(m.text.c_str());
		if (m.media.kind == pp::Media::kPhoto)
			return BString("Photo");
		if (m.media.kind == pp::Media::kSticker)
			return BString((m.media.bigEmoji ? m.media.emoji : m.media.emoji + " Sticker").c_str());
		if (m.media.kind == pp::Media::kVideo)
			return BString("Video");
		if (m.media.kind == pp::Media::kAnimation)
			return BString("GIF");
		if (m.media.kind == pp::Media::kVideoNote)
			return BString("Video message");
		if (m.media.kind == pp::Media::kVoice)
			return BString("Voice message");
		return BString("Message");
	};

	BTimeFormat timeFormat;
	BDateFormat dateFormat;
	int lastDay = -1;
	int64 lastSender = 0;  // name only on the first message of a run
	bool unreadMarked = false;
	std::vector<pp::Message> messages = fHistory->Sorted();
	for (size_t i = 0; i < messages.size(); i++) {
		const pp::Message& m = messages[i];
		// A centred date between days, as Paper Plane shows.
		time_t when = (time_t)m.date;
		struct tm local;
		localtime_r(&when, &local);
		int day = (local.tm_year + 1900) * 1000 + local.tm_yday;
		if (day != lastDay && m.date > 0) {
			lastDay = day;
			Row separator;
			dateFormat.Format(separator.text, when, B_LONG_DATE_FORMAT);
			separator.service = true;
			add(separator);
			lastSender = 0;
		}

		if (fFirstUnread != 0 && m.id > fFirstUnread && !m.outgoing && !unreadMarked) {
			// Where the unread messages start, as when the chat was opened.
			Row marker;
			marker.text = "Unread messages";
			marker.service = true;
			add(marker);
			unreadMarked = true;
			lastSender = 0;
		}

		Row r;
		r.id = m.id;
		bool channelPost = m.senderIsChat && m.senderId == chatId;
		if (group && !m.outgoing && !channelPost && !m.service) {
			r.sender = fNames.Sender(m).c_str();
			// The sender's picture beside their messages.
			pp::Photo photo = fNames.SenderPhoto(m);
			r.showAvatar = true;
			r.senderKey = m.senderIsChat ? -m.senderId : m.senderId;
			r.avatar = fFiles.Path(photo.fileId).c_str();
			r.avatarMini = photo.preview;
			r.initials = pp::Initials(r.sender.String()).c_str();
			if (r.avatar.IsEmpty() && photo.fileId != 0)
				fFiles.Want(photo.fileId, 3);
		}
		if (r.showAvatar && r.senderKey == lastSender
			&& m.replyToId == 0 && !m.forwarded)
			r.sender = "";
		lastSender = r.showAvatar ? r.senderKey : 0;
		r.text = m.text.c_str();
		r.spans = m.spans;
		for (const pp::Reaction& reaction : m.reactions)
			r.reactions << reaction.emoji.c_str() << "\t" << reaction.count << "\t"
				<< (reaction.chosen ? "1" : "0") << "\n";
		timeFormat.Format(r.time, (time_t)m.date, B_SHORT_TIME_FORMAT);
		r.outgoing = m.outgoing;
		r.read = m.outgoing && m.id <= fHistory->LastReadOutbox();
		r.pending = m.pending;
		r.failed = m.failed;
		r.edited = m.edited;
		r.service = m.service;

		if (m.forwarded) {
			std::string origin = fNames.ForwardOrigin(m);
			r.forward = origin.empty() ? "Forwarded" : ("Forwarded from " + origin).c_str();
		}

		if (m.replyToId != 0) {
			const pp::Message* target = fHistory->ReplyTarget(m);
			if (target != nullptr) {
				r.replySender = target->outgoing ? "You" : fNames.Sender(*target).c_str();
				if (r.replySender.IsEmpty())
					r.replySender = fChats.Find(chatId) ? fChats.Find(chatId)->title.c_str() : "";
				r.replyText = summary(*target);
			} else {
				r.replySender = "Reply";
				r.replyText = m.replyText.c_str();
			}
			if (!m.replyQuote.empty())
				r.replyText = m.replyQuote.c_str();
		}

		const pp::Media& media = m.media;
		r.mediaKind = (int32)media.kind;
		if (media.kind != pp::Media::kNone) {
			r.mediaWidth = media.width;
			r.mediaHeight = media.height;
			r.mini = media.minithumbnail;
			r.spoiler = media.spoiler;
			r.secret = media.secret;
			r.emoji = media.emoji.c_str();
			r.duration = media.duration;
			r.waveform = media.waveform;
			r.image = fFiles.Path(media.fileId).c_str();
			// Fetch what's shown, newest first; a self-destructing photo
			// only when it's opened.
			if (!media.secret && r.image.IsEmpty())
				fFiles.Want(media.fileId, 1 + (int)std::min<size_t>(30, 30 * i / messages.size()));
		}
		add(r);
	}
	fMain->PostMessage(&list);
	_StartDownloads();

	// Fetch the replied-to messages that aren't loaded: once each, in one
	// request.
	std::vector<int64_t> missing = fHistory->TakeMissingReplies();
	if (!missing.empty())
		_Send(pp::request::GetMessages(chatId, missing, "reply:" + std::to_string(chatId)));
}


void
App::_StartDownloads()
{
	for (const json& request : fFiles.TakeDownloads())
		_Send(request);
}


void
App::_OpenMedia(int64 chatId, int64 messageId)
{
	const pp::Message* m = nullptr;
	if (fHistory != nullptr && fHistory->ChatId() == chatId)
		m = fHistory->Find(messageId);
	// The info window's shared photos.
	if (m == nullptr && fInfo.chatId == chatId) {
		for (const pp::Message& photo : fInfo.photos) {
			if (photo.id == messageId)
				m = &photo;
		}
	}
	if (m == nullptr || m->media.kind == pp::Media::kNone
		|| m->media.kind == pp::Media::kSticker)
		return;
	// Opening a self-destructing photo starts its timer, as in the official
	// apps; TDLib deletes it when the timer runs out.
	if (m->media.secret)
		_Send(pp::request::OpenContent(chatId, messageId));
	int32 file = m->media.fullFileId != 0 ? m->media.fullFileId : m->media.fileId;
	if (file == 0)
		return;
	if (m->media.kind == pp::Media::kVoice) {
		// A click plays it, or stops it when it's the one playing; listening
		// marks it as listened to for the sender.
		bool playing = fVoiceMessage == messageId;
		_StopVoice();
		fPlayWhenDownloaded.clear();
		if (playing)
			return;
		_Send(pp::request::OpenContent(chatId, messageId));
		if (!fFiles.Path(file).empty())
			_PlayVoice(fFiles.Path(file), messageId);
		else {
			fPlayWhenDownloaded[file] = messageId;
			fFiles.Want(file, 32);
			_StartDownloads();
		}
		return;
	}
	if (!fFiles.Path(file).empty()) {
		_Launch(fFiles.Path(file));
		return;
	}
	fOpenWhenDownloaded.insert(file);
	fFiles.Want(file, 32);
	_StartDownloads();
}


void
App::_SendFiles(const BMessage* message)
{
	int64 chatId = message->GetInt64("chat_id", 0);
	if (fHistory == nullptr || fHistory->ChatId() != chatId)
		return;
	std::string caption = message->GetString("caption", "");
	int64 replyTo = message->GetInt64("reply_to", 0);
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		BEntry entry(&ref, true);
		BPath path;
		if (!entry.IsFile() || entry.GetPath(&path) != B_OK)
			continue;  // folders aren't sent
		// Images Haiku can read go as photos (with their size), the rest as
		// documents. The caption and the reply go with the first one only.
		BBitmap* image = BTranslationUtils::GetBitmap(&ref);
		if (image != nullptr) {
			BRect bounds = image->Bounds();
			delete image;
			_Send(pp::request::SendPhoto(chatId, path.Path(),
				bounds.IntegerWidth() + 1, bounds.IntegerHeight() + 1, caption, replyTo));
		} else
			_Send(pp::request::SendDocument(chatId, path.Path(), caption, replyTo));
		caption.clear();
		replyTo = 0;
	}
	fTyping.Reset();
}


std::string
App::_TypingText(int64 chatId)
{
	std::vector<pp::Presence::Action> actions = fPresence.Actions(chatId, time(nullptr));
	if (actions.empty())
		return std::string();
	std::map<int64_t, std::string> names;
	for (const auto& a : actions)
		names[a.senderId] = a.senderId < 0 ? fNames.Chat(-a.senderId) : fNames.User(a.senderId);
	return pp::ActionText(actions, names, fPresence.PrivateUser(chatId) != 0);
}


void
App::_SendChatStatus()
{
	if (fHistory == nullptr)
		return;
	int64 chatId = fHistory->ChatId();
	bool active;
	BString status = _StatusText(chatId, true, &active);
	BMessage message(kMsgChatStatus);
	message.AddInt64("chat_id", chatId);
	message.AddString("status", status);
	message.AddBool("active", active);
	fMain->PostMessage(&message);
	if (fInfoWindow != nullptr && fInfo.chatId == chatId)
		_SendChatInfo();
}


// The header's second line: who's typing, else "online", "last seen …"
// or the number of members.
BString
App::_StatusText(int64 chatId, bool withTyping, bool* activeOut)
{
	BString status;
	std::string typing = withTyping ? _TypingText(chatId) : std::string();
	bool active = !typing.empty();
	if (active)
		status = typing.c_str();
	else if (int64 user = fPresence.PrivateUser(chatId)) {
		pp::UserStatus s = fPresence.Status(user);
		switch (s.kind) {
			case pp::UserStatus::kOnline:
				status = "online";
				active = true;
				break;
			case pp::UserStatus::kOffline:
			{
				// "last seen today at 22:41", "… yesterday at …", or a date.
				time_t when = (time_t)s.time, now = time(nullptr);
				struct tm w, n;
				localtime_r(&when, &w);
				localtime_r(&now, &n);
				BString clock, date;
				BTimeFormat().Format(clock, when, B_SHORT_TIME_FORMAT);
				if (w.tm_year == n.tm_year && w.tm_yday == n.tm_yday)
					status << "last seen today at " << clock;
				else if (now - when < 2 * 86400 && (n.tm_yday - w.tm_yday == 1
						|| (n.tm_yday == 0 && w.tm_mon == 11 && w.tm_mday == 31)))
					status << "last seen yesterday at " << clock;
				else {
					BDateFormat().Format(date, when, B_MEDIUM_DATE_FORMAT);
					status << "last seen " << date;
				}
				break;
			}
			case pp::UserStatus::kRecently: status = "last seen recently"; break;
			case pp::UserStatus::kLastWeek: status = "last seen within a week"; break;
			case pp::UserStatus::kLastMonth: status = "last seen within a month"; break;
			default: break;
		}
	} else if (int32 count = fPresence.MemberCount(chatId)) {
		const char* what = fPresence.IsChannel(chatId)
			? (count == 1 ? "subscriber" : "subscribers") : (count == 1 ? "member" : "members");
		status << count << " " << what;
	}
	if (activeOut != nullptr)
		*activeOut = active;
	return status;
}


void
App::_ShowChatInfo(int64 chatId)
{
	if (chatId == 0)
		return;
	if (fInfoWindow != nullptr && fInfo.chatId == chatId) {
		if (fInfoMessenger.LockTarget()) {
			fInfoWindow->Activate();
			fInfoWindow->Unlock();
		}
		return;
	}
	// One info window: another chat's replaces it.
	if (fInfoWindow != nullptr && fInfoMessenger.LockTarget())
		fInfoWindow->Quit();
	fInfo = pp::ChatInfo();
	fInfo.chatId = chatId;
	fInfoWindow = new ChatInfoWindow(chatId);
	fInfoMessenger = BMessenger(fInfoWindow);
	if (fMain->Lock()) {
		BRect frame = fMain->Frame();
		fMain->Unlock();
		fInfoWindow->MoveTo(frame.right - fInfoWindow->Frame().Width() - 40, frame.top + 40);
	}
	_SendChatInfo();
	fInfoWindow->Show();
	for (const json& r : pp::request::ChatInfo(chatId, fPresence.PrivateUser(chatId),
			fPresence.BasicGroup(chatId), fPresence.Supergroup(chatId),
			fPresence.IsChannel(chatId)))
		_Send(r);
}


void
App::_SendChatInfo()
{
	if (fInfoWindow == nullptr)
		return;
	int64 chatId = fInfo.chatId;
	BMessage info(kMsgChatInfo);
	info.AddInt64("chat_id", chatId);
	const pp::Chat* chat = fChats.Find(chatId);
	std::string title = chat != nullptr ? chat->title : fNames.Chat(chatId);
	info.AddString("title", title.c_str());
	info.AddString("initials", pp::Initials(title).c_str());
	info.AddString("status", _StatusText(chatId, false));
	info.AddString("about", fInfo.about.c_str());
	info.AddBool("private", fPresence.PrivateUser(chatId) != 0);
	pp::Photo photo = fNames.ChatPhoto(chatId);
	info.AddString("avatar", fFiles.Path(photo.fileId).c_str());
	AddBytes(info, "avatar_mini", photo.preview);
	if (photo.fileId != 0 && fFiles.Path(photo.fileId).empty())
		fFiles.Want(photo.fileId, 20);
	int32 count = fInfo.memberCount != 0 ? fInfo.memberCount : fPresence.MemberCount(chatId);
	info.AddInt32("member_count", count);
	for (int64_t user : fInfo.members) {
		std::string name = fNames.User(user);
		info.AddString("member", name.empty() ? "Deleted account" : name.c_str());
	}
	int priority = 19;
	for (const pp::Message& m : fInfo.photos) {
		info.AddInt64("photo_id", m.id);
		std::string path = fFiles.Path(m.media.fileId);
		info.AddString("photo_path", path.c_str());
		AddBytes(info, "photo_mini", m.media.minithumbnail);
		if (path.empty() && m.media.fileId != 0)
			fFiles.Want(m.media.fileId, std::max(2, priority--));
	}
	fInfoMessenger.SendMessage(&info);
	_StartDownloads();
}


void
App::_SendFindResult()
{
	BMessage result(kMsgFindResult);
	result.AddInt32("index", fFoundIndex);
	result.AddInt32("count", (int32)fFound.size());
	fMain->PostMessage(&result);
}


void
App::_ShowMessage(int64 messageId)
{
	// A search result: on screen if it's loaded, else the history around it
	// (a fresh one, so there's no gap; older and newer load by scrolling).
	if (fHistory == nullptr)
		return;
	fHighlight = messageId;
	fScrollTo = messageId;
	if (fHistory->Find(messageId) != nullptr) {
		_FlushHistory();
		return;
	}
	int64 chatId = fHistory->ChatId();
	fHistory = std::make_unique<pp::ChatHistory>(chatId);
	if (const pp::Chat* chat = fChats.Find(chatId))
		fHistory->SetLastReadOutbox(chat->lastReadOutbox);
	fHistory->SetAtLatest(false);
	fFirstUnread = 0;
	fHistoryPages = 3;  // no follow-up pages
	fLoadingHistory = true;
	_Send(pp::request::HistoryAround(chatId, messageId, 20, 20));
}


void
App::_SendChatLists()
{
	BMessage lists(kMsgChatLists);
	lists.AddString("key", "main");
	lists.AddString("name", "All chats");
	for (const pp::ChatList::Folder& f : fChats.Folders()) {
		lists.AddString("key", ("folder:" + std::to_string(f.id)).c_str());
		lists.AddString("name", f.name.c_str());
	}
	lists.AddString("key", "archive");
	lists.AddString("name", "Archive");
	fMain->PostMessage(&lists);
}


void
App::_SendStickerSets()
{
	BMessage sets(kMsgStickerSets);
	for (const pp::StickerSetInfo& set : fStickerSets) {
		sets.AddInt64("id", set.id);
		sets.AddString("title", set.title.c_str());
	}
	fMain->PostMessage(&sets);
}


void
App::_SendStickers(bool update)
{
	auto it = fStickers.find(fPickerSet);
	if (it == fStickers.end())
		return;
	BMessage list(kMsgStickers);
	list.AddInt64("set_id", fPickerSet);
	list.AddBool("update", update);
	int priority = 12;
	for (const pp::StickerItem& sticker : it->second) {
		list.AddInt32("file_id", sticker.fileId);
		list.AddInt32("width", sticker.width);
		list.AddInt32("height", sticker.height);
		list.AddString("emoji", sticker.emoji.c_str());
		BString path = fFiles.Path(sticker.imageFileId).c_str();
		list.AddString("image", path);
		// The picker's images, first ones first, after the chat's media.
		if (path.IsEmpty() && sticker.imageFileId != 0)
			fFiles.Want(sticker.imageFileId, std::max(2, priority--));
	}
	fMain->PostMessage(&list);
	_StartDownloads();
}


void
App::_PlayVoice(const std::string& path, int64 messageId)
{
	_StopVoice();
	if (fVoice.Play(path.c_str()) != B_OK) {
		// No decoder for it here: the user's player may have one.
		_Launch(path);
		return;
	}
	fVoiceMessage = messageId;
	BMessage tick(kMsgVoiceTick);
	fVoiceRunner = new BMessageRunner(BMessenger(this), &tick, 100000);
}


void
App::_StopVoice()
{
	fVoice.Stop();
	delete fVoiceRunner;
	fVoiceRunner = nullptr;
	if (fVoiceMessage != 0) {
		fVoiceMessage = 0;
		BMessage progress(kMsgVoiceProgress);
		progress.AddInt64("message_id", 0);
		progress.AddFloat("progress", 0);
		fMain->PostMessage(&progress);
	}
}


void
App::_Launch(const std::string& path)
{
	// The user's viewer for the file's type opens it (ShowImage for photos,
	// MediaPlayer for videos by default).
	entry_ref ref;
	if (get_ref_for_path(path.c_str(), &ref) == B_OK)
		be_roster->Launch(&ref);
}


void
App::_ViewVisible()
{
	// Read receipts: only while the user can see the chat.
	if (fHistory == nullptr || !fActive)
		return;
	std::vector<int64_t> ids = fHistory->TakeUnviewed();
	if (!ids.empty())
		_Send(pp::request::ViewMessages(fHistory->ChatId(), ids));
}


void
App::_Notify(const json& message)
{
	int64 chatId = pp::Int(message, "chat_id");
	const pp::Chat* chat = fChats.Find(chatId);
	if (chat == nullptr)
		return;
	pp::Message m = pp::ParseMessage(message);

	if (!fPrefs.notifications)
		return;
	if (fPrefs.sound)
		system_beep("Powergram new message");
	BString content;
	if (fPrefs.previews && fChats.ShowsPreview(chatId)) {
		if (chat->kind == pp::Chat::kGroup) {
			std::string sender = fNames.Sender(m);
			if (!sender.empty())
				content << sender.c_str() << ": ";
		}
		content << m.text.c_str();
	} else
		content = "New message";

	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetGroup(APP_NAME);
	notification.SetTitle(chat->title.c_str());
	notification.SetContent(content);
	// One notification per chat, replaced by the next.
	BString id;
	id << "chat-" << chatId;
	notification.SetMessageID(id);
	notification.SetOnClickApp(APP_SIGNATURE);
	BString arg;
	arg << "--open-chat=" << chatId;
	notification.AddOnClickArg(arg);
	notification.Send();
}


void
App::_ShowError(const char* text)
{
	BAlert* alert = new BAlert(APP_NAME, text, "OK", nullptr, nullptr,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go(nullptr);
}


void
App::_SetOnline(bool online)
{
	if (fAuth == nullptr || fAuth->State().step != AuthStep::Ready)
		return;
	if (fOnline == (online ? 1 : 0))
		return;
	fOnline = online ? 1 : 0;
	_Send(pp::request::Online(online));
}


void
App::_HandleAuth()
{
	const pp::AuthState& s = fAuth->State();

	switch (s.step) {
		case AuthStep::Ready:
			if (fLogin->Lock()) {
				if (!fLogin->IsHidden())
					fLogin->Hide();
				fLogin->Unlock();
			}
			if (fMain->Lock()) {
				if (fMain->IsHidden())
					fMain->Show();
				fMain->Unlock();
			}
			_Send(pp::ChatList::LoadRequest(100));
			fLoadedLists = {"main"};
			_SendChatLists();
			if (fPresenceRunner == nullptr) {
				BMessage tick(kMsgPresenceTick);
				fPresenceRunner = new BMessageRunner(BMessenger(this), &tick, 3000000);
			}
			fOnline = -1;
			_SetOnline(fActive);
			return;

		case AuthStep::Closed:
			fClosed = true;
			if (fQuitting) {
				PostMessage(B_QUIT_REQUESTED);
				return;
			}
			// Logged out: start over with a fresh TDLib instance.
			fChats = pp::ChatList();
			fNames = pp::Names();
			fFiles.Clear();
			fOpenWhenDownloaded.clear();
			fPlayWhenDownloaded.clear();
			_StopVoice();
			fHistory.reset();
			fOnline = -1;
			_FlushChats();
			fAuth = std::make_unique<pp::AuthFlow>(_Params());
			fClosed = false;
			fClient->NewInstance();
			return;

		case AuthStep::Starting:
		case AuthStep::LoggingOut:
		case AuthStep::Closing:
			if (s.step != AuthStep::Starting && fMain->Lock()) {
				if (!fMain->IsHidden())
					fMain->Hide();
				fMain->Unlock();
			}
			break;

		default:
			break;
	}

	BMessage state(kMsgAuthState);
	state.AddInt32("step", (int32)s.step);
	state.AddString("code_info", s.codeInfo.c_str());
	state.AddInt32("code_length", s.codeLength);
	state.AddString("hint", s.passwordHint.c_str());
	state.AddString("td_type", s.tdType.c_str());
	fLogin->PostMessage(&state);
	if (s.step == AuthStep::NeedApiKey && !fBadApiKey.empty()) {
		BMessage error(kMsgAuthError);
		error.AddString("text", fBadApiKey.c_str());
		fLogin->PostMessage(&error);
		fBadApiKey.clear();
	}

	// Show the login window when there is something to enter.
	if (s.step != AuthStep::Starting && fLogin->Lock()) {
		if (fLogin->IsHidden())
			fLogin->Show();
		fLogin->Unlock();
	}
}


void
App::_ScheduleChatFlush()
{
	// TDLib sends chats one update at a time; redraw at most ten times a second.
	if (fFlushRunner != nullptr)
		return;
	BMessage flush(kMsgFlushChats);
	fFlushRunner = new BMessageRunner(BMessenger(this), &flush, 100000, 1);
}


void
App::_FlushChats()
{
	delete fFlushRunner;
	fFlushRunner = nullptr;

	BMessage list(kMsgChatList);
	for (const pp::Chat& c : fChats.Sorted(fShownList)) {
		list.AddInt64("id", c.id);
		list.AddString("title", c.title.c_str());
		list.AddString("text", c.lastText.c_str());
		list.AddString("sender", c.lastSender.c_str());
		list.AddInt64("date", c.lastDate);
		list.AddInt32("unread", c.unread);
		list.AddInt32("mentions", c.unreadMentions);
		list.AddBool("pinned", c.pinned);
		list.AddBool("read", c.outgoingRead);
		list.AddString("typing", _TypingText(c.id).c_str());
		list.AddString("draft", c.draft.c_str());
		list.AddBool("muted", fChats.IsMuted(c.id));
		list.AddBool("marked_unread", c.markedUnread);
		list.AddInt32("kind", (int32)c.kind);
		BString photo = fFiles.Path(c.photoFileId).c_str();
		list.AddString("photo", photo);
		AddBytes(list, "photo_mini", c.photoPreview);
		// Avatars come after the open chat's media (priority 1 of 32).
		if (photo.IsEmpty() && c.photoFileId != 0)
			fFiles.Want(c.photoFileId, 1);
	}
	fMain->PostMessage(&list);
	_StartDownloads();
}


void
App::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgTdObject:
		{
			json object = json::parse(message->GetString("json", "{}"), nullptr, false);
			if (!object.is_discarded())
				_HandleObject(object);
			break;
		}

		case kMsgFlushChats:
			_FlushChats();
			break;

		case kMsgReact:
			_Send(pp::request::React(message->GetInt64("chat_id", 0),
				message->GetInt64("message_id", 0), message->GetString("emoji", ""),
				message->GetBool("add", true)));
			break;

		case kMsgForwardMessage:
			_Send(pp::request::Forward(message->GetInt64("to_chat_id", 0),
				message->GetInt64("from_chat_id", 0), {message->GetInt64("message_id", 0)}));
			break;

		case kMsgShowChatInfo:
			_ShowChatInfo(message->GetInt64("chat_id", 0));
			break;

		case kMsgChatInfoClosed:
			if (message->GetInt64("chat_id", 0) == fInfo.chatId) {
				fInfoWindow = nullptr;
				fInfo = pp::ChatInfo();
			}
			break;

		case kMsgShowSettings:
			if (fSettingsWindow == nullptr) {
				fSettingsWindow = new SettingsWindow(BMessenger(this), fPrefs);
				fSettingsWindow->Show();
			} else if (fSettingsWindow->Lock()) {
				fSettingsWindow->Activate();
				fSettingsWindow->Unlock();
			}
			break;

		case kMsgSettingsChanged:
			fPrefs = AppSettings::FromMessage(message);
			fSettings.preferences.MakeEmpty();
			fPrefs.ToMessage(&fSettings.preferences);
			fSettings.Save();
			fMain->PostMessage(message);  // the text size
			break;

		case kMsgSettingsClosed:
			fSettingsWindow = nullptr;
			break;

		case kMsgShowList:
		{
			fShownList = message->GetString("key", "main");
			// Each list is loaded the first time it's shown.
			if (fLoadedLists.insert(fShownList).second)
				_Send(pp::ChatList::LoadRequest(100, fShownList));
			_FlushChats();
			break;
		}

		case kMsgChatAction:
		{
			int64 chatId = message->GetInt64("chat_id", 0);
			const pp::Chat* chat = fChats.Find(chatId);
			if (chat == nullptr)
				break;
			std::string action = message->GetString("action", "");
			std::vector<json> requests;
			if (action == "read")
				requests = pp::ChatList::MarkReadRequests(*chat);
			else if (action == "unread")
				requests.push_back(pp::ChatList::MarkUnreadRequest(chatId));
			else if (action == "mute" || action == "unmute")
				requests.push_back(pp::ChatList::MuteRequest(chatId, action == "mute"));
			else if (action == "pin" || action == "unpin")
				requests.push_back(pp::ChatList::PinRequest(chatId, fShownList, action == "pin"));
			else if (action == "archive")
				requests.push_back(pp::ChatList::MoveRequest(chatId, "archive"));
			else if (action == "unarchive")
				requests.push_back(pp::ChatList::MoveRequest(chatId, "main"));
			else if (action == "delete") {
				if (fHistory != nullptr && fHistory->ChatId() == chatId)
					_OpenChat(0);
				requests = pp::ChatList::DeleteRequests(*chat);
			}
			for (const json& r : requests)
				_Send(r);
			break;
		}

		case kMsgSaveDraft:
		{
			// Only when it changed: each save is a request to Telegram.
			int64 chatId = message->GetInt64("chat_id", 0);
			std::string text = message->GetString("text", "");
			const pp::Chat* chat = fChats.Find(chatId);
			if (chat != nullptr && chat->draft != text)
				_Send(pp::ChatList::DraftRequest(chatId, text));
			break;
		}

		case kMsgFind:
			if (fHistory != nullptr) {
				std::string query = message->GetString("query", "");
				fFound.clear();
				fFoundIndex = -1;
				fHighlight = 0;
				if (query.empty()) {
					_SendFindResult();
					_FlushHistory();
				} else
					_Send(pp::request::SearchChat(fHistory->ChatId(), query, 100));
			}
			break;

		case kMsgFindStep:
			if (!fFound.empty()) {
				int32 count = (int32)fFound.size();
				fFoundIndex = (fFoundIndex + message->GetInt32("delta", 1) + count) % count;
				_SendFindResult();
				_ShowMessage(fFound[fFoundIndex]);
			}
			break;

		case kMsgLoadNewer:
			if (fHistory != nullptr && !fHistory->AtLatest() && !fLoadingHistory) {
				fLoadingHistory = true;
				fHistoryPages = 3;  // no follow-up pages
				_Send(pp::request::HistoryNewer(fHistory->ChatId(), fHistory->NewestId(), 40));
			}
			break;

		case kMsgJumpToLatest:
			if (fHistory == nullptr)
				break;
			if (fHistory->AtLatest())
				_FlushHistory(true);
			else
				_OpenChat(fHistory->ChatId(), true);
			break;

		case kMsgVoiceTick:
			if (!fVoice.IsPlaying()) {
				_StopVoice();
				break;
			}
			{
				BMessage progress(kMsgVoiceProgress);
				progress.AddInt64("message_id", fVoiceMessage);
				progress.AddFloat("progress", fVoice.Progress());
				fMain->PostMessage(&progress);
			}
			break;

		case kMsgPresenceTick:
			// Typing expires and "last seen" ages without an update.
			_SendChatStatus();
			_ScheduleChatFlush();
			break;

		case kMsgFlushHistory:
			_SendHistory();
			break;

		case kMsgSubmitApiKey:
			fSettings.apiId = message->GetInt32("api_id", 0);
			fSettings.apiHash = message->GetString("api_hash", "");
			fSettings.Save();
			_Send(fAuth->SetApiKey(fSettings.apiId, fSettings.apiHash.String()), true);
			break;
		case kMsgSubmitPhone:
			_Send(fAuth->PhoneRequest(message->GetString("text", "")), true);
			break;
		case kMsgSubmitCode:
			_Send(fAuth->CodeRequest(message->GetString("text", "")), true);
			break;
		case kMsgSubmitPassword:
			_Send(fAuth->PasswordRequest(message->GetString("text", "")), true);
			break;
		case kMsgSubmitName:
			_Send(fAuth->RegisterRequest(message->GetString("first", ""),
				message->GetString("last", "")), true);
			break;

		case kMsgLogOut:
		{
			BAlert* alert = new BAlert(APP_NAME, "Log out of Telegram on "
				"this computer? Chats stay on Telegram's servers.",
				"Cancel", "Log out", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			alert->Go(new BInvoker(new BMessage(kMsgLogOutConfirmed), this));
			break;
		}
		case kMsgLogOutConfirmed:
			if (message->GetInt32("which", 0) == 1)
				_Send(pp::AuthFlow::LogOutRequest(), true);
			break;

		case kMsgOpenChat:
			_OpenChat(message->GetInt64("id", 0));
			break;

		case kMsgLoadOlder:
			if (fHistory != nullptr && !fLoadingHistory && fHistory->Size() > 0) {
				fLoadingHistory = true;
				fHistoryPages = 3;  // one page per scroll, no follow-ups
				_Send(pp::request::History(fHistory->ChatId(),
					fHistory->OldestId(), 40));
			}
			break;

		case kMsgMessageProperties:
		{
			int64 chatId = message->GetInt64("chat_id", 0);
			int64 messageId = message->GetInt64("message_id", 0);
			_Send(pp::request::Properties(chatId, messageId,
				"props:" + std::to_string(chatId) + ":" + std::to_string(messageId)), true);
			break;
		}

		case kMsgEmojiUsed:
		{
			std::string emoji = message->GetString("emoji", "");
			auto& recent = fSettings.recentEmoji;
			recent.erase(std::remove(recent.begin(), recent.end(), emoji), recent.end());
			recent.insert(recent.begin(), emoji);
			if (recent.size() > 27)
				recent.resize(27);
			fSettings.Save();
			break;
		}

		case kMsgStickerSetsWanted:
			if (!fStickerSets.empty())
				_SendStickerSets();
			_Send(pp::request::InstalledStickerSets());  // and refresh
			break;

		case kMsgStickerSetWanted:
		{
			fPickerSet = message->GetInt64("set_id", 0);
			if (fStickers.count(fPickerSet))
				_SendStickers(false);
			// Each set once a session; the recently used ones every time.
			if (fPickerSet == 0)
				_Send(pp::request::RecentStickers());
			else if (!fStickers.count(fPickerSet))
				_Send(pp::request::StickerSet(fPickerSet));
			break;
		}

		case kMsgSendSticker:
		{
			int64 chatId = message->GetInt64("chat_id", 0);
			if (fHistory == nullptr || fHistory->ChatId() != chatId)
				break;
			pp::StickerItem sticker;
			sticker.fileId = message->GetInt32("file_id", 0);
			sticker.width = message->GetInt32("width", 0);
			sticker.height = message->GetInt32("height", 0);
			sticker.emoji = message->GetString("emoji", "");
			_Send(pp::request::SendSticker(chatId, sticker, message->GetInt64("reply_to", 0)));
			fStickers.erase(0);  // the recently used list changes
			break;
		}

		case kMsgSendFiles:
			_SendFiles(message);
			break;

		case kMsgOpenMedia:
			_OpenMedia(message->GetInt64("chat_id", 0), message->GetInt64("message_id", 0));
			break;

		case kMsgEditText:
			_Send(pp::request::EditText(message->GetInt64("chat_id", 0),
				message->GetInt64("message_id", 0), message->GetString("text", "")));
			break;

		case kMsgDeleteMessage:
			_Send(pp::request::Delete(message->GetInt64("chat_id", 0),
				{message->GetInt64("message_id", 0)},
				message->GetBool("for_everyone", false)));
			break;

		case kMsgSendText:
		{
			int64 chatId = message->GetInt64("chat_id", 0);
			if (fHistory == nullptr || fHistory->ChatId() != chatId)
				break;
			_Send(pp::request::SendText(chatId, message->GetString("text", ""),
				message->GetInt64("reply_to", 0)));
			fTyping.Reset();  // sending ends "typing" on Telegram's side
			break;
		}

		case kMsgEntryChanged:
		{
			int64 chatId = message->GetInt64("chat_id", 0);
			if (fHistory == nullptr || fHistory->ChatId() != chatId)
				break;
			if (message->GetBool("empty", true)) {
				_Send(pp::request::Typing(chatId, false));
				fTyping.Reset();
			} else if (fTyping.ShouldSend(system_time() / 1e6))
				_Send(pp::request::Typing(chatId, true));
			break;
		}

		case kMsgWindowActive:
		{
			fActive = message->GetBool("active", false);
			if (fActive)
				_ViewVisible();
			// Switching between our own windows shouldn't flicker the
			// status: decide a second later.
			delete fOnlineRunner;
			BMessage timer(kMsgOnlineTimer);
			fOnlineRunner = new BMessageRunner(BMessenger(this), &timer,
				fActive ? 100000 : 1000000, 1);
			break;
		}

		case kMsgOnlineTimer:
			delete fOnlineRunner;
			fOnlineRunner = nullptr;
			_SetOnline(fActive);
			break;

		case kMsgForceQuit:
			fClosed = true;
			PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			BApplication::MessageReceived(message);
	}
}


void
App::_CloseWindows()
{
	if (fMain != nullptr && fMain->Lock()) {
		fSettings.mainFrame = fMain->Frame();
		fMain->Quit();
		fMain = nullptr;
	}
	if (fLogin != nullptr && fLogin->Lock()) {
		fLogin->Quit();
		fLogin = nullptr;
	}
	if (fInfoWindow != nullptr && fInfoMessenger.LockTarget())
		fInfoWindow->Quit();
	fInfoWindow = nullptr;
	if (fSettingsWindow != nullptr && BMessenger(fSettingsWindow).LockTarget())
		fSettingsWindow->Quit();
	fSettingsWindow = nullptr;
	fSettings.Save();
}


bool
App::QuitRequested()
{
	if (fClosed || fClient == nullptr) {
		_CloseWindows();
		return true;
	}
	if (!fQuitting) {
		// Let TDLib close its database first; give up after five seconds.
		fQuitting = true;
		_SetOnline(false);
		_Send({{"@type", "close"}}, true);
		BMessage force(kMsgForceQuit);
		BMessageRunner::StartSending(BMessenger(this), &force, 5000000, 1);
	}
	return false;
}


void
App::AboutRequested()
{
	BAboutWindow* about = new BAboutWindow(APP_NAME, APP_SIGNATURE);
	about->AddDescription("A Telegram client for Haiku, after Paper Plane by "
		"the Paper Plane developers. Preview.");
	about->AddCopyright(2026, "the Powergram contributors");
	about->AddText("Licence: GNU GPL 3.0 or later. Uses TDLib.");
	about->Show();
}


int
main()
{
	App app;
	app.Run();
	return 0;
}
