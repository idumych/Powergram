// SPDX-License-Identifier: GPL-3.0-or-later
#include "Mentions.h"

#include <cctype>

namespace pp {

bool
MentionQuery(const std::string& text, size_t cursor, std::string* query, size_t* at)
{
	if (cursor > text.size())
		return false;
	// Back over the word: letters, digits, underscores and any non-ASCII
	// (names in other scripts).
	size_t i = cursor;
	while (i > 0) {
		unsigned char c = (unsigned char)text[i - 1];
		if (c == '@' || !(std::isalnum(c) || c == '_' || c >= 0x80))
			break;
		i--;
	}
	if (i == 0 || text[i - 1] != '@')
		return false;
	size_t sign = i - 1;
	// The @ starts a word: not part of an e-mail address.
	if (sign > 0) {
		unsigned char before = (unsigned char)text[sign - 1];
		if (std::isalnum(before) || before == '_' || before >= 0x80)
			return false;
	}
	if (cursor - i > 32)
		return false;   // longer than any username
	*query = text.substr(i, cursor - i);
	*at = sign;
	return true;
}


std::string
InsertMention(const std::string& text, size_t at, size_t cursor, const std::string& name,
	const std::string& username, size_t* newCursor)
{
	std::string inserted = (username.empty() ? name : "@" + username) + " ";
	// No doubled space when one already follows.
	size_t end = cursor;
	if (end < text.size() && text[end] == ' ')
		end++;
	std::string result = text.substr(0, at) + inserted + text.substr(end);
	*newCursor = at + inserted.size();
	return result;
}


json
MentionEntities(const std::string& text,
	const std::vector<std::pair<std::string, int64_t>>& mentions,
	const std::vector<size_t>& inserted)
{
	auto wordChar = [](unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; };
	json entities = json::array();
	for (size_t m = 0; m < mentions.size(); m++) {
		const auto& mention = mentions[m];
		if (mention.first.empty())
			continue;
		const std::string& name = mention.first;
		size_t want = m < inserted.size() ? inserted[m] : 0;
		size_t at = std::string::npos;
		if (m < inserted.size() && want <= text.size() && text.compare(want, name.size(), name) == 0)
			at = want;   // still where it was put
		else {
			// Edits before it moved it: the nearest whole-word match.
			for (size_t i = text.find(name); i != std::string::npos; i = text.find(name, i + 1)) {
				size_t end = i + name.size();
				if ((i > 0 && wordChar(text[i - 1])) || (end < text.size() && wordChar(text[end])))
					continue;
				if (at == std::string::npos
					|| (i > want ? i - want : want - i) < (at > want ? at - want : want - at))
					at = i;
			}
		}
		if (at == std::string::npos)
			continue;   // edited out
		entities.push_back({{"@type", "textEntity"},
			{"offset", Utf8ToUtf16(text, at)},
			{"length", Utf8ToUtf16(text, at + mention.first.size()) - Utf8ToUtf16(text, at)},
			{"type", {{"@type", "textEntityTypeMentionName"}, {"user_id", mention.second}}}});
	}
	return entities;
}


std::vector<int64_t>
MemberUserIds(const json& chatMembers)
{
	std::vector<int64_t> ids;
	const json& members = Field(chatMembers, "members");
	if (!members.is_array())
		return ids;
	for (const json& member : members) {
		const json& sender = Field(member, "member_id");
		if (Type(sender) == "messageSenderUser")
			ids.push_back(Int(sender, "user_id"));
	}
	return ids;
}


std::string
FirstName(const std::string& name)
{
	size_t space = name.find(' ');
	return space == std::string::npos ? name : name.substr(0, space);
}


namespace request {

json
SearchMembers(int64_t chatId, const std::string& query, const std::string& extra)
{
	return {{"@type", "searchChatMembers"}, {"chat_id", chatId}, {"query", query},
		{"limit", 8}, {"@extra", extra}};
}


json
UnreadMentions(int64_t chatId, const std::string& extra)
{
	return {{"@type", "searchChatMessages"}, {"chat_id", chatId}, {"query", ""},
		{"from_message_id", 0}, {"offset", 0}, {"limit", 100},
		{"filter", {{"@type", "searchMessagesFilterUnreadMention"}}}, {"@extra", extra}};
}

} // namespace request

} // namespace pp
