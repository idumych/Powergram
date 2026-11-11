// SPDX-License-Identifier: GPL-3.0-or-later
// Messages between the application and its windows.
#pragma once

#include <Message.h>
#include <SupportDefs.h>

#include <string>

#define APP_SIGNATURE "application/x-vnd.powergram"
#define APP_NAME "Powergram"

// Binary data (tiny JPEG previews) in the parallel arrays of a message.
// BMessage::AddData refuses zero bytes, so an empty value would add no
// entry and shift every later row's data onto the wrong row: each value
// carries a leading marker byte.
inline void
AddBytes(BMessage& message, const char* name, const std::string& bytes)
{
	std::string data(1, '\x01');
	data += bytes;
	message.AddData(name, B_RAW_TYPE, data.data(), data.size(), false);
}


inline std::string
FindBytes(const BMessage* message, const char* name, int32 index)
{
	const void* data;
	ssize_t size;
	if (message->FindData(name, B_RAW_TYPE, index, &data, &size) != B_OK || size < 1)
		return std::string();
	return std::string(static_cast<const char*>(data) + 1, size - 1);
}


enum {
	// app → login window: "step" (int32 pp::AuthStep), "code_info",
	// "code_length" (int32), "hint"
	kMsgAuthState = 'aust',
	// app → login window: "text"
	kMsgAuthError = 'auer',

	// login window → app
	kMsgSubmitApiKey = 'sapi',   // "api_id" (int32), "api_hash"
	kMsgSubmitPhone = 'sphn',    // "text"
	kMsgSubmitCode = 'scod',     // "text"
	kMsgSubmitPassword = 'spwd', // "text"
	kMsgSubmitName = 'snam',     // "first", "last"

	// app → main window: parallel arrays "id" (int64), "title", "text",
	// "sender", "date" (int64), "unread" (int32), "mentions" (int32),
	// "pinned" (bool), "read" (bool)
	kMsgChatList = 'chls',
	kMsgFlushChats = 'flch',
	kMsgFlushHistory = 'flhi',

	// main window → app: "id" (int64) of the chat selected, 0 for none
	kMsgOpenChat = 'opch',
	// app → main window: "chat_id" (int64), "title", "scroll_end" (bool),
	// parallel arrays "id" (int64), "sender", "text", "time", "outgoing",
	// "read", "pending", "failed", "edited", "service" (bools)
	kMsgHistory = 'hist',
	// main window → app: "chat_id" (int64), "text"
	kMsgSendText = 'stxt',
	// main window → app: the entry changed; "chat_id" (int64), "empty" (bool)
	kMsgEntryChanged = 'entc',
	// main window → app: "active" (bool), the window was (de)activated
	kMsgWindowActive = 'wact',
	// app → itself: apply the online status after activation settles
	kMsgOnlineTimer = 'onlt',
	// main window → app: scrolled to the top, load older messages
	kMsgLoadOlder = 'lold',

	// message view → main window: secondary click on a message,
	// "message_id" (int64), "where" (screen BPoint)
	kMsgMessageMenu = 'mmnu',
	// main window → app: "chat_id", "message_id"; app → main window: the
	// same plus bools "can_reply", "can_edit", "can_delete_self",
	// "can_delete_all", "can_copy"
	kMsgMessageProperties = 'mprp',
	// main window → app: "chat_id", "message_id", "text"
	kMsgEditText = 'etxt',
	// main window → app: "chat_id", "message_id", "for_everyone" (bool)
	kMsgDeleteMessage = 'dmsg',
	// app → main window: "id" (int64) of a chat to select (notification click)
	kMsgSelectChat = 'slct',

	// message view → main window → app: a photo was clicked;
	// "message_id" (int64), plus "chat_id" (int64) on the way to the app
	kMsgOpenMedia = 'opmd',
	// message menu → save panel → main window → app: save a message's file;
	// "message_id" (int64), the panel's "directory" (entry_ref) and "name",
	// plus "chat_id" (int64) on the way to the app
	kMsgSaveFile = 'svfl',
	// main window → app: a Telegram link or @username to open in the app,
	// "url" (string); the Open window posts it to the main window first
	kMsgOpenLink = 'opln',
	// Chat menu → main window: show the "Open username or link" window
	kMsgOpenLinkAsk = 'olak',
	// invite question → app: join through "link" (string); its "which"
	// (int32) is the alert's button, 0 for Join
	kMsgJoinInvite = 'jnin',
	// main window → app: join the public chat "chat_id" (int64) on screen
	kMsgJoinChat = 'jnch',
	// app → main window: the pinned message the bar shows: "chat_id",
	// "message_id" (int64, 0 hides the bar), "text" (string), "index" and
	// "count" (int32)
	kMsgPinned = 'pind',
	// pinned bar → main window → app: jump to it and show the next one
	kMsgPinnedClicked = 'pinc',
	// message menu → main window → app: "message_id" (int64), "pin" (bool),
	// plus "chat_id" (int64) on the way to the app
	kMsgPin = 'pin ',
	// main window → app: "action" (string: "start", "send" or "cancel"),
	// "chat_id" and "reply_to" (int64)
	kMsgVoiceRecord = 'vrec',
	// app → main window: "recording" (bool), "seconds" (int32), "level"
	// (float, 0..1)
	kMsgVoiceRecordState = 'vrst',
	// app → itself, while recording
	kMsgVoiceRecordTick = 'vrtk',
	// main window → app: members matching "query" (string) in "chat_id"
	// (int64), answer tagged "seq" (int32)
	kMsgMentionSearch = 'mnsr',
	// app → main window: "seq" (int32), and per member "user_id" (int64),
	// "name" and "username" (string)
	kMsgMentionResults = 'mnrs',
	// mention popup → main window: "index" (int32) clicked
	kMsgMentionChosen = 'mnch',
	// message view → main window → app: go to the oldest unread mention
	kMsgJumpToMention = 'jmen',
	kMsgSaveAsk = 'svak',

