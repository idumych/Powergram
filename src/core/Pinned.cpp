// SPDX-License-Identifier: GPL-3.0-or-later
#include "Pinned.h"

#include <algorithm>

namespace pp {

void
PinnedMessages::SetFound(const json& found)
{
	// Keep showing the same message if it's still pinned.
	int64_t shown = Shown() != nullptr ? Shown()->id : 0;
	fList.clear();
	const json& messages = Field(found, "messages");
	if (messages.is_array()) {
		for (const json& m : messages)
			fList.push_back(ParseMessage(m));
	}
	std::sort(fList.begin(), fList.end(),
		[](const Message& a, const Message& b) { return a.id > b.id; });
	fShown = 0;
	for (size_t i = 0; i < fList.size(); i++) {
		if (fList[i].id == shown)
			fShown = (int)i;
	}
}


bool
PinnedMessages::Affects(const json& update) const
{
	if (Int(update, "chat_id") != fChatId || fChatId == 0)
		return false;
	std::string type = Type(update);
	if (type == "updateMessageIsPinned")
		return true;
	auto pinned = [&](int64_t id) {
		return std::any_of(fList.begin(), fList.end(),
			[&](const Message& m) { return m.id == id; });
	};
	if (type == "updateMessageContent" || type == "updateMessageEdited")
		return pinned(Int(update, "message_id"));
	if (type == "updateDeleteMessages" && Bool(update, "is_permanent")) {
		const json& ids = Field(update, "message_ids");
		if (ids.is_array()) {
			for (const json& id : ids)
				if (pinned(ToInt64(id)))
					return true;
		}
	}
	return false;
}


const Message*
PinnedMessages::Shown() const
{
	return fShown >= 0 && fShown < (int)fList.size() ? &fList[fShown] : nullptr;
}


void
PinnedMessages::Next()
{
	if (!fList.empty())
		fShown = (fShown + 1) % (int)fList.size();
}


namespace request {

json
Pinned(int64_t chatId)
{
	return {{"@type", "searchChatMessages"}, {"chat_id", chatId}, {"query", ""},
		{"from_message_id", 0}, {"offset", 0}, {"limit", 50},
		{"filter", {{"@type", "searchMessagesFilterPinned"}}},
		{"@extra", "pinned:" + std::to_string(chatId)}};
}


json
Pin(int64_t chatId, int64_t messageId, bool pin)
{
	if (!pin)
		return {{"@type", "unpinChatMessage"}, {"chat_id", chatId},
			{"message_id", messageId}, {"@extra", "pin"}};
	return {{"@type", "pinChatMessage"}, {"chat_id", chatId}, {"message_id", messageId},
		{"disable_notification", false}, {"only_for_self", false}, {"@extra", "pin"}};
}

} // namespace request

} // namespace pp
