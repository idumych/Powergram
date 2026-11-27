// SPDX-License-Identifier: GPL-3.0-or-later
// People ignored in a chat, for a while or for good: their messages are
// hidden there and don't notify. Kept only on this computer.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace pp {

class IgnoreList {
public:
	struct Entry {
		int64_t chatId = 0;
		int64_t senderKey = 0;   // a user's id, or minus a chat's id
		int64_t until = 0;       // Unix time it ends; 0 for good
		std::string name;        // shown in the list of ignored people
	};

	// Ignores the sender in the chat until `until` (0: for good), replacing
	// an earlier time.
	void Ignore(int64_t chatId, int64_t senderKey, int64_t until, const std::string& name);
	bool Unignore(int64_t chatId, int64_t senderKey);

	bool IsIgnored(int64_t chatId, int64_t senderKey, int64_t now) const;
	// Who is ignored in the chat right now, in the order they were added.
	std::vector<Entry> In(int64_t chatId, int64_t now) const;
	// Everyone ignored anywhere right now, in the order they were added.
	std::vector<Entry> All(int64_t now) const;
	bool Empty() const { return fEntries.empty(); }

	// Forgets the times that have run out; true if any did.
	bool Prune(int64_t now);
	// The next time one runs out, or 0 if none will.
	int64_t NextExpiry() const;

	// One line per entry: "chat\tsender\tuntil\tname".
	std::string Encode() const;
	static IgnoreList Decode(const std::string& text);

private:
	std::map<std::pair<int64_t, int64_t>, Entry> fEntries;
	std::map<std::pair<int64_t, int64_t>, uint64_t> fOrder;
	uint64_t fNext = 0;
};

}  // namespace pp
