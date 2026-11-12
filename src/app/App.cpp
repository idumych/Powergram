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
#include <PathFinder.h>
#include <Deskbar.h>
#include <FindDirectory.h>
#include <Directory.h>
#include <Path.h>
#include <TranslationUtils.h>
#include <Roster.h>
#include <Url.h>
#include <Screen.h>
#include <TimeFormat.h>
#include <DateFormat.h>
#include <Notification.h>

#include <algorithm>
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
#include "../core/SecretChat.h"
#include "../core/ChatHistory.h"
#include "../core/ChatInfo.h"
#include "../core/ChatList.h"
#include "../core/Compose.h"
#include "../core/Contacts.h"
#include "../core/Files.h"
#include "../core/Links.h"
#include "../core/Pinned.h"
#include "../core/VoiceNote.h"
#include "../core/Mentions.h"
#include "../core/Profile.h"
#include "../core/Presence.h"
#include "../core/Stickers.h"
#include "ChatInfoWindow.h"
#include "ComposeWindows.h"
#include "SendFilesWindow.h"
#include "ProfileWindow.h"
#include "FileSaving.h"
#include "MediaProbe.h"
#include "LoginWindow.h"
#include "MediaWindow.h"
#include "MainWindow.h"
#include "NewChatWindow.h"
#include "Messages.h"
#include "Settings.h"
#include "SettingsWindow.h"
#include "TdClient.h"
#include "VoicePlayer.h"
#include "VoiceRecorder.h"

using pp::AuthStep;
using pp::json;

// A photo, video or file asked to open, while it downloads.
struct OpenRequest {
	int32_t file = 0;
	pp::Media::Kind kind = pp::Media::kNone;
	bool inViewer = false;   // Powergram's viewer, else the system's app
	std::string title;       // "Photo from …", for the viewer
};


class App : public BApplication {
public:
	App();
	~App() override;

	void ArgvReceived(int32 argc, char** argv) override;
	void ReadyToRun() override;
	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;
	void RefsReceived(BMessage* message) override;
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
	const pp::Message* _FindMessage(int64 chatId, int64 messageId) const;
	void _OpenMedia(int64 chatId, int64 messageId, int32 where = kOpenAsSet);
	void _ShowMedia(const OpenRequest& request, const std::string& path);
	void _SaveFile(const BMessage* message);
	void _Launch(const std::string& path, const std::string& mimeType = "");
	void _SendFiles(const BMessage* message);
	void _SendStickerSets();
	void _SendChatStatus();
	std::string _TypingText(int64 chatId);
	void _SendStickers(bool update);
	void _ViewVisible();
	void _SetOnline(bool online);
	void _Notify(const json& message);
	void _ShowError(const char* text);

	// Telegram links and usernames, opened in the app.
	void _OpenLink(const std::string& url);
	bool _HandleLinkReply(const json& object, const std::string& extra);
	// Selects a chat, adding it at the top of the list for now if it isn't
	// in the one shown (a public chat found by its username); `messageId`
	// to scroll to, once it's open.
	void _ShowChat(int64 chatId, int64 messageId = 0);
	int64 fExtraChat = 0;
	int64 fMyId = 0;
	BMessage fPendingProps;   // a message menu waiting for its reactions
	// The Deskbar item (an add-on of its own) and the count it shows.
	void _UpdateDeskbar();
	void _SendUnread();
	BMessenger fDeskbarItem;
	int32 fUnreadSent = -1;

	// The Profile window, and what it shows.
	void _SendProfile();
	bool _HandleProfileReply(const json& object, const std::string& extra);
	BMessenger fProfileWindow;
	pp::Profile fProfile;
	int fProfileSaving = 0;       // setName/setBio answers still to come
	std::string fProfileError;   // the user's own id (TDLib's "my_id" option)
	// The open chat's pinned messages, for the bar over it.
	pp::PinnedMessages fPinned;
	void _SendPinned();
	int64 fPendingChat = 0;
	int64 fPendingMessage = 0;

	// Starting chats: the New chat window and the contacts it lists, and
	// the group and add contact windows waiting for an answer.
	void _ShowNewChat(const char* what);
	void _SendContacts();
	bool _HandleContactsReply(const json& object, const std::string& extra);
	std::vector<PersonRow> _ContactRows() const;
	pp::Contacts fContacts;
	BMessenger fNewChatWindow;
	std::map<std::string, BMessenger> fCreateReplies;   // "create:<n>" → window
	std::map<std::string, std::vector<int64_t>> fCreateMembers;   // a new channel's

	// The Scheduled messages window, for one chat at a time.
	void _SendScheduled();
	BMessenger fScheduledWindow;
	pp::ScheduledMessages fScheduled;
	int32 fPollAnswersMax = 12;   // TDLib's "poll_answer_count_max"
	int32 fCreateSeq = 0;

	Settings fSettings;
	bool fTestDc;
	std::unique_ptr<TdClient> fClient;
	std::unique_ptr<pp::AuthFlow> fAuth;
	pp::ChatList fChats;
	std::string fShownList = "main";   // the chat list on screen
	std::set<std::string> fLoadedLists;
	void _SendChatLists();
	pp::Names fNames;
	pp::SecretChats fSecret;
	// A secret chat's state, notice and timer for the open chat's window.
	const pp::SecretChat* _SecretChat(int64 chatId) const;
	pp::FileStore fFiles;
	pp::Presence fPresence;
	AppSettings fPrefs;
	SettingsWindow* fSettingsWindow = nullptr;
	BMessageRunner* fPresenceRunner = nullptr;
	std::map<int32_t, OpenRequest> fOpenWhenDownloaded;  // clicked files on their way
	BMessenger fMediaWindow;   // the viewer, while it's open
	std::multimap<int32_t, std::string> fSaveWhenDownloaded;  // file → where
	std::map<int32_t, std::string> fFileTypes;  // documents' MIME types
	std::set<int32_t> fProgressFiles;  // documents downloading in the open chat
	bigtime_t fLastProgress = 0;

	// Voice messages play here rather than in MediaPlayer.
	void _PlayVoice(const std::string& path, int64 messageId);
	void _StopVoice();
	VoicePlayer fVoice;
	int64 fVoiceMessage = 0;   // playing, or 0
	BMessageRunner* fVoiceRunner = nullptr;
	std::map<int32_t, int64> fPlayWhenDownloaded;  // file → message

	// Recording a voice message, for one chat at a time.
	void _Record(const BMessage* message);
	void _StopRecording(bool send);
	void _SendRecordState();
	VoiceRecorder fRecorder;
	BMessageRunner* fRecordRunner = nullptr;
	int64 fRecordChat = 0;
	int64 fRecordReply = 0;
	BString fRecordPath;
	bigtime_t fRecordAction = 0;   // last "recording a voice message" sent

