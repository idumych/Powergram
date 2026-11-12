// SPDX-License-Identifier: GPL-3.0-or-later
#include "Contacts.h"

#include <algorithm>
#include <set>

#include "AuthFlow.h"
#include "Profile.h"

namespace pp {

namespace {

// The next character of UTF-8 text as a code point; moves `i` past it.
uint32_t
NextCodePoint(const std::string& text, size_t& i)
{
	unsigned char c = text[i++];
	if (c < 0x80)
		return c;
	int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
	uint32_t code = c & (0x3F >> more);
	for (; more > 0 && i < text.size() && ((unsigned char)text[i] & 0xC0) == 0x80; more--)
		code = (code << 6) | ((unsigned char)text[i++] & 0x3F);
	return code;
}


uint32_t
FoldCase(uint32_t c)
{
	if (c >= 'A' && c <= 'Z')
		return c + 32;
	if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)        // Latin-1
		|| (c >= 0x391 && c <= 0x3AB && c != 0x3A2)  // Greek
		|| (c >= 0x410 && c <= 0x42F))               // Cyrillic
		return c + 32;
	if (c >= 0x400 && c <= 0x40F)                    // Cyrillic Ѐ..Џ
		return c + 80;
	return c;
}


std::vector<uint32_t>
Folded(const std::string& text)
{
	std::vector<uint32_t> out;
	for (size_t i = 0; i < text.size();)
		out.push_back(FoldCase(NextCodePoint(text, i)));
	return out;
}


bool
StartsWith(const std::vector<uint32_t>& text, size_t at, const std::vector<uint32_t>& prefix)
{
	return text.size() - at >= prefix.size()
		&& std::equal(prefix.begin(), prefix.end(), text.begin() + at);
}


std::string
Digits(const std::string& text)
{
	std::string digits;
	for (char c : text) {
		if (c >= '0' && c <= '9')
			digits += c;
	}
	return digits;
}


std::string
Trimmed(const std::string& text)
{
	size_t start = text.find_first_not_of(" \t\n");
	if (start == std::string::npos)
		return std::string();
	size_t end = text.find_last_not_of(" \t\n");
	return text.substr(start, end - start + 1);
}


json
IdArray(const std::vector<int64_t>& ids)
{
	json array = json::array();
	for (int64_t id : ids)
		array.push_back(id);
	return array;
}

} // namespace


std::string
Contact::Name() const
{
	if (deleted)
		return "Deleted account";
	std::string name = firstName;
	if (!lastName.empty())
		name += (name.empty() ? "" : " ") + lastName;
	return name;
}


bool
Contacts::Apply(const json& object)
{
	std::string type = Type(object);
	const json* user = nullptr;
	if (type == "updateUser")
		user = &Field(object, "user");
	else if (type == "user")
		user = &object;
	else
		return false;
	Contact c;
	c.id = Int(*user, "id");
	if (c.id == 0)
		return false;
	c.firstName = Str(*user, "first_name");
	c.lastName = Str(*user, "last_name");
	const json& active = Field(Field(*user, "usernames"), "active_usernames");
	if (active.is_array() && !active.empty() && active[0].is_string())
		c.username = active[0].get<std::string>();
	c.phone = Str(*user, "phone_number");
	c.contact = Bool(*user, "is_contact");
	c.mutual = Bool(*user, "is_mutual_contact");
	std::string kind = Type(Field(*user, "type"));
	c.bot = kind == "userTypeBot";
	c.deleted = kind == "userTypeDeleted";
	auto it = fUsers.find(c.id);
	bool was = it != fUsers.end() && it->second.contact;
	fUsers[c.id] = c;
	return was || c.contact;
}


void
Contacts::SetContacts(const json& users)
{
	std::vector<int64_t> ids = UserIds(users);
	std::set<int64_t> contacts(ids.begin(), ids.end());
	for (auto& [id, user] : fUsers)
		user.contact = contacts.count(id) > 0;
	for (int64_t id : ids) {
		Contact& user = fUsers[id];
		user.id = id;
		user.contact = true;
	}
	fLoaded = true;
}


const Contact*
Contacts::Find(int64_t id) const
{
	auto it = fUsers.find(id);
	return it != fUsers.end() ? &it->second : nullptr;
}


std::vector<Contact>
Contacts::Matching(const std::string& query) const
{
	std::vector<Contact> found;
	for (const auto& [id, user] : fUsers) {
		if (user.contact && !user.deleted && ContactMatches(user, query))
			found.push_back(user);
	}
	std::stable_sort(found.begin(), found.end(), [](const Contact& a, const Contact& b) {
		return Folded(a.Name()) < Folded(b.Name());
	});
	return found;
}


