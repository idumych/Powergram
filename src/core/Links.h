// SPDX-License-Identifier: GPL-3.0-or-later
// Telegram links (t.me/…, tg://…) and @usernames, opened in the app the
// way the official apps open them: TDLib says what a link is, and the app
// finds the chat, asks to join, or jumps to the message.
#pragma once

#include <cstdint>
#include <string>

#include "Json.h"

namespace pp {

// Whether TDLib should look at a link rather than the browser: t.me,
// telegram.me and telegram.dog links, and tg: links.
bool IsTelegramLink(const std::string& url);

// What the user typed in "Open username or link…" as a link: "@name" or
// "name" → https://t.me/name, "+hash" → https://t.me/+hash; links as they
// are. "" if it can't be one.
std::string NormalizeLink(const std::string& typed);

// What to do with a link, from TDLib's internalLinkType (or our own
// tg://user?id= for mentions of people without a username).
struct LinkAction {
	enum Kind { kBrowser, kPublicChat, kInvite, kMessage, kUser } kind = kBrowser;
	std::string value;   // the username, invite link or message link
	int64_t userId = 0;
};
LinkAction ParseLinkType(const json& internalLinkType);
// tg://user?id=N, as mentions without a username are given; 0 if not one.
int64_t MentionedUser(const std::string& url);

// "Join Haiku app makers? 1,204 members" from a chatInviteLinkInfo; the
// chat to open straight away if the user can already see it, else 0.
std::string InviteQuestion(const json& chatInviteLinkInfo);
int64_t InviteChat(const json& chatInviteLinkInfo);

// A failed link, username or join, in words.
std::string LinkErrorText(const json& error);

namespace request {

json LinkType(const std::string& url, const std::string& extra);
json SearchPublicChat(const std::string& username, const std::string& extra);
json CheckInvite(const std::string& link, const std::string& extra);
json JoinByInvite(const std::string& link, const std::string& extra);
json MessageLinkInfo(const std::string& url, const std::string& extra);
json CreatePrivateChat(int64_t userId, const std::string& extra);
json JoinChat(int64_t chatId, const std::string& extra);

} // namespace request

} // namespace pp
