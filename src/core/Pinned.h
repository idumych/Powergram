// SPDX-License-Identifier: GPL-3.0-or-later
// The open chat's pinned messages, for the bar over its history: the
// newest first; a click jumps to the one shown and moves on to the next.
#pragma once

#include <cstdint>
#include <vector>

#include "ChatHistory.h"
#include "Json.h"

namespace pp {

class PinnedMessages {
public:
	explicit PinnedMessages(int64_t chatId = 0) : fChatId(chatId) {}

	int64_t ChatId() const { return fChatId; }

	// The foundChatMessages reply to request::Pinned.
	void SetFound(const json& found);
	// Whether an update changes the list, which is then fetched again: a
	// message pinned or unpinned, or a pinned one edited or deleted.
	bool Affects(const json& update) const;

	const std::vector<Message>& List() const { return fList; }
	// The one the bar shows (nullptr: none), its place in the list, and
	// moving on to the next older one (after the oldest, the newest).
	const Message* Shown() const;
	int ShownIndex() const { return fShown; }
	void Next();

private:
	int64_t fChatId;
	std::vector<Message> fList;
	int fShown = 0;
};

namespace request {

// Replies carry "@extra" "pinned:<chat id>".
json Pinned(int64_t chatId);
json Pin(int64_t chatId, int64_t messageId, bool pin);

} // namespace request

} // namespace pp
