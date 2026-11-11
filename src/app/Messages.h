// SPDX-License-Identifier: GPL-3.0-or-later
// Messages between the application and its windows.
#pragma once

#include <Message.h>
#include <SupportDefs.h>

#include <string>

#define APP_SIGNATURE "application/x-vnd.paper-plane"
#define APP_NAME "Paper Plane"

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

	kMsgLogOut = 'lout',
	kMsgLogOutConfirmed = 'loco',
	kMsgForceQuit = 'fqut'
};
