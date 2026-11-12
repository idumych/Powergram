// SPDX-License-Identifier: GPL-3.0-or-later
// Contacts and starting chats: your contacts (followed through updateUser),
// finding people and public chats, adding a contact by phone number, and
// creating groups and channels.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Json.h"

namespace pp {

struct Contact {
	int64_t id = 0;
	std::string firstName;
	std::string lastName;
	std::string username;    // the first active one, without the @
	std::string phone;       // digits only, as TDLib gives it
	bool contact = false;
	bool mutual = false;
	bool bot = false;
	bool deleted = false;

	std::string Name() const;
};

class Contacts {
public:
	// Takes updateUser (and user replies); true if a contact changed.
	bool Apply(const json& object);
	// Takes the getContacts reply: these are the contacts, whatever the
	// users said before. TDLib sends each one's updateUser first.
	void SetContacts(const json& users);
	bool Loaded() const { return fLoaded; }

	const Contact* Find(int64_t id) const;
	// Contacts whose name, username or phone matches `query` ("" for all),
	// sorted by name.
	std::vector<Contact> Matching(const std::string& query) const;

private:
	std::map<int64_t, Contact> fUsers;
	bool fLoaded = false;
};

// Whether `query` matches someone: a word of the name starting with it, the
// username starting with it (an @ allowed), or the phone number containing
// its digits (at least three). ASCII letters and Latin-1, Greek and
// Cyrillic ones compare without case.
bool ContactMatches(const Contact& contact, const std::string& query);

// The ids in a users or chats reply, in its order.
std::vector<int64_t> UserIds(const json& users);
std::vector<int64_t> ChatIds(const json& chats);
// The new chat's id from a chat or createdBasicGroupChat reply, 0 if none.
int64_t CreatedChatId(const json& reply);
// The people a new group couldn't take (createdBasicGroupChat or
// failedToAddMembers): privacy settings that don't allow it.
std::vector<int64_t> FailedMembers(const json& reply);

// What's wrong with a new group's or channel's title or description, ""
// if nothing: Telegram allows titles of 1 to 128 characters and
// descriptions of up to 255.
std::string ChatTitleProblem(const std::string& title);
std::string ChatDescriptionProblem(const std::string& description);

namespace request {

json GetContacts(const std::string& extra);
// Public chats and channels by name or username ("telegram" finds
// @telegram); TDLib wants at least a few letters.
json SearchPublicChats(const std::string& query, const std::string& extra);
// (The private chat with a user: CreatePrivateChat in Links.h.)
// Adds someone by phone number (in international format; a + is added);
// the reply's user_ids has 0 when nobody on Telegram has that number.
json ImportContact(const std::string& phone, const std::string& firstName,
	const std::string& lastName, const std::string& extra);
json RemoveContact(int64_t userId, const std::string& extra);
// A group (a basic group, which Telegram turns into a supergroup when it
// needs to) with these members; may be none.
json NewGroup(const std::string& title, const std::vector<int64_t>& members,
	const std::string& extra);
json NewChannel(const std::string& title, const std::string& description,
	const std::string& extra);
json AddMembers(int64_t chatId, const std::vector<int64_t>& members, const std::string& extra);

} // namespace request

} // namespace pp