	// The info window, for one chat at a time.
	void _ShowChatInfo(int64 chatId);
	void _SendChatInfo();
	BString _StatusText(int64 chatId, bool withTyping, bool* active = nullptr);
	BString _UserStatusText(int64 user, bool* active = nullptr);
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
	delete fRecordRunner;
	// The item stays in Deskbar as a launcher (a click starts the app), so
	// it's left there, without a count that would go stale.
	if (fDeskbarItem.IsValid()) {
		BMessage count(kMsgDeskbarUnread);
		count.AddInt32("unread", 0);
		fDeskbarItem.SendMessage(&count);
	}
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
	// Pictures pasted on earlier days, sent or not, aren't needed any more.
	BPath pasted = Settings::Directory();
	pasted.Append("pasted");
	CleanPastedFiles(pasted.Path());
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
	_UpdateDeskbar();

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
			auto open = fOpenWhenDownloaded.find(id);
			if (open != fOpenWhenDownloaded.end()) {
				_ShowMedia(open->second, fFiles.Path(id));
				fOpenWhenDownloaded.erase(open);
			}
			auto saves = fSaveWhenDownloaded.equal_range(id);
			for (auto it = saves.first; it != saves.second; ++it)
				SaveCopy(fFiles.Path(id), it->second, fFileTypes[id]);
			fSaveWhenDownloaded.erase(id);
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
		if (shown && fProfileWindow.IsValid())
			_SendProfile();
		_StartDownloads();
		if (type == "updateFile")
			return;
	}
	if (type == "updateFile") {
		// A document's card shows how far it's got, twice a second.
		int32 id = (int32)pp::Int(pp::Field(object, "file"), "id");
		if (fProgressFiles.count(id) && system_time() - fLastProgress > 500000) {
			fLastProgress = system_time();
			_FlushHistory();
		}
		return;
	}

	std::vector<json> requests;
	if (fAuth->Handle(object, requests)) {
		for (const json& r : requests)
			_Send(r, true);
		_HandleAuth();
		return;
	}

	fNames.Apply(object);
	if (fContacts.Apply(object) && fNewChatWindow.IsValid())
		_SendContacts();
	if (fSecret.Apply(object) && fHistory != nullptr) {
		_SendChatStatus();
		_ScheduleChatFlush();
	}
	if (type == "updateChatMessageAutoDeleteTime" && fHistory != nullptr
		&& pp::Int(object, "chat_id") == fHistory->ChatId()) {
		fChats.Apply(object);
		_SendChatStatus();
	}
	if (type == "updateOption" && pp::Str(object, "name") == "my_id")
		fMyId = pp::Int(pp::Field(object, "value"), "value");
	if (type == "updateOption" && pp::Str(object, "name") == "poll_answer_count_max")
		fPollAnswersMax = (int32)pp::Int(pp::Field(object, "value"), "value", 12);
	if (fScheduledWindow.IsValid() && fScheduled.Apply(object))
		_SendScheduled();
	if (fProfileWindow.IsValid() && fProfile.userId != 0
		&& (type == "updateUser" || type == "updateUserFullInfo") && fProfile.Apply(object))
		_SendProfile();
	if (fPinned.Affects(object))
		_Send(pp::request::Pinned(fPinned.ChatId()));
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
			bool asked = fOpenWhenDownloaded.erase(id) + fSaveWhenDownloaded.erase(id) > 0;
			if (asked) {
				BString text("Could not download the file. ");
				text << pp::AuthFlow::ErrorText(object).c_str();
				_ShowError(text);
			}
			if (fHistory != nullptr)
				_FlushHistory();
			_StartDownloads();
		}
		return;
	}
	if (extra.rfind("history:", 0) == 0) {
		_HandleHistoryReply(object);
		return;
	}
	if (extra.rfind("scheduled:", 0) == 0) {
		if (type == "messages" && atoll(extra.c_str() + 10) == fScheduled.ChatId()) {
			fScheduled.Set(object);
			_SendScheduled();
		}
		return;
	}
	if (extra == "send_poll" || extra == "send_location" || extra == "reschedule"
		|| extra == "send_scheduled") {
		if (type == "error") {
			BString text(extra == "send_poll" ? "Could not send the poll. "
				: extra == "send_location" ? "Could not send the location. "
				: extra == "reschedule" ? "Could not change the scheduled message. "
				: "Could not schedule the message. ");
			text << pp::AuthFlow::ErrorText(object).c_str();
			_ShowError(text);
		}
		return;
	}
	if (extra == "secret_new" || extra == "self_destruct") {
		if (type == "error") {
			BString text(extra == "secret_new" ? "Could not start a secret chat. "
				: "Could not set the self-destruct timer. ");
			text << pp::AuthFlow::ErrorText(object).c_str();
			_ShowError(text);
		} else if (type == "chat")
			_ShowChat(pp::Int(object, "id"));
		return;
	}
	if (_HandleLinkReply(object, extra))
		return;
	if (_HandleProfileReply(object, extra))
		return;
	if (_HandleContactsReply(object, extra))
		return;
	if (extra.rfind("pinned:", 0) == 0) {
		if (type == "foundChatMessages" && atoll(extra.c_str() + 7) == fPinned.ChatId()) {
			fPinned.SetFound(object);
			_SendPinned();
		}
		return;
	}
	if (extra.rfind("mention:", 0) == 0) {
		// Suggestions for an "@name" being typed: names and usernames.
		BMessage results(kMsgMentionResults);
		results.AddInt32("seq", atoi(extra.c_str() + 8));
		if (type == "chatMembers") {
			for (int64_t user : pp::MemberUserIds(object)) {
				if (user == fMyId)
					continue;
				results.AddInt64("user_id", user);
				results.AddString("name", fNames.User(user).c_str());
				results.AddString("username", fNames.Username(user).c_str());
			}
		}
		fMain->PostMessage(&results);
		return;
	}
	if (extra.rfind("mentions:", 0) == 0) {
		// The oldest unread mention: the search gives them newest first.
		if (type == "foundChatMessages" && fHistory != nullptr
			&& fHistory->ChatId() == atoll(extra.c_str() + 9)) {
			int64 oldest = 0;
			const json& messages = pp::Field(object, "messages");
			if (messages.is_array()) {
				for (const json& m : messages) {
					int64 id = pp::Int(m, "id");
					if (id != 0 && (oldest == 0 || id < oldest))
						oldest = id;
				}
			}
			if (oldest != 0)
				_ShowMessage(oldest);
		}
		return;
	}
	if (extra == "pin") {
		if (type == "error") {
			BString text("Could not change the pinned messages. ");
			text << pp::AuthFlow::ErrorText(object).c_str();
			_ShowError(text);
		}
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
		props.AddBool("can_pin", pp::Bool(object, "can_be_pinned"));
		const pp::Message* shown = fHistory != nullptr && fHistory->ChatId() == chatId
			? fHistory->Find(messageId) : nullptr;
		props.AddBool("pinned", shown != nullptr && shown->pinned);
		// The menu offers only the reactions this message allows (chats can
		// limit them, or turn them off): ask before showing it.
		fPendingProps = props;
		_Send(pp::request::AvailableReactions(chatId, messageId,
			"avail:" + std::to_string(chatId) + ":" + std::to_string(messageId)), true);
		return;
	}
	if (extra.rfind("avail:", 0) == 0) {
		std::string key = extra.substr(6);
		std::string pending = std::to_string(fPendingProps.GetInt64("chat_id", 0)) + ":"
			+ std::to_string(fPendingProps.GetInt64("message_id", 0));
		if (key != pending || fPendingProps.what != kMsgMessageProperties)
			return;   // another menu since
		if (type == "availableReactions") {
			for (const std::string& emoji : pp::ParseAvailableReactions(object))
				fPendingProps.AddString("reaction", emoji.c_str());
		}
		fMain->PostMessage(&fPendingProps);
		fPendingProps = BMessage();
		return;
	}
	if (extra == "react") {
		if (type == "error") {
			BString text("Could not react to the message. ");
			text << pp::AuthFlow::ErrorText(object).c_str();
			_ShowError(text);
		}
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
	if (fRecorder.IsRecording())
		_StopRecording(false);
	fHistory.reset();
	fPinned = pp::PinnedMessages();
	fTyping.Reset();
	fLoadingHistory = false;
	fFiles.ForgetWanted();  // the old chat's photos that haven't started
	fPlayWhenDownloaded.clear();
	if (id == 0)
		return;

	fHistory = std::make_unique<pp::ChatHistory>(id);
	fPinned = pp::PinnedMessages(id);
	_SendPinned();   // none until they come
	_Send(pp::request::Pinned(id));
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


// A message in a few words: its text, or what it is (replies, the pinned bar).
static BString
Summary(const pp::Message& m)
{
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
	if (m.media.kind == pp::Media::kIcon)
		return BString("Icon");
	if (m.media.kind == pp::Media::kDocument)
		return BString((m.media.title.empty() ? m.media.fileName : m.media.title).c_str());
	return BString("Message");
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
		BString sender, text, time, forward, replySender, replyText, image, emoji, animation;
		int32 animationFormat = 0;
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
		BString fileName, fileTitle, fileType;
		int64 fileSize = 0, fileDone = -1;
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
		list.AddString("animation", r.animation);
		list.AddInt32("animation_format", r.animationFormat);
		list.AddInt32("duration", r.duration);
		list.AddBool("show_avatar", r.showAvatar);
		list.AddInt64("sender_key", r.senderKey);
		list.AddString("avatar", r.avatar);
		AddBytes(list, "avatar_mini", r.avatarMini);
		list.AddString("initials", r.initials);
		list.AddString("spans", pp::EncodeSpans(r.spans).c_str());
		list.AddString("reactions", r.reactions);
		AddBytes(list, "waveform", r.waveform);
		list.AddString("file_name", r.fileName);
		list.AddString("file_title", r.fileTitle);
		list.AddString("file_type", r.fileType);
		list.AddInt64("file_size", r.fileSize);
		list.AddInt64("file_done", r.fileDone);
	};

	BTimeFormat timeFormat;
	BDateFormat dateFormat;
	int lastDay = -1;
	int64 lastSender = 0;  // name only on the first message of a run
	bool unreadMarked = false;
	std::vector<pp::Message> messages = fHistory->Sorted();
	fProgressFiles.clear();
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
				r.replyText = Summary(*target);
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
			// Animated stickers play once their file is here.
			if (media.animationFileId != 0) {
				r.animation = fFiles.Path(media.animationFileId).c_str();
				r.animationFormat = media.animationFormat;
				if (r.animation.IsEmpty())
					fFiles.Want(media.animationFileId,
						1 + (int)std::min<size_t>(30, 30 * i / messages.size()));
			}
			// The file that's opened or saved: its name and, for a
			// document's card, how much of it is here.
			int32 file = media.fullFileId != 0 ? media.fullFileId : media.fileId;
			std::string path = fFiles.Path(file);
			r.fileName = media.fileName.c_str();
			if (r.fileName.IsEmpty() && !path.empty())
				r.fileName = BPath(path.c_str()).Leaf();
			r.fileTitle = media.title.empty() ? media.fileName.c_str() : media.title.c_str();
			r.fileType = media.mimeType.c_str();
			r.fileSize = media.fileSize;
			if (!path.empty())
				r.fileDone = media.fileSize;
			else if (fFiles.Downloading(file)) {
				r.fileDone = fFiles.Downloaded(file);
				if (media.kind == pp::Media::kDocument)
					fProgressFiles.insert(file);
			}
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


const pp::Message*
App::_FindMessage(int64 chatId, int64 messageId) const
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
	return m;
}