	// app → main window: the voice message playing, "message_id" (int64,
	// 0 when none) and "progress" (float, 0..1)
	kMsgVoiceProgress = 'vprg',
	// app → itself, while a voice message plays
	kMsgVoiceTick = 'vtik',

	// main window → app: "chat_id" (int64) whose info to show
	kMsgShowChatInfo = 'shci',
	// app → info window: "chat_id", "title", "status", "about", "private",
	// "avatar", "avatar_mini", "initials", "member_count", "member" (names),
	// "photo_id"/"photo_path"/"photo_mini" (parallel arrays)
	kMsgChatInfo = 'chif',
	// info window → app: "chat_id", it closed
	kMsgChatInfoClosed = 'chic',

	// main window → app: "chat_id" (int64), "refs", "caption", "reply_to"
	kMsgSendFiles = 'sfil',

	// emoji picker → main window: "emoji"; main window → app: the same,
	// to remember it as recently used
	kMsgInsertEmoji = 'iemj',
	kMsgEmojiUsed = 'uemj',
	// sticker picker → main window: "file_id", "width", "height" (int32),
	// "emoji"; main window → app: plus "chat_id", "reply_to"
	kMsgSendSticker = 'sstk',
	// sticker picker → app: the list of sets, or one set ("set_id", 0 for
	// the recently used stickers)
	kMsgStickerSetsWanted = 'wsts',
	kMsgStickerSetWanted = 'wstk',
	// app → sticker picker: "id" (int64) and "title" arrays
	kMsgStickerSets = 'ssts',
	// app → sticker picker: "set_id", "update" (bool), arrays "file_id",
	// "width", "height" (int32), "emoji", "image" (path or empty)
	kMsgStickers = 'stks',

	// message view → main window: a link was clicked, "url"
	kMsgOpenUrl = 'ourl',

	// app → main window: "chat_id" (int64), "status" (header subtitle),
	// "active" (bool: typing and the like, drawn in the accent colour)
	kMsgChatStatus = 'csts',
	// app → itself: refresh statuses (typing expires, "last seen")
	kMsgPresenceTick = 'ptck',

	// app → main window: "chat_id", "text": the opened chat's draft
	kMsgDraft = 'drft',
	// main window → app: "chat_id", "text": the entry when leaving a chat
	kMsgSaveDraft = 'sdft',

	// message view → main window → app: scrolled to the bottom of a partly
	// loaded history / the jump-to-latest button was clicked
	kMsgLoadNewer = 'lnew',
	kMsgJumpToLatest = 'jump',

	// main window → app: the chat list's right-click menu; "chat_id",
	// "action" ("read", "unread", "mute", "unmute", "pin", "unpin",
	// "archive", "unarchive", "delete")
	kMsgChatAction = 'chac',

	// app → main window: the lists to choose from; "key" and "name" arrays
	// ("main"/"All chats", "folder:<id>"/name, "archive"/"Archive")
	kMsgChatLists = 'clst',
	// main window → app: "key", the list to show
	kMsgShowList = 'shls',

	// main window → app: "from_chat_id", "message_id", "to_chat_id"
	kMsgForwardMessage = 'fwdm',

	// message view or menu → main window → app: "message_id", "emoji",
	// "add" (bool); "chat_id" added by the window
	kMsgReact = 'reac',

	// main window → app: search the open chat, "query"; step through the
	// results, "delta" (int32, +1 older / -1 newer)
	kMsgFind = 'find',
	kMsgFindStep = 'fstp',
	// app → main window: "index", "count" (int32; index -1 for none)
	kMsgFindResult = 'fres',

	// settings window → app: the settings (see AppSettings::ToMessage);
	// app → main window: the same, to apply the text size
	kMsgSettingsChanged = 'stch',
	kMsgSettingsClosed = 'stcl',
	// main window menu → app
	kMsgShowSettings = 'shst',

	kMsgLogOut = 'lout',
	kMsgLogOutConfirmed = 'loco',
	kMsgForceQuit = 'fqut'
};
