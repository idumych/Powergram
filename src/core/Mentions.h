// SPDX-License-Identifier: GPL-3.0-or-later
// Mentioning people while typing: the "@query" being typed, the members
// that match it, what to put in its place, and the entities that make a
// mention of someone without a username a real one.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Json.h"

namespace pp {

// The "@word" just before the cursor (a byte offset), if the @ starts a
// word: true, with the word after the @ (maybe empty) and where the @ is.
bool MentionQuery(const std::string& text, size_t cursor, std::string* query, size_t* at);

// Replaces the "@query" at `at` (up to `cursor`) with "@username " or, for
// someone without one, "Name "; returns the new text and puts the cursor
// after it.
std::string InsertMention(const std::string& text, size_t at, size_t cursor,
	const std::string& name, const std::string& username, size_t* newCursor);

// textEntityTypeMentionName entities for each (name, user id) still in the
// text, in UTF-16 offsets as TDLib wants; each name's first occurrence.
json MentionEntities(const std::string& text,
	const std::vector<std::pair<std::string, int64_t>>& mentions);

// UTF-16 units in the first `bytes` bytes of UTF-8 text.
int32_t Utf8ToUtf16(const std::string& text, size_t bytes);

// The user ids in a chatMembers reply, in its order.
std::vector<int64_t> MemberUserIds(const json& chatMembers);

// The first name of "Ann Smith", for inserting.
std::string FirstName(const std::string& name);

namespace request {

// Members whose name or username starts with `query` ("" for some of them).
json SearchMembers(int64_t chatId, const std::string& query, const std::string& extra);
// The unread mentions in a chat (newest first), to go to the oldest.
json UnreadMentions(int64_t chatId, const std::string& extra);

} // namespace request

} // namespace pp