void
App::_OpenMedia(int64 chatId, int64 messageId, int32 where)
{
	const pp::Message* m = _FindMessage(chatId, messageId);
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
	if (!m->media.mimeType.empty())
		fFileTypes[file] = m->media.mimeType;
	// Photos and videos in Powergram's viewer or the system's apps, as the
	// settings say or the message menu picked; other files in their apps.
	OpenRequest request;
	request.file = file;
	request.kind = m->media.kind;
	request.inViewer = pp::ViewsInApp(m->media.kind)
		&& (where == kOpenInViewer || (where == kOpenAsSet && fPrefs.mediaInApp));
	const char* what = m->media.kind == pp::Media::kPhoto ? "Photo"
		: m->media.kind == pp::Media::kAnimation ? "GIF"
		: m->media.kind == pp::Media::kVideoNote ? "Video message" : "Video";
	request.title = std::string(what) + " from " + fNames.Sender(*m);
	if (!fFiles.Path(file).empty()) {
		_ShowMedia(request, fFiles.Path(file));
		return;
	}
	fOpenWhenDownloaded[file] = request;
	fFiles.Want(file, 32);
	_StartDownloads();
	_FlushHistory();  // "Downloading…" on the card
}


void
App::_SaveFile(const BMessage* message)
{
	const pp::Message* m = _FindMessage(message->GetInt64("chat_id", 0),
		message->GetInt64("message_id", 0));
	entry_ref dir;
	const char* name = message->GetString("name", nullptr);
	if (m == nullptr || message->FindRef("directory", &dir) != B_OK || name == nullptr)
		return;
	int32 file = m->media.fullFileId != 0 ? m->media.fullFileId : m->media.fileId;
	if (file == 0)
		return;
	BPath to(&dir);
	to.Append(name);
	if (!m->media.mimeType.empty())
		fFileTypes[file] = m->media.mimeType;
	if (!fFiles.Path(file).empty()) {
		SaveCopy(fFiles.Path(file), to.Path(), m->media.mimeType);
		return;
	}
	fSaveWhenDownloaded.insert({file, to.Path()});
	fFiles.Want(file, 32);
	_StartDownloads();
	_FlushHistory();
}


void
App::_SendFiles(const BMessage* message)
{
	// The open chat, or one chosen for files from elsewhere (Deskbar,
	// Tracker), which comes as "to_chat_id".
	int64 chatId = message->GetInt64("to_chat_id", message->GetInt64("chat_id", 0));
	if (chatId == 0)
		return;
	bool open = fHistory != nullptr && fHistory->ChatId() == chatId;
	std::string caption = message->GetString("caption", "");
	int64 replyTo = open ? message->GetInt64("reply_to", 0) : 0;
	// Photos and playable videos together go as albums; the rest as
	// documents. The caption and the reply go with the first one only.
	std::vector<pp::OutgoingFile> files;
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		BEntry entry(&ref, true);
		BPath path;
		if (!entry.IsFile() || entry.GetPath(&path) != B_OK)
			continue;  // folders aren't sent
		files.push_back(ProbeFile(path.Path()));
	}
	for (const json& request : pp::request::SendFiles(chatId, files, caption, replyTo))
		_Send(request);
	if (open)
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


