// SPDX-License-Identifier: GPL-3.0-or-later
// Secret chats: end-to-end encrypted private chats, kept on this device
// only. TDLib does the encryption; this follows their state (waiting for
// the other side, ready, ended), the encryption key's picture both people
// compare, and the self-destruct timer.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Json.h"

namespace pp {

struct SecretChat {
	int32_t id = 0;
	int64_t userId = 0;
	enum State { kPending, kReady, kClosed } state = kPending;
	bool outbound = false;     // we started it
	std::string keyHash;       // the key's hash (36 bytes), once ready
};

class SecretChats {
public:
	// Takes updateSecretChat (and secretChat replies); true if it was one.
	bool Apply(const json& object);
	const SecretChat* Find(int32_t id) const;

private:
	std::map<int32_t, SecretChat> fChats;
};

// What's said in place of the entry while the chat can't be written in
// ("Waiting for Mira to come online…"); "" when it can.
std::string SecretChatNotice(const SecretChat& chat, const std::string& userName);

// The encryption key as Telegram's apps draw it: 12 by 12 squares, row by
// row, each one of four colours (0..3), two bits of the key hash each.
std::vector<uint8_t> KeyPicture(const std::string& keyHash);
// The four colours, as 0xRRGGBB.
extern const uint32_t kKeyPictureColors[4];

// Self-destruct timers offered (seconds; 0 is off), and their names
// ("Off", "5 seconds", "1 minute", …).
const std::vector<int32_t>& SelfDestructTimes();
std::string SelfDestructText(int32_t seconds);

namespace request {

// Starts a secret chat with a user; the answer is the new chat.
json NewSecretChat(int64_t userId, const std::string& extra);
json SetSelfDestruct(int64_t chatId, int32_t seconds);

} // namespace request

} // namespace pp
