// SPDX-License-Identifier: GPL-3.0-or-later
#include "Markdown.h"

#include <algorithm>

namespace pp {

json
ParsedText(const json& reply, const std::string& text, const json& entities)
{
	if (Type(reply) == "formattedText" && Field(reply, "text").is_string())
		return reply;
	json formatted = {{"@type", "formattedText"}, {"text", text}};
	formatted["entities"] = entities.is_array() ? entities : json::array();
	return formatted;
}


size_t
Utf8Offset(const std::string& text, int32_t units)
{
	size_t i = 0;
	for (int32_t u = 0; i < text.size() && u < units; ) {
		unsigned char c = (unsigned char)text[i];
		size_t length = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
		u += length == 4 ? 2 : 1;   // outside the BMP: a surrogate pair
		i += length;
	}
	return std::min(i, text.size());
}


EditDraft
EditDraftFrom(const json& formattedText)
{
	EditDraft draft;
	draft.text = Str(formattedText, "text");
	const json& entities = Field(formattedText, "entities");
	if (!entities.is_array())
		return draft;
	for (const json& entity : entities) {
		const json& type = Field(entity, "type");
		if (Type(type) != "textEntityTypeMentionName")
			continue;
		int32_t offset = (int32_t)Int(entity, "offset");
		size_t from = Utf8Offset(draft.text, offset);
		size_t to = Utf8Offset(draft.text, offset + (int32_t)Int(entity, "length"));
		if (to > from)
			draft.mentions.push_back({draft.text.substr(from, to - from), Int(type, "user_id")});
	}
	return draft;
}


const json&
ContentText(const json& content)
{
	static const json kNone;
	std::string type = Type(content);
	if (type == "messageText")
		return Field(content, "text");
	if (type == "messagePhoto" || type == "messageVideo" || type == "messageAnimation"
		|| type == "messageDocument" || type == "messageAudio" || type == "messageVoiceNote")
		return Field(content, "caption");
	return kNone;
}


namespace request {

json
ParseMarkdown(const std::string& text, const json& entities)
{
	json formatted = {{"@type", "formattedText"}, {"text", text}};
	formatted["entities"] = entities.is_array() ? entities : json::array();
	return {{"@type", "parseMarkdown"}, {"text", formatted}};
}


json
MarkdownText(const json& formattedText)
{
	return {{"@type", "getMarkdownText"}, {"text", formattedText}};
}


json
EditCaption(int64_t chatId, int64_t messageId, const json& caption, bool aboveMedia)
{
	return {{"@type", "editMessageCaption"}, {"chat_id", chatId},
		{"message_id", messageId}, {"caption", caption},
		{"show_caption_above_media", aboveMedia}, {"@extra", "edit"}};
}

} // namespace request

} // namespace pp
