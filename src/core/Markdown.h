// SPDX-License-Identifier: GPL-3.0-or-later
// Formatting typed in the message box, read the way the official apps read
// it: **bold**, __italic__, ~~strikethrough~~, ||spoiler||, `code`,
// ```pre``` (a language may follow the opening ```) and [text](link). TDLib
// parses it (parseMarkdown) and, for editing, turns a message back into it
// (getMarkdownText); both can run synchronously (td_execute).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Json.h"

namespace pp {

// People mentioned by name (no username), as MentionEntities takes them:
// the name as it's written in the text, and the user.
using Mentions = std::vector<std::pair<std::string, int64_t>>;

// The formattedText parseMarkdown answered for `text`; if it answered
// with an error, the text as typed, with `entities`.
json ParsedText(const json& reply, const std::string& text, const json& entities);

// A message being edited, as the message box holds it: Markdown, and the
// mentions of people without a username (which Markdown can't write; they
// go back as entities when it's saved).
struct EditDraft {
	std::string text;
	Mentions mentions;
};
// From a formattedText: getMarkdownText's answer, or the message's own text
// if that failed.
EditDraft EditDraftFrom(const json& formattedText);

// A message content's formatted text: a text message's text or a photo's,
// video's, file's or voice message's caption; null if it has none.
const json& ContentText(const json& content);

// The byte offset in UTF-8 `text` of the UTF-16 offset `units` (TDLib's
// entity offsets), at most the text's size.
size_t Utf8Offset(const std::string& text, int32_t units);

namespace request {

json ParseMarkdown(const std::string& text, const json& entities);
json MarkdownText(const json& formattedText);
// A photo's, video's or file's caption, kept above or below the media as
// it is; text messages go with EditText.
json EditCaption(int64_t chatId, int64_t messageId, const json& caption, bool aboveMedia);

} // namespace request

} // namespace pp
