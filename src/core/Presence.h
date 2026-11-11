// SPDX-License-Identifier: GPL-3.0-or-later
// Who is online and who is typing: user statuses, group sizes and chat
// actions ("typing…", "recording a voice message…"), for the chat header.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Json.h"

namespace pp {

struct UserStatus {
	enum Kind { kUnknown, kOnline, kOffline, kRecently, kLastWeek, kLastMonth } kind = kUnknown;
	int64_t time = 0;    // offline: when last seen; online: until when
};

class Presence {
public:
	// Takes updateUser, updateUserStatus, updateNewChat, updateBasicGroup,
	// updateSupergroup and updateChatAction; true if it was one of them.
	bool Apply(const json& object, int64_t now);

	UserStatus Status(int64_t userId) const;
	// The user behind a private (or secret) chat, 0 for groups.
	int64_t PrivateUser(int64_t chatId) const;
	// Members of a group or channel, 0 if not known.
	int32_t MemberCount(int64_t chatId) const;
	bool IsChannel(int64_t chatId) const;
	// The basic group or supergroup behind a chat, 0 if it isn't one.
	int64_t BasicGroup(int64_t chatId) const;
	int64_t Supergroup(int64_t chatId) const;
	// Not a member of this group or channel (left, banned, or just looking
	// at a public one): the chat offers to join instead of the entry.
	bool CanJoin(int64_t chatId) const;

	struct Action {
		int64_t senderId;
		std::string kind;    // "typing", "voice", "photo", "video", "file", "sticker", …
		int64_t since;
	};
	// Current actions in a chat; older than 6 s count as stopped.
	std::vector<Action> Actions(int64_t chatId, int64_t now) const;

private:
	std::map<int64_t, UserStatus> fStatuses;
	std::map<int64_t, int64_t> fPrivateUser;     // chat → user
	std::map<int64_t, int64_t> fBasicGroup;      // chat → basic group id
	std::map<int64_t, int64_t> fSupergroup;      // chat → supergroup id
	std::map<int64_t, bool> fChannel;            // chat → is a channel
	std::map<int64_t, int32_t> fBasicCounts;
	std::map<int64_t, int32_t> fSuperCounts;
	std::map<int64_t, bool> fBasicMember;        // basic group → we're in it
	std::map<int64_t, bool> fSuperMember;        // supergroup → we're in it
	std::map<int64_t, std::map<int64_t, Action>> fActions;  // chat → sender → action
};

// Header text for an action list, e.g. "typing…", "Ann is typing…",
// "Ann and Bo are typing…", "3 people are typing…". `names` gives a sender
// id's short name; `private` leaves names out.
std::string ActionText(const std::vector<Presence::Action>& actions,
	const std::map<int64_t, std::string>& names, bool privateChat);

} // namespace pp
