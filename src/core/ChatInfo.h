// SPDX-License-Identifier: GPL-3.0-or-later
// A chat's info panel: the bio or description, the members and the
// latest shared photos, gathered from TDLib's replies.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ChatHistory.h"
#include "Json.h"

namespace pp {

struct ChatInfo {
	int64_t chatId = 0;
	std::string about;            // a user's bio, a group's description
	int32_t memberCount = 0;
	std::vector<int64_t> members; // user ids, in Telegram's order
	std::vector<Message> photos;  // newest first

	// Takes userFullInfo, basicGroupFullInfo, supergroupFullInfo,
	// chatMembers and foundChatMessages; true if it was one of them.
	bool Apply(const json& object);
};

namespace request {

// What the panel asks for: the full info of the chat's user, basic group
// or supergroup, a supergroup's first members (not a channel's: only its
// admins may list them) and the latest photos. Replies carry "@extra"
// "info:<chat id>".
std::vector<json> ChatInfo(int64_t chatId, int64_t userId, int64_t basicGroupId,
	int64_t supergroupId, bool channel);

} // namespace request

} // namespace pp
