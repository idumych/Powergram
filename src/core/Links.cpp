// SPDX-License-Identifier: GPL-3.0-or-later
#include "Links.h"

#include <cctype>
#include <cstdlib>

#include "AuthFlow.h"

namespace pp {

static std::string
Lower(std::string text)
{
	for (char& c : text)
		c = (char)std::tolower((unsigned char)c);
	return text;
}


static std::string
Trim(const std::string& text)
{
	size_t from = text.find_first_not_of(" \t\r\n");
	if (from == std::string::npos)
		return std::string();
	size_t to = text.find_last_not_of(" \t\r\n");
	return text.substr(from, to - from + 1);
}


bool
IsTelegramLink(const std::string& url)
{
	std::string u = Lower(url);
	if (u.rfind("tg:", 0) == 0)
		return true;
	for (const char* scheme : {"https://", "http://", ""}) {
		if (u.rfind(scheme, 0) != 0)
			continue;
		std::string rest = u.substr(std::string(scheme).size());
		if (rest.rfind("www.", 0) == 0)
			rest = rest.substr(4);
		for (const char* host : {"t.me", "telegram.me", "telegram.dog"}) {
			std::string h(host);
			if (rest.rfind(h, 0) == 0 && (rest.size() == h.size() || rest[h.size()] == '/'))
				return true;
		}
		if (*scheme != '\0')
			break;  // a scheme, but not one of these hosts
	}
	return false;
}


std::string
NormalizeLink(const std::string& typed)
{
	std::string text = Trim(typed);
	if (text.empty())
		return std::string();
	if (IsTelegramLink(text)) {
		std::string lower = Lower(text);
		if (lower.rfind("tg:", 0) == 0 || lower.rfind("http", 0) == 0)
			return text;
		return "https://" + text;
	}
	if (text[0] == '+') {
		for (size_t i = 1; i < text.size(); i++)
			if (!std::isalnum((unsigned char)text[i]) && text[i] != '_' && text[i] != '-')
				return std::string();
		return text.size() > 1 ? "https://t.me/" + text : std::string();
	}
	if (text[0] == '@')
		text = text.substr(1);
	// Usernames: letters, digits and underscores, at least 4 long (some
	// old and collectible ones are shorter; TDLib will say).
	if (text.empty())
		return std::string();
	for (char c : text)
		if (!std::isalnum((unsigned char)c) && c != '_')
			return std::string();
	return "https://t.me/" + text;
}


LinkAction
ParseLinkType(const json& link)
{
	LinkAction action;
	std::string type = Type(link);
	if (type == "internalLinkTypePublicChat") {
		action.kind = LinkAction::kPublicChat;
		action.value = Str(link, "chat_username");
	} else if (type == "internalLinkTypeChatInvite") {
		action.kind = LinkAction::kInvite;
		action.value = Str(link, "invite_link");
	} else if (type == "internalLinkTypeMessage") {
		action.kind = LinkAction::kMessage;
		action.value = Str(link, "url");
	}
	return action;
}


int64_t
MentionedUser(const std::string& url)
{
	const std::string prefix = "tg://user?id=";
	if (Lower(url).rfind(prefix, 0) != 0)
		return 0;
	return std::atoll(url.c_str() + prefix.size());
}


std::string
InviteQuestion(const json& info)
{
	std::string type = Type(Field(info, "type"));
	bool channel = type == "inviteLinkChatTypeChannel";
	std::string text = std::string(channel ? "Join the channel " : "Join the group ")
		+ "\xE2\x80\x9C" + Str(info, "title") + "\xE2\x80\x9D?";
	int64_t members = Int(info, "member_count");
	if (members > 0) {
		// 1,204 members
		std::string digits = std::to_string(members), grouped;
		for (size_t i = 0; i < digits.size(); i++) {
			if (i > 0 && (digits.size() - i) % 3 == 0)
				grouped += ',';
			grouped += digits[i];
		}
		text += "\n" + grouped + (members == 1 ? (channel ? " subscriber" : " member")
			: (channel ? " subscribers" : " members"));
	}
	std::string about = Str(info, "description");
	if (!about.empty())
		text += "\n\n" + about;
	return text;
}


int64_t
InviteChat(const json& info)
{
	// TDLib gives the chat when the user can already open it: a member, or
	// a public chat. Otherwise only after joining.
	int64_t chat = Int(info, "chat_id");
	if (chat != 0 && Int(info, "accessible_for") == 0)
		return chat;
	return 0;
}


std::string
LinkErrorText(const json& error)
{
	std::string msg = Str(error, "message");
	struct { const char* code; const char* text; } known[] = {
		{"USERNAME_NOT_OCCUPIED", "No one on Telegram has that username."},
		{"USERNAME_INVALID", "That is not a valid username."},
		{"INVITE_HASH_EXPIRED", "This invite link has expired."},
		{"INVITE_HASH_INVALID", "This invite link is not valid."},
		{"INVITE_HASH_EMPTY", "This invite link is not valid."},
		{"INVITE_REQUEST_SENT", "Your request to join has been sent. An admin has to approve it."},
		{"CHANNELS_TOO_MUCH", "You are in too many groups and channels to join another one."},
		{"USERS_TOO_MUCH", "This group is full."},
		{"CHANNEL_PRIVATE", "This chat is private, or you were removed from it."},
		{"Chat not found", "This chat could not be found."},
	};
	for (const auto& k : known)
		if (msg.find(k.code) != std::string::npos)
			return k.text;
	return AuthFlow::ErrorText(error);
}


namespace request {

json
LinkType(const std::string& url, const std::string& extra)
{
	return {{"@type", "getInternalLinkType"}, {"link", url}, {"@extra", extra}};
}


json
SearchPublicChat(const std::string& username, const std::string& extra)
{
	return {{"@type", "searchPublicChat"}, {"username", username}, {"@extra", extra}};
}


json
CheckInvite(const std::string& link, const std::string& extra)
{
	return {{"@type", "checkChatInviteLink"}, {"invite_link", link}, {"@extra", extra}};
}


json
JoinByInvite(const std::string& link, const std::string& extra)
{
	return {{"@type", "joinChatByInviteLink"}, {"invite_link", link}, {"@extra", extra}};
}


json
MessageLinkInfo(const std::string& url, const std::string& extra)
{
	return {{"@type", "getMessageLinkInfo"}, {"url", url}, {"@extra", extra}};
}


json
CreatePrivateChat(int64_t userId, const std::string& extra)
{
	return {{"@type", "createPrivateChat"}, {"user_id", userId}, {"force", false},
		{"@extra", extra}};
}


json
JoinChat(int64_t chatId, const std::string& extra)
{
	return {{"@type", "joinChat"}, {"chat_id", chatId}, {"@extra", extra}};
}

} // namespace request

} // namespace pp
