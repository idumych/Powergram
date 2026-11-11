// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChatInfo.h"

#include <algorithm>

namespace pp {

static std::vector<int64_t>
MemberIds(const json& members)
{
	std::vector<int64_t> ids;
	if (!members.is_array())
		return ids;
	for (const json& member : members) {
		const json& sender = Field(member, "member_id");
		if (Type(sender) == "messageSenderUser")
			ids.push_back(Int(sender, "user_id"));
	}
	return ids;
}


bool
ChatInfo::Apply(const json& object)
{
	std::string type = Type(object);
	if (type == "userFullInfo") {
		about = Str(Field(object, "bio"), "text");
	} else if (type == "basicGroupFullInfo") {
		about = Str(object, "description");
		members = MemberIds(Field(object, "members"));
		memberCount = (int32_t)members.size();
	} else if (type == "supergroupFullInfo") {
		about = Str(object, "description");
		memberCount = (int32_t)Int(object, "member_count");
	} else if (type == "chatMembers") {
		members = MemberIds(Field(object, "members"));
		memberCount = std::max(memberCount, (int32_t)Int(object, "total_count"));
	} else if (type == "foundChatMessages") {
		photos.clear();
		const json& messages = Field(object, "messages");
		if (messages.is_array()) {
			for (const json& m : messages) {
				Message parsed = ParseMessage(m);
				if (parsed.media.kind == Media::kPhoto)
					photos.push_back(parsed);
			}
		}
	} else
		return false;
	return true;
}


namespace request {

std::vector<json>
ChatInfo(int64_t chatId, int64_t userId, int64_t basicGroupId, int64_t supergroupId,
	bool channel)
{
	std::string extra = "info:" + std::to_string(chatId);
	std::vector<json> requests;
	if (userId != 0)
		requests.push_back({{"@type", "getUserFullInfo"}, {"user_id", userId}});
	else if (basicGroupId != 0)
		requests.push_back({{"@type", "getBasicGroupFullInfo"}, {"basic_group_id", basicGroupId}});
	else if (supergroupId != 0) {
		requests.push_back({{"@type", "getSupergroupFullInfo"}, {"supergroup_id", supergroupId}});
		if (!channel)
			requests.push_back({{"@type", "getSupergroupMembers"}, {"supergroup_id", supergroupId},
				{"filter", {{"@type", "supergroupMembersFilterRecent"}}},
				{"offset", 0}, {"limit", 50}});
	}
	requests.push_back({{"@type", "searchChatMessages"}, {"chat_id", chatId}, {"query", ""},
		{"from_message_id", 0}, {"offset", 0}, {"limit", 24},
		{"filter", {{"@type", "searchMessagesFilterPhoto"}}}});
	for (json& r : requests)
		r["@extra"] = extra;
	return requests;
}

} // namespace request

} // namespace pp
