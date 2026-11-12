// SPDX-License-Identifier: GPL-3.0-or-later
#include "SecretChat.h"

#include "ChatHistory.h"

namespace pp {

bool
SecretChats::Apply(const json& object)
{
	std::string type = Type(object);
	const json* chat = nullptr;
	if (type == "updateSecretChat")
		chat = &Field(object, "secret_chat");
	else if (type == "secretChat")
		chat = &object;
	else
		return false;
	SecretChat s;
	s.id = (int32_t)Int(*chat, "id");
	if (s.id == 0)
		return false;
	s.userId = Int(*chat, "user_id");
	std::string state = Type(Field(*chat, "state"));
	s.state = state == "secretChatStateReady" ? SecretChat::kReady
		: state == "secretChatStateClosed" ? SecretChat::kClosed : SecretChat::kPending;
	s.outbound = Bool(*chat, "is_outbound");
	s.keyHash = DecodeBase64(Str(*chat, "key_hash"));
	fChats[s.id] = s;
	return true;
}


const SecretChat*
SecretChats::Find(int32_t id) const
{
	auto it = fChats.find(id);
	return it != fChats.end() ? &it->second : nullptr;
}


std::string
SecretChatNotice(const SecretChat& chat, const std::string& userName)
{
	std::string name = userName.empty() ? "the other person" : userName;
	switch (chat.state) {
		case SecretChat::kReady:
			return "";
		case SecretChat::kClosed:
			return "This secret chat has ended.";
		case SecretChat::kPending:
			break;
	}
	if (chat.outbound)
		return "Waiting for " + name + " to come online" "\xE2\x80\xA6";
	return "Starting the secret chat" "\xE2\x80\xA6";
}


const uint32_t kKeyPictureColors[4] = {0xffffff, 0xd5e6f3, 0x2d5775, 0x2f99c9};


std::vector<uint8_t>
KeyPicture(const std::string& keyHash)
{
	// As Telegram's identicon: two bits a square, low bits first.
	std::vector<uint8_t> squares;
	if (keyHash.size() < 36)
		return squares;
	squares.resize(144);
	for (int i = 0; i < 144; i++) {
		int bit = i * 2;
		squares[i] = ((uint8_t)keyHash[bit / 8] >> (bit % 8)) & 3;
	}
	return squares;
}


const std::vector<int32_t>&
SelfDestructTimes()
{
	static const std::vector<int32_t> times = {0, 1, 2, 3, 4, 5, 10, 15, 30, 60, 3600,
		86400, 604800};
	return times;
}


std::string
SelfDestructText(int32_t seconds)
{
	if (seconds <= 0)
		return "Off";
	struct { int32_t size; const char* one; const char* many; } units[] = {
		{604800, "week", "weeks"}, {86400, "day", "days"}, {3600, "hour", "hours"},
		{60, "minute", "minutes"}, {1, "second", "seconds"}};
	for (const auto& u : units) {
		if (seconds % u.size == 0) {
			int32_t n = seconds / u.size;
			return std::to_string(n) + " " + (n == 1 ? u.one : u.many);
		}
	}
	return std::to_string(seconds) + " seconds";
}


namespace request {

json
NewSecretChat(int64_t userId, const std::string& extra)
{
	return {{"@type", "createNewSecretChat"}, {"user_id", userId}, {"@extra", extra}};
}


json
SetSelfDestruct(int64_t chatId, int32_t seconds)
{
	return {{"@type", "setChatMessageAutoDeleteTime"}, {"chat_id", chatId},
		{"message_auto_delete_time", seconds}};
}

} // namespace request

} // namespace pp