const pp::SecretChat*
App::_SecretChat(int64 chatId) const
{
	const pp::Chat* chat = fChats.Find(chatId);
	if (chat == nullptr || !chat->secret)
		return nullptr;
	return fSecret.Find(chat->secretChatId);
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
	message.AddBool("can_join", fPresence.CanJoin(chatId));
	// Secret chats: what to say while it can't be written in, and the
	// timer; private chats: whether one can be started from here.
	const pp::Chat* chat = fChats.Find(chatId);
	if (const pp::SecretChat* secret = _SecretChat(chatId)) {
		message.AddBool("secret", true);
		message.AddString("notice", pp::SecretChatNotice(*secret,
			fNames.User(secret->userId)).c_str());
		message.AddInt32("self_destruct", chat->autoDelete);
	} else if (chat != nullptr && chat->secret) {
		message.AddBool("secret", true);
		message.AddString("notice", "Starting the secret chat" B_UTF8_ELLIPSIS);
	}
	message.AddBool("can_start_secret", chat != nullptr && !chat->secret
		&& chat->kind == pp::Chat::kPrivate && chat->userId != 0 && chat->userId != fMyId);
	// "Send when online" needs the other person's exact status.
	pp::UserStatus::Kind seen = chat != nullptr && chat->userId != 0
		? fPresence.Status(chat->userId).kind : pp::UserStatus::kUnknown;
	message.AddBool("can_send_when_online", chat != nullptr && !chat->secret
		&& chat->userId != fMyId
		&& (seen == pp::UserStatus::kOnline || seen == pp::UserStatus::kOffline));
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
	else if (int64 user = fPresence.PrivateUser(chatId))
		status = _UserStatusText(user, &active);
	else if (int32 count = fPresence.MemberCount(chatId)) {
		const char* what = fPresence.IsChannel(chatId)
			? (count == 1 ? "subscriber" : "subscribers") : (count == 1 ? "member" : "members");
		status << count << " " << what;
	}
	if (activeOut != nullptr)
		*activeOut = active;
	return status;
}


// "online", "last seen today at 22:41", "last seen recently"…; "" if unknown.
BString
App::_UserStatusText(int64 user, bool* activeOut)
{
	BString status;
	bool active = false;
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
	// A secret chat's key, as a picture to compare with the other phone.
	if (const pp::SecretChat* secret = _SecretChat(chatId)) {
		std::vector<uint8_t> key = pp::KeyPicture(secret->keyHash);
		if (!key.empty()) {
			AddBytes(info, "key_picture", std::string(key.begin(), key.end()));
			info.AddString("key_name", fNames.User(secret->userId).c_str());
		}
	}
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
		// An animated sticker without an image: the sticker itself, whose
		// first frame the picker draws.
		BString animation;
		if (sticker.imageFileId == 0 && sticker.animationFormat != 0) {
			animation = fFiles.Path(sticker.fileId).c_str();
			if (animation.IsEmpty())
				fFiles.Want(sticker.fileId, std::max(2, priority--));
		}
		list.AddString("animation", animation);
		list.AddInt32("animation_format", sticker.animationFormat);
	}
	fMain->PostMessage(&list);
	_StartDownloads();
	_SendUnread();
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
App::_ShowMedia(const OpenRequest& request, const std::string& path)
{
	if (!request.inViewer) {
		_Launch(path, fFileTypes[request.file]);
		return;
	}
	// Typed, so the viewer can name the app that would open it instead.
	GiveFileType(path.c_str(), fFileTypes[request.file]);
	if (!fMediaWindow.IsValid()) {
		MediaWindow* window = new MediaWindow();
		fMediaWindow = BMessenger(window);
	}
	if (request.kind != pp::Media::kPhoto && request.kind != pp::Media::kAnimation)
		_StopVoice();   // one thing speaking at a time
	BMessage show(kMsgShowMedia);
	show.AddString("path", path.c_str());
	show.AddInt32("kind", request.kind);
	show.AddString("title", request.title.c_str());
	fMediaWindow.SendMessage(&show);
}