bool
ContactMatches(const Contact& contact, const std::string& query)
{
	std::string q = Trimmed(query);
	if (q.empty())
		return true;
	std::string digits = Digits(q);
	if (digits.size() >= 3 && q.find_first_not_of("+0123456789 ()-.") == std::string::npos)
		return contact.phone.find(digits) != std::string::npos;
	if (q[0] == '@') {
		q.erase(0, 1);
		return !q.empty() && StartsWith(Folded(contact.username), 0, Folded(q));
	}
	std::vector<uint32_t> want = Folded(q);
	if (StartsWith(Folded(contact.username), 0, want))
		return true;
	// Any word of the name, and the whole name ("ann sm" finds Ann Smith).
	std::vector<uint32_t> name = Folded(contact.Name());
	for (size_t i = 0; i < name.size(); i++) {
		if ((i == 0 || name[i - 1] == ' ') && StartsWith(name, i, want))
			return true;
	}
	return false;
}



std::vector<int64_t>
UserIds(const json& users)
{
	std::vector<int64_t> ids;
	const json& list = Field(users, "user_ids");
	if (list.is_array()) {
		for (const json& id : list)
			ids.push_back(ToInt64(id));
	}
	return ids;
}


std::vector<int64_t>
ChatIds(const json& chats)
{
	std::vector<int64_t> ids;
	const json& list = Field(chats, "chat_ids");
	if (list.is_array()) {
		for (const json& id : list)
			ids.push_back(ToInt64(id));
	}
	return ids;
}


int64_t
CreatedChatId(const json& reply)
{
	std::string type = Type(reply);
	if (type == "chat")
		return Int(reply, "id");
	if (type == "createdBasicGroupChat")
		return Int(reply, "chat_id");
	return 0;
}


std::vector<int64_t>
FailedMembers(const json& reply)
{
	const json* failed = &reply;
	if (Type(reply) == "createdBasicGroupChat")
		failed = &Field(reply, "failed_to_add_members");
	std::vector<int64_t> ids;
	const json& list = Field(*failed, "failed_to_add_members");
	if (list.is_array()) {
		for (const json& member : list)
			ids.push_back(Int(member, "user_id"));
	}
	return ids;
}


std::string
ChatTitleProblem(const std::string& title)
{
	std::string trimmed = Trimmed(title);
	if (trimmed.empty())
		return "Enter a name.";
	if (CharacterCount(trimmed) > 128)
		return "The name can be at most 128 characters long.";
	return std::string();
}


std::string
ChatDescriptionProblem(const std::string& description)
{
	if (CharacterCount(Trimmed(description)) > 255)
		return "The description can be at most 255 characters long.";
	return std::string();
}


namespace request {

json
GetContacts(const std::string& extra)
{
	return {{"@type", "getContacts"}, {"@extra", extra}};
}


json
SearchPublicChats(const std::string& query, const std::string& extra)
{
	std::string q = Trimmed(query);
	if (!q.empty() && q[0] == '@')
		q.erase(0, 1);
	return {{"@type", "searchPublicChats"}, {"query", q}, {"@extra", extra}};
}



json
ImportContact(const std::string& phone, const std::string& firstName,
	const std::string& lastName, const std::string& extra)
{
	std::string number = NormalizePhone(phone);
	if (!number.empty() && number[0] != '+')
		number = "+" + number;
	json contact = {{"@type", "importedContact"}, {"phone_number", number},
		{"first_name", Trimmed(firstName)}, {"last_name", Trimmed(lastName)}};
	return {{"@type", "importContacts"}, {"contacts", json::array({contact})},
		{"@extra", extra}};
}


json
RemoveContact(int64_t userId, const std::string& extra)
{
	return {{"@type", "removeContacts"}, {"user_ids", json::array({userId})},
		{"@extra", extra}};
}


json
NewGroup(const std::string& title, const std::vector<int64_t>& members,
	const std::string& extra)
{
	return {{"@type", "createNewBasicGroupChat"}, {"user_ids", IdArray(members)},
		{"title", Trimmed(title)}, {"message_auto_delete_time", 0}, {"@extra", extra}};
}


json
NewChannel(const std::string& title, const std::string& description,
	const std::string& extra)
{
	return {{"@type", "createNewSupergroupChat"}, {"title", Trimmed(title)},
		{"is_forum", false}, {"is_channel", true}, {"description", Trimmed(description)},
		{"message_auto_delete_time", 0}, {"for_import", false}, {"@extra", extra}};
}


json
AddMembers(int64_t chatId, const std::vector<int64_t>& members, const std::string& extra)
{
	return {{"@type", "addChatMembers"}, {"chat_id", chatId}, {"user_ids", IdArray(members)},
		{"@extra", extra}};
}

} // namespace request

} // namespace pp
