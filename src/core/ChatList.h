// SPDX-License-Identifier: GPL-3.0-or-later
// The main chat list, built from TDLib updates and kept in Telegram's order.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Json.h"

namespace pp {

struct Chat {
	int64_t id = 0;
	std::string title;
	std::string lastText;     // one line: the last message, as Paper Plane shows it
	std::string lastSender;   // "You" or a name; empty in private chats
	int64_t lastDate = 0;     // unix time
	int64_t order = 0;        // position in chatListMain; 0 = not in the list
	bool pinned = false;
	int32_t unread = 0;
	int32_t unreadMentions = 0;
	bool outgoingRead = false; // last message is ours and was read
	bool outgoing = false;
	int64_t lastReadOutbox = 0; // our messages up to this id were read
	std::string draft;          // unsent text kept for this chat
	int64_t lastMessageId = 0;  // the chat's newest message
	int64_t lastReadInbox = 0;  // incoming messages up to this id were read
	int32_t photoFileId = 0;    // small (160 px) chat photo, 0: none
	std::string photoPreview;   // its tiny inline JPEG

	enum Kind { kPrivate, kGroup, kChannel } kind = kPrivate;
	// Notification settings; "use default" means the scope's (kind's).
	bool useDefaultMute = true;
	int32_t muteFor = 0;
	bool useDefaultPreview = true;
	bool showPreview = true;

	bool markedUnread = false;  // "Mark as unread" from any device
	// Position in every list it's in: "main", "archive", "folder:<id>".
	struct Position {
		int64_t order = 0;
		bool pinned = false;
	};
	std::map<std::string, Position> lists;
	bool InList(const std::string& list) const
	{
		auto it = lists.find(list);
		return it != lists.end() && it->second.order != 0;
	}
};

// "main", "archive" or "folder:<id>" for a TDLib ChatList object.
std::string ListKey(const json& chatList);

class ChatList {
public:
	// Applies a TDLib update; returns true if anything shown changed.
	bool Apply(const json& object);

	// Chats in a list ("main", "archive", "folder:<id>"), in display order
	// (pinned first, then newest).
	std::vector<Chat> Sorted(const std::string& list = "main") const;

	const Chat* Find(int64_t id) const;
	size_t Size() const { return fChats.size(); }

	bool IsMuted(int64_t chatId) const;
	// Chats in the main list with something unread that aren't muted (a
	// mention counts even when muted), as Telegram's badges count them.
	int32_t UnreadChats() const;
	bool ShowsPreview(int64_t chatId) const;

	// Whether a new message deserves a desktop notification: incoming, not
	// muted, not in the chat on screen, recent (TDLib replays missed ones
	// at start-up), and at most one per chat every few seconds.
	bool ShouldNotify(const json& message, int64_t visibleChat, int64_t now);

	// Request that makes TDLib send the first `limit` chats of a list as updates.
	static json LoadRequest(int limit, const std::string& list = "main");

	// The user's chat folders (updateChatFolders), in their order.
	struct Folder {
		int32_t id;
		std::string name;
	};
	const std::vector<Folder>& Folders() const { return fFolders; }
	// The chat list's right-click menu.
	static std::vector<json> MarkReadRequests(const Chat& chat);
	static json MarkUnreadRequest(int64_t chatId);
	// Mutes for good (or unmutes); other notification settings go back to
	// the defaults, as TDLib takes the whole settings object.
	static json MuteRequest(int64_t chatId, bool mute);
	static json PinRequest(int64_t chatId, const std::string& list, bool pin);
	static json MoveRequest(int64_t chatId, const std::string& toList);
	// Private chats: delete the history for us; groups and channels: leave.
	static std::vector<json> DeleteRequests(const Chat& chat);
	static json ListObject(const std::string& list);

	// Saves (or with "" clears) the chat's draft on Telegram.
	static json DraftRequest(int64_t chatId, const std::string& text);

private:
	void ApplyPositions(Chat& chat, const json& positions);
	void ApplyLastMessage(Chat& chat, const json& message);

	std::map<int64_t, Chat> fChats;
	std::vector<Folder> fFolders;
	struct Scope { int32_t muteFor = 0; bool showPreview = true; };
	Scope fScopes[3];
	std::map<int64_t, int64_t> fLastNotified;
};

// One line of text for a message's content ("Photo", "Sticker 😀", the text).
std::string MessagePreview(const json& content);

// Initials for an avatar: "Tyrone Ford" → "TF", "harold" → "H".
std::string Initials(const std::string& title);

} // namespace pp