void
App::_Launch(const std::string& path, const std::string& mimeType)
{
	// The user's viewer for the file's type opens it (ShowImage for photos,
	// MediaPlayer for videos by default). TDLib's files have no type yet:
	// sniffed, or else the one the sender's app gave.
	GiveFileType(path.c_str(), mimeType);
	entry_ref ref;
	if (get_ref_for_path(path.c_str(), &ref) != B_OK)
		return;
	status_t status = be_roster->Launch(&ref);
	if (status != B_OK && status != B_ALREADY_RUNNING) {
		BString text("No application here opens \"");
		text << ref.name << "\". Use Save As" B_UTF8_ELLIPSIS " from the message's menu to keep it.";
		_ShowError(text);
	}
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
	// Secret chats never show what was said outside the app.
	if (fPrefs.previews && fChats.ShowsPreview(chatId) && !chat->secret) {
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
App::_OpenLink(const std::string& url)
{
	if (int64 user = pp::MentionedUser(url)) {
		_Send(pp::request::CreatePrivateChat(user, "openchat"));
		return;
	}
	if (!pp::IsTelegramLink(url)) {
		BUrl(url.c_str(), true).OpenWithPreferredApplication(false);
		return;
	}
	// TDLib knows what each kind of t.me link is.
	_Send(pp::request::LinkType(url, "link:" + url));
}


bool
App::_HandleLinkReply(const json& object, const std::string& extra)
{
	std::string type = pp::Type(object);
	if (extra.rfind("link:", 0) == 0) {
		std::string url = extra.substr(5);
		pp::LinkAction action = pp::ParseLinkType(object);
		switch (action.kind) {
			case pp::LinkAction::kPublicChat:
				_Send(pp::request::SearchPublicChat(action.value, "openchat"));
				break;
			case pp::LinkAction::kInvite:
				_Send(pp::request::CheckInvite(action.value, "invite:" + action.value));
				break;
			case pp::LinkAction::kMessage:
				_Send(pp::request::MessageLinkInfo(action.value, "msglink"));
				break;
			default:
				// Stickers, proxies, settings…: the web page explains.
				BUrl(url.c_str(), true).OpenWithPreferredApplication(false);
		}
		return true;
	}
	if (extra == "openchat" || extra == "join" || extra == "msglink"
		|| extra.rfind("invite:", 0) == 0) {
		if (type == "error") {
			BString text(extra == "join" ? "Could not join the chat. "
				: extra.rfind("invite:", 0) == 0 ? "Could not open the invite link. "
				: "Could not open the chat. ");
			text << pp::LinkErrorText(object).c_str();
			_ShowError(text);
			return true;
		}
		if (type == "chat")
			_ShowChat(pp::Int(object, "id"));
		else if (type == "messageLinkInfo") {
			int64 chat = pp::Int(object, "chat_id");
			int64 message = pp::Int(pp::Field(object, "message"), "id");
			if (chat != 0)
				_ShowChat(chat, message);
			else
				_ShowError("This message could not be found. It may have been deleted, "
					"or it is in a chat you are not in.");
		} else if (type == "chatInviteLinkInfo") {
			int64 chat = pp::InviteChat(object);
			if (chat != 0) {
				_ShowChat(chat);
				return true;
			}
			BAlert* alert = new BAlert(APP_NAME, pp::InviteQuestion(object).c_str(),
				"Join", "Cancel", nullptr, B_WIDTH_AS_USUAL, B_IDEA_ALERT);
			alert->SetShortcut(1, B_ESCAPE);
			BMessage* join = new BMessage(kMsgJoinInvite);
			join->AddString("link", extra.substr(7).c_str());
			alert->Go(new BInvoker(join, this));
		}
		// joinChat answers "ok": the supergroup update redraws the header.
		return true;
	}
	return false;
}


void
App::_Record(const BMessage* message)
{
	BString action = message->GetString("action", "");
	if (action == "start") {
		if (fRecorder.IsRecording())
			return;
		_StopVoice();
		// Recordings wait in the cache until TDLib has uploaded them; ones
		// older than a day go when the next starts.
		BPath dir;
		if (find_directory(B_USER_CACHE_DIRECTORY, &dir, true) != B_OK)
			dir.SetTo("/tmp");
		dir.Append("Powergram/voice");
		create_directory(dir.Path(), 0700);
		BDirectory folder(dir.Path());
		BEntry old;
		while (folder.GetNextEntry(&old) == B_OK) {
			time_t modified = 0;
			if (old.GetModificationTime(&modified) == B_OK
				&& modified < time(nullptr) - 24 * 3600)
				old.Remove();
		}
		fRecordPath = dir.Path();
		fRecordPath << "/voice-" << (int64)real_time_clock_usecs() << ".ogg";
		fRecordChat = message->GetInt64("chat_id", 0);
		fRecordReply = message->GetInt64("reply_to", 0);
		BString problem;
		if (fRecorder.Start(fRecordPath.String(), &problem) != B_OK) {
			_SendRecordState();
			_ShowError(problem.String());
			return;
		}
		fRecordAction = system_time();
		_Send(pp::request::RecordingVoice(fRecordChat, true));
		BMessage tick(kMsgVoiceRecordTick);
		delete fRecordRunner;
		fRecordRunner = new BMessageRunner(BMessenger(this), &tick, 200000);
		_SendRecordState();
	} else if (fRecorder.IsRecording())
		_StopRecording(action == "send");
}


void
App::_StopRecording(bool send)
{
	delete fRecordRunner;
	fRecordRunner = nullptr;
	int32 seconds = 0;
	std::string levels;
	// Under a second is a slip, as in the official apps: not sent.
	bool keep = send && fRecorder.Seconds() >= 1.0;
	status_t status = fRecorder.Stop(keep, &seconds, &levels);
	_Send(pp::request::RecordingVoice(fRecordChat, false));
	if (keep && status == B_OK)
		_Send(pp::request::SendVoice(fRecordChat, fRecordPath.String(), seconds, levels,
			fRecordReply));
	else if (keep) {
		BString text("Could not finish the voice message: ");
		text << strerror(status);
		_ShowError(text);
	}
	fRecordChat = fRecordReply = 0;
	_SendRecordState();
}


void
App::_SendRecordState()
{
	BMessage state(kMsgVoiceRecordState);
	state.AddBool("recording", fRecorder.IsRecording());
	state.AddInt32("seconds", (int32)fRecorder.Seconds());
	state.AddFloat("level", fRecorder.TakeLevel());
	fMain->PostMessage(&state);
}


bool
App::_HandleProfileReply(const json& object, const std::string& extra)
{
	if (extra.rfind("profile:", 0) != 0)
		return false;
	std::string type = pp::Type(object);
	auto result = [&](const std::string& text) {
		BMessage message(kMsgProfileResult);
		message.AddString("text", text.c_str());
		fProfileWindow.SendMessage(&message);
	};
	if (extra == "profile:me" && type == "user") {
		fProfile.Apply(object);
		_Send(pp::request::UserFullInfo(fProfile.userId, "profile:full"));
		_SendProfile();
	} else if (extra == "profile:full" && type == "userFullInfo") {
		fProfile.Apply(object);
		_SendProfile();
	} else if (extra == "profile:sessions" && type == "sessions") {
		BMessage list(kMsgProfileSessions);
		BDateFormat dates;
		for (const pp::Session& s : pp::ParseSessions(object)) {
			list.AddInt64("session_id", s.id);
			list.AddString("application", s.application.c_str());
			list.AddString("device", s.device.c_str());
			list.AddString("location", s.location.c_str());
			BString active = pp::ActiveText(s.lastActive, time(nullptr)).c_str();
			if (active.IsEmpty())
				dates.Format(active, (time_t)s.lastActive, B_MEDIUM_DATE_FORMAT);
			list.AddString("active", active);
			list.AddBool("current", s.current);
			list.AddBool("unconfirmed", s.unconfirmed);
		}
		fProfileWindow.SendMessage(&list);
	} else if (extra == "profile:save") {
		if (type == "error" && fProfileError.empty())
			fProfileError = "Could not save: " + pp::AuthFlow::ErrorText(object);
		if (--fProfileSaving <= 0) {
			fProfileSaving = 0;
			result(fProfileError);
			// The updates that follow refresh the window; the bio's comes
			// with the full info.
			_Send(pp::request::UserFullInfo(fProfile.userId, "profile:full"));
		}
	} else if (extra == "profile:photo") {
		result(type == "error" ? "Could not change the picture: "
			+ pp::AuthFlow::ErrorText(object) : std::string());
	} else if (extra == "profile:terminate") {
		if (type == "error")
			result("Could not log out the device: " + pp::AuthFlow::ErrorText(object));
		_Send(pp::request::ActiveSessions("profile:sessions"));
	}
	return true;
}


void
App::_SendProfile()
{
	if (!fProfileWindow.IsValid())
		return;
	BMessage data(kMsgProfileData);
	data.AddInt64("user_id", fProfile.userId);
	data.AddString("first", fProfile.firstName.c_str());
	data.AddString("last", fProfile.lastName.c_str());
	data.AddString("username", fProfile.username.c_str());
	data.AddString("bio", fProfile.bio.c_str());
	data.AddBool("has_photo", fProfile.photoId != 0);
	BString photo = fFiles.Path(fProfile.photoFileId).c_str();
	data.AddString("photo", photo);
	AddBytes(data, "photo_mini", fProfile.photoPreview);
	fProfileWindow.SendMessage(&data);
	if (photo.IsEmpty() && fProfile.photoFileId != 0) {
		fFiles.Want(fProfile.photoFileId, 32);
		_StartDownloads();
	}
}


void
App::_ShowNewChat(const char* what)
{
	BRect frame = fMain != nullptr ? fMain->Frame() : BScreen().Frame();
	BString kind(what);
	if (kind == "group" || kind == "channel") {
		(new NewGroupWindow(BMessenger(this), kind == "channel", _ContactRows(), frame))->Show();
		if (!fContacts.Loaded())
			_Send(pp::request::GetContacts("contacts"));
		return;
	}
	if (kind == "contact") {
		(new AddContactWindow(BMessenger(this), frame))->Show();
		return;
	}
	if (fNewChatWindow.IsValid()) {
		BMessage activate(B_EXECUTE_PROPERTY);
		activate.AddSpecifier("Active");
		activate.AddBool("data", true);
		fNewChatWindow.SendMessage(&activate);
		return;
	}
	NewChatWindow* window = new NewChatWindow(BMessenger(this), frame);
	fNewChatWindow = BMessenger(window);
	window->Show();
	// Shown at once if they're known, and again when the server answers.
	if (fContacts.Loaded())
		_SendContacts();
	_Send(pp::request::GetContacts("contacts"));
}


std::vector<PersonRow>
App::_ContactRows() const
{
	std::vector<PersonRow> rows;
	for (const pp::Contact& c : fContacts.Matching("")) {
		BString detail = const_cast<App*>(this)->_UserStatusText(c.id);
		if (c.bot)
			detail = "bot";
		else if (detail.IsEmpty() && !c.username.empty())
			detail << "@" << c.username.c_str();
		rows.push_back({c.id, c.Name().c_str(), detail, c.username.c_str(), c.phone.c_str()});
	}
	return rows;
}


void
App::_SendContacts()
{
	if (!fNewChatWindow.IsValid())
		return;
	BMessage list(kMsgNewChatContacts);
	list.AddBool("loaded", fContacts.Loaded());
	for (const PersonRow& row : _ContactRows()) {
		list.AddInt64("id", row.id);
		list.AddString("name", row.name);
		list.AddString("detail", row.detail);
		list.AddString("username", row.username);
		list.AddString("phone", row.phone);
	}
	fNewChatWindow.SendMessage(&list);
}


bool
App::_HandleContactsReply(const json& object, const std::string& extra)
{
	std::string type = pp::Type(object);
	auto answer = [&](const std::string& text) {
		auto it = fCreateReplies.find(extra);
		if (it == fCreateReplies.end())
			return;
		BMessage result(kMsgCreateResult);
		result.AddString("text", text.c_str());
		it->second.SendMessage(&result);
		fCreateReplies.erase(it);
	};
	// People a new group or channel couldn't take: their privacy settings.
	auto reportFailed = [&](const std::vector<int64_t>& failed) {
		if (failed.empty())
			return;
		BString names;
		for (size_t i = 0; i < failed.size(); i++) {
			if (i > 0)
				names << (i + 1 == failed.size() ? " and " : ", ");
			names << fNames.User(failed[i]).c_str();
		}
		BString text;
		text << names << " couldn't be added: their privacy settings only let people they know add them. "
			"You can send them an invite link instead.";
		BAlert* alert = new BAlert("not added", text, "OK", nullptr, nullptr,
			B_WIDTH_AS_USUAL, B_INFO_ALERT);
		alert->Go(nullptr);
	};

	if (extra == "contacts") {
		if (type == "users") {
			fContacts.SetContacts(object);
			_SendContacts();
		}
		return true;
	}
	if (extra.rfind("publicsearch:", 0) == 0) {
		// "publicsearch:<seq>:<query>"
		size_t colon = extra.find(':', 13);
		BMessage found(kMsgNewChatFound);
		found.AddInt32("seq", atoi(extra.c_str() + 13));
		found.AddString("query", colon != std::string::npos ? extra.c_str() + colon + 1 : "");
		for (int64_t id : pp::ChatIds(object)) {
			found.AddInt64("id", id);
			found.AddString("name", fNames.Chat(id).c_str());
			BString detail = _StatusText(id, false);
			if (detail.IsEmpty())
				detail = fPresence.IsChannel(id) ? "Channel" : "Group";
			found.AddString("detail", detail);
		}
		if (fNewChatWindow.IsValid())
			fNewChatWindow.SendMessage(&found);
		return true;
	}
	if (extra.rfind("create:", 0) == 0) {
		int64 chatId = pp::CreatedChatId(object);
		if (type == "error" || chatId == 0) {
			answer("Could not create it: " + pp::AuthFlow::ErrorText(object));
			fCreateMembers.erase(extra);
			return true;
		}
		auto members = fCreateMembers.find(extra);
		if (members != fCreateMembers.end()) {
			if (!members->second.empty()) {
				_Send(pp::request::AddMembers(chatId, members->second,
					"create_members:" + std::to_string(chatId)));
			}
			fCreateMembers.erase(members);
		}
		reportFailed(pp::FailedMembers(object));
		answer("");
		_ShowChat(chatId);
		if (fMain != nullptr)
			fMain->Activate();
		return true;
	}
	if (extra.rfind("create_members:", 0) == 0) {
		if (type == "error") {
			BString text("Could not add the subscribers. ");
			text << pp::AuthFlow::ErrorText(object).c_str();
			_ShowError(text);
		} else
			reportFailed(pp::FailedMembers(object));
		return true;
	}
	if (extra.rfind("addcontact:", 0) == 0) {
		std::vector<int64_t> ids = pp::UserIds(object);
		if (type == "error")
			answer("Could not add the contact: " + pp::AuthFlow::ErrorText(object));
		else if (ids.empty() || ids[0] == 0)
			answer("Nobody on Telegram has this number.");
		else {
			answer("");
			_Send(pp::request::GetContacts("contacts"));
		}
		return true;
	}
	if (extra == "contact_remove") {
		if (type == "error") {
			BString text("Could not remove the contact. ");
			text << pp::AuthFlow::ErrorText(object).c_str();
			_ShowError(text);
		}
		_Send(pp::request::GetContacts("contacts"));
		return true;
	}
	return false;
}


void
App::_SendScheduled()
{
	if (!fScheduledWindow.IsValid())
		return;
	BMessage list(kMsgScheduledList);
	for (const pp::ScheduledMessage& m : fScheduled.Sorted()) {
		list.AddInt64("id", m.id);
		list.AddInt64("send_at", m.sendAt);
		list.AddString("preview", m.preview.c_str());
	}
	fScheduledWindow.SendMessage(&list);
}

void
App::_SendPinned()
{
	BMessage pinned(kMsgPinned);
	pinned.AddInt64("chat_id", fPinned.ChatId());
	const pp::Message* shown = fPinned.Shown();
	pinned.AddInt64("message_id", shown != nullptr ? shown->id : 0);
	pinned.AddString("text", shown != nullptr ? Summary(*shown) : BString());
	pinned.AddInt32("index", fPinned.ShownIndex());
	pinned.AddInt32("count", (int32)fPinned.List().size());
	fMain->PostMessage(&pinned);
}


void
App::_ShowChat(int64 chatId, int64 messageId)
{
	if (chatId == 0)
		return;
	if (fHistory != nullptr && fHistory->ChatId() == chatId) {
		if (messageId != 0)
			_ShowMessage(messageId);
		BMessage select(kMsgSelectChat);
		select.AddInt64("id", chatId);
		fMain->PostMessage(&select);
		return;
	}
	std::vector<pp::Chat> chats = fChats.Sorted(fShownList);
	if (std::none_of(chats.begin(), chats.end(),
			[&](const pp::Chat& c) { return c.id == chatId; })) {
		fExtraChat = chatId;
		_FlushChats();   // before the selection, so the row is there
	}
	fPendingChat = chatId;
	fPendingMessage = messageId;
	BMessage select(kMsgSelectChat);
	select.AddInt64("id", chatId);
	fMain->PostMessage(&select);
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
			fMediaWindow.SendMessage(B_QUIT_REQUESTED);   // the old account's
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
	state.AddString("link", s.link.c_str());
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
	std::vector<pp::Chat> chats = fChats.Sorted(fShownList);
	// A chat opened from a link that isn't in this list: first, for now.
	if (fExtraChat != 0 && fChats.Find(fExtraChat) != nullptr
		&& std::none_of(chats.begin(), chats.end(),
			[&](const pp::Chat& c) { return c.id == fExtraChat; }))
		chats.insert(chats.begin(), *fChats.Find(fExtraChat));
	for (const pp::Chat& c : chats) {
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
		list.AddBool("secret", c.secret);
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

		case kMsgShowNewChat:
			_ShowNewChat(message->GetString("what", ""));
			break;

		case kMsgShowScheduled:
		{
			int64 chatId = message->GetInt64("chat_id", 0);
			if (fScheduledWindow.IsValid()) {
				if (fScheduled.ChatId() == chatId) {
					BMessage activate(B_EXECUTE_PROPERTY);
					activate.AddSpecifier("Active");
					activate.AddBool("data", true);
					fScheduledWindow.SendMessage(&activate);
					break;
				}
				fScheduledWindow.SendMessage(B_QUIT_REQUESTED);
			}
			ScheduledWindow* window = new ScheduledWindow(BMessenger(this), chatId,
				fNames.Chat(chatId).c_str(), fMain->Frame());
			fScheduledWindow = BMessenger(window);
			fScheduled = pp::ScheduledMessages(chatId);
			window->Show();
			_Send(pp::request::ScheduledMessages(chatId, "scheduled:" + std::to_string(chatId)));
			break;
		}
		case kMsgScheduledClosed:
			if (message->GetInt64("chat_id", 0) == fScheduled.ChatId()) {
				fScheduledWindow = BMessenger();
				fScheduled = pp::ScheduledMessages();
			}
			break;
		case kMsgScheduledAction:
		{
			int64 chatId = message->GetInt64("chat_id", 0);
			int64 messageId = message->GetInt64("message_id", 0);
			BString action = message->GetString("action", "");
			if (action == "now")
				_Send(pp::request::SendScheduledNow(chatId, messageId));
			else if (action == "move")
				_Send(pp::request::Reschedule(chatId, messageId, message->GetInt64("send_at", 0)));
			else if (action == "delete")
				_Send(pp::request::Delete(chatId, {messageId}, true));
			break;
		}

		case kMsgShowPoll:
		{
			int64 chatId = message->GetInt64("chat_id", 0);
			(new PollWindow(BMessenger(this), chatId, fPresence.IsChannel(chatId),
				fPollAnswersMax, fMain->Frame()))->Show();
			break;
		}
		case kMsgSendPoll:
		{
			pp::PollDraft poll;
			poll.question = message->GetString("question", "");
			const char* option;
			for (int32 i = 0; message->FindString("option", i, &option) == B_OK; i++)
				poll.options.push_back(option);
			poll.anonymous = message->GetBool("anonymous", true);
			poll.multipleAnswers = message->GetBool("multiple", false);
			poll.quiz = message->GetBool("quiz", false);
			poll.correctOption = message->GetInt32("correct", -1);
			poll.explanation = message->GetString("explanation", "");
			_Send(pp::request::SendPoll(message->GetInt64("chat_id", 0), poll, 0,
				pp::SendOptions()));
			break;
		}
		case kMsgShowLocation:
			(new LocationWindow(BMessenger(this), message->GetInt64("chat_id", 0),
				fMain->Frame()))->Show();
			break;
		case kMsgSendLocation:
			_Send(pp::request::SendLocation(message->GetInt64("chat_id", 0),
				message->GetDouble("latitude", 0), message->GetDouble("longitude", 0), 0,
				pp::SendOptions()));
			break;
		case kMsgNewChatClosed:
			fNewChatWindow = BMessenger();
			break;
		case kMsgNewChatSearch:
		{
			BString query = message->GetString("query", "");
			BString extra("publicsearch:");
			extra << message->GetInt32("seq", 0) << ":" << query;
			_Send(pp::request::SearchPublicChats(query.String(), extra.String()));
			break;
		}
		case kMsgNewChatOpen:
			if (int64 user = message->GetInt64("user_id", 0))
				_Send(pp::request::CreatePrivateChat(user, "openchat"));
			else
				_ShowChat(message->GetInt64("chat_id", 0));
			if (fMain != nullptr)
				fMain->Activate();
			break;
		case kMsgRemoveContact:
			_Send(pp::request::RemoveContact(message->GetInt64("user_id", 0), "contact_remove"));
			break;
		case kMsgCreateGroup:
		{
			BString extra("create:");
			extra << ++fCreateSeq;
			fCreateReplies[extra.String()] = message->ReturnAddress();
			std::string title = message->GetString("title", "");
			std::vector<int64_t> members;
			int64 id;
			for (int32 i = 0; message->FindInt64("user_id", i, &id) == B_OK; i++)
				members.push_back(id);
			if (message->GetBool("channel", false)) {
				// Subscribers are added once the channel is there.
				fCreateMembers[extra.String()] = members;
				_Send(pp::request::NewChannel(title, message->GetString("description", ""),
					extra.String()));
			} else
				_Send(pp::request::NewGroup(title, members, extra.String()));
			break;
		}
		case kMsgAddContact:
		{
			BString extra("addcontact:");
			extra << ++fCreateSeq;
			fCreateReplies[extra.String()] = message->ReturnAddress();
			_Send(pp::request::ImportContact(message->GetString("phone", ""),
				message->GetString("first", ""), message->GetString("last", ""), extra.String()));
			break;
		}

		case kMsgNewSecretChat:
		{
			const pp::Chat* chat = fChats.Find(message->GetInt64("chat_id", 0));
			if (chat != nullptr && chat->userId != 0)
				_Send(pp::request::NewSecretChat(chat->userId, "secret_new"));
			break;
		}
		case kMsgSetSelfDestruct:
		{
			json timer = pp::request::SetSelfDestruct(message->GetInt64("chat_id", 0),
				message->GetInt32("seconds", 0));
			timer["@extra"] = "self_destruct";
			_Send(timer);
			break;
		}

		case kMsgReact:
		{
			json react = pp::request::React(message->GetInt64("chat_id", 0),
				message->GetInt64("message_id", 0), message->GetString("emoji", ""),
				message->GetBool("add", true));
			react["@extra"] = "react";   // a refusal is shown, not just logged
			_Send(react);
			break;
		}

		case kMsgForwardMessage:
			_Send(pp::request::Forward(message->GetInt64("to_chat_id", 0),
				message->GetInt64("from_chat_id", 0), {message->GetInt64("message_id", 0)}));
			break;

		case kMsgShowProfile:
		{
			BMessenger window = fProfileWindow;
			if (window.IsValid()) {
				BMessage activate(B_EXECUTE_PROPERTY);
				activate.AddSpecifier("Active");
				activate.AddBool("data", true);
				window.SendMessage(&activate);
				break;
			}
			ProfileWindow* profile = new ProfileWindow(BMessenger(this));
			fProfileWindow = BMessenger(profile);
			profile->Show();
			fProfile = pp::Profile();
			_Send(pp::request::GetMe("profile:me"));
			_Send(pp::request::ActiveSessions("profile:sessions"));
			break;
		}

		case kMsgProfileClosed:
			fProfileWindow = BMessenger();
			break;

		case kMsgProfileSave:
		{
			std::string first = message->GetString("first", "");
			std::string last = message->GetString("last", "");
			std::string bio = message->GetString("bio", "");
			fProfileError.clear();
			fProfileSaving = 0;
			if (first != fProfile.firstName || last != fProfile.lastName) {
				_Send(pp::request::SetName(first, last, "profile:save"));
				fProfileSaving++;
			}
			if (bio != fProfile.bio) {
				_Send(pp::request::SetBio(bio, "profile:save"));
				fProfileSaving++;
			}
			if (fProfileSaving == 0) {
				BMessage result(kMsgProfileResult);
				result.AddString("text", "");
				fProfileWindow.SendMessage(&result);
			}
			break;
		}

		case kMsgProfilePhoto:
		{
			entry_ref ref;
			BPath path;
			if (message->FindRef("refs", &ref) == B_OK && path.SetTo(&ref) == B_OK)
				_Send(pp::request::SetProfilePhoto(path.Path(), "profile:photo"));
			break;
		}

		case kMsgProfileRemovePhoto:
			if (fProfile.photoId != 0)
				_Send(pp::request::DeleteProfilePhoto(fProfile.photoId, "profile:photo"));
			break;

		case kMsgTerminateSession:
			_Send(pp::request::TerminateSession(message->GetInt64("session_id", 0),
				"profile:terminate"));
			break;

		case kMsgTerminateOthers:
			_Send(pp::request::TerminateOtherSessions("profile:terminate"));
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
			_UpdateDeskbar();
			break;

		case kMsgDeskbarHello:
			message->FindMessenger("item", &fDeskbarItem);
			fUnreadSent = -1;
			_SendUnread();
			break;

		case kMsgDeskbarFiles:
			RefsReceived(message);
			break;

		case kMsgSettingsClosed:
			fSettingsWindow = nullptr;
			break;

		case kMsgShowList:
		{
			fShownList = message->GetString("key", "main");
			fExtraChat = 0;
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
		case kMsgSubmitQr:
			_Send(fAuth->QrRequest(), true);
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
		{
			int64 id = message->GetInt64("id", 0);
			_OpenChat(id);
			if (id != 0 && id == fPendingChat && fPendingMessage != 0)
				_ShowMessage(fPendingMessage);
			fPendingChat = fPendingMessage = 0;
			break;
		}

		case kMsgOpenLink:
			_OpenLink(message->GetString("url", ""));
			break;

		case kMsgJoinInvite:
			if (message->GetInt32("which", 1) == 0)
				_Send(pp::request::JoinByInvite(message->GetString("link", ""), "openchat"));
			break;

		case kMsgVoiceRecord:
			_Record(message);
			break;

		case kMsgMentionSearch:
			_Send(pp::request::SearchMembers(message->GetInt64("chat_id", 0),
				message->GetString("query", ""),
				"mention:" + std::to_string(message->GetInt32("seq", 0))));
			break;

		case kMsgJumpToMention:
		{
			int64 chatId = message->GetInt64("chat_id", 0);
			_Send(pp::request::UnreadMentions(chatId, "mentions:" + std::to_string(chatId)));
			break;
		}

		case kMsgVoiceRecordTick:
			if (!fRecorder.IsRecording())
				break;
			// Telegram's longest is an hour; stop well before, sending it.
			if (fRecorder.Seconds() >= 30 * 60) {
				_StopRecording(true);
				break;
			}
			if (system_time() - fRecordAction > 5000000) {
				fRecordAction = system_time();
				_Send(pp::request::RecordingVoice(fRecordChat, true));
			}
			_SendRecordState();
			break;

		case kMsgPinnedClicked:
			if (const pp::Message* shown = fPinned.Shown()) {
				if (fHistory != nullptr && fHistory->ChatId() == fPinned.ChatId())
					_ShowMessage(shown->id);
				fPinned.Next();
				_SendPinned();
			}
			break;

		case kMsgPin:
			_Send(pp::request::Pin(message->GetInt64("chat_id", 0),
				message->GetInt64("message_id", 0), message->GetBool("pin", true)));
			break;

		case kMsgJoinChat:
			_Send(pp::request::JoinChat(message->GetInt64("chat_id", 0), "join"));
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

		case kMsgSaveFile:
			_SaveFile(message);
			break;
		case kMsgOpenMedia:
			_OpenMedia(message->GetInt64("chat_id", 0), message->GetInt64("message_id", 0),
				message->GetInt32("where", kOpenAsSet));
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
			// People without a username picked from the suggestions.
			std::vector<std::pair<std::string, int64_t>> mentioned;
			const char* name;
			for (int32 i = 0; message->FindString("mention_name", i, &name) == B_OK; i++)
				mentioned.push_back({name, message->GetInt64("mention_user", i, 0)});
			std::string text = message->GetString("text", "");
			pp::SendOptions options;
			options.silent = message->GetBool("silent", false);
			options.sendAt = message->GetInt64("send_at", 0);
			options.whenOnline = message->GetBool("when_online", false);
			json send = pp::request::SendTextWith(chatId, text, message->GetInt64("reply_to", 0),
				pp::MentionEntities(text, mentioned), options);
			if (options.Scheduled())
				send["@extra"] = "send_scheduled";
			_Send(send);
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


// Files opened with Powergram, or dropped on its Deskbar item: the main
// window asks which chat they go to.
void
App::RefsReceived(BMessage* message)
{
	if (fMain == nullptr || !message->HasRef("refs"))
		return;
	BMessage pick(*message);
	pick.what = kMsgDeskbarFiles;
	fMain->PostMessage(&pick);
	fMain->Activate();
}


void
App::_UpdateDeskbar()
{
	BDeskbar deskbar;
	bool shown = deskbar.HasItem(kDeskbarItemName);
	if (!fPrefs.deskbar) {
		if (shown)
			deskbar.RemoveItem(kDeskbarItemName);
		fDeskbarItem = BMessenger();
		return;
	}
	// Deskbar keeps items added from a file across restarts, so one may be
	// there from before; replace it so that it says hello to this run.
	if (shown) {
		if (fDeskbarItem.IsValid())
			return;
		deskbar.RemoveItem(kDeskbarItemName);
	}
	// Next to the app when it runs from where it was built, else in the
	// package's add-ons folder.
	app_info info;
	status_t infoStatus = GetAppInfo(&info);
	BPath app(&info.ref);
	BPath path;
	if (infoStatus == B_OK && app.GetParent(&path) == B_OK)
		path.Append("obj/PowergramDeskbar");
	status_t findStatus = B_OK;
	if (!BEntry(path.Path()).Exists()) {
		// By reference: a pointer would pick the code-address constructor.
		BPathFinder finder(info.ref);
		findStatus = finder.FindPath(B_FIND_PATH_ADD_ONS_DIRECTORY,
			"Powergram/PowergramDeskbar", B_FIND_PATH_EXISTING_ONLY, path);
	}
	entry_ref ref;
	if (findStatus != B_OK || path.InitCheck() != B_OK
		|| get_ref_for_path(path.Path(), &ref) != B_OK) {
		fprintf(stderr, "Powergram: no Deskbar item add-on found (app %s: %s; "
			"add-ons: %s, %s)\n", app.Path() != nullptr ? app.Path() : "?",
			strerror(infoStatus), strerror(findStatus),
			path.Path() != nullptr ? path.Path() : "?");
		return;
	}
	status_t status = deskbar.AddItem(&ref);
	if (status != B_OK)
		fprintf(stderr, "Powergram: Deskbar item: %s\n", strerror(status));
}


void
App::_SendUnread()
{
	if (!fDeskbarItem.IsValid())
		return;
	int32 unread = fChats.UnreadChats();
	if (unread == fUnreadSent)
		return;
	fUnreadSent = unread;
	BMessage count(kMsgDeskbarUnread);
	count.AddInt32("unread", unread);
	fDeskbarItem.SendMessage(&count);
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
