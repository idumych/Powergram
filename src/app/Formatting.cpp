// SPDX-License-Identifier: GPL-3.0-or-later
#include "Formatting.h"

#include <td/telegram/td_json_client.h>

#include "../core/ChatHistory.h"
#include "../core/Compose.h"
#include "../core/Mentions.h"
#include "Messages.h"


// A synchronous TDLib function (parseMarkdown, getMarkdownText); null if
// TDLib gave no answer.
static pp::json
Execute(const pp::json& request)
{
	const char* answer = td_execute(request.dump().c_str());
	if (answer == nullptr)
		return pp::json();
	pp::json object = pp::json::parse(answer, nullptr, false);
	return object.is_discarded() ? pp::json() : object;
}


pp::json
TypedText(const std::string& text, const pp::Mentions& mentions)
{
	// The mentions as entities of the typed text; TDLib moves them along
	// as it takes the Markdown out.
	pp::json entities = pp::MentionEntities(text, mentions);
	return pp::ParsedText(Execute(pp::request::ParseMarkdown(text, entities)), text, entities);
}


pp::Mentions
MessageMentions(const BMessage* message)
{
	pp::Mentions mentions;
	const char* name;
	for (int32 i = 0; message->FindString("mention_name", i, &name) == B_OK; i++)
		mentions.push_back({name, message->GetInt64("mention_user", i, 0)});
	return mentions;
}


pp::json
OutgoingText(const BMessage* sendText)
{
	pp::json text = TypedText(sendText->GetString("text", ""), MessageMentions(sendText));
	pp::SendOptions options;
	options.silent = sendText->GetBool("silent", false);
	options.sendAt = sendText->GetInt64("send_at", 0);
	options.whenOnline = sendText->GetBool("when_online", false);
	pp::json send = pp::request::SendTextWith(sendText->GetInt64("chat_id", 0),
		pp::Str(text, "text"), sendText->GetInt64("reply_to", 0), pp::Field(text, "entities"),
		options);
	if (options.Scheduled())
		send["@extra"] = "send_scheduled";
	return send;
}


pp::json
OutgoingEdit(const BMessage* editText, bool media, bool captionAbove)
{
	pp::json text = TypedText(editText->GetString("text", ""), MessageMentions(editText));
	int64 chatId = editText->GetInt64("chat_id", 0);
	int64 messageId = editText->GetInt64("message_id", 0);
	if (media)
		return pp::request::EditCaption(chatId, messageId, text, captionAbove);
	return pp::request::EditText(chatId, messageId, pp::Str(text, "text"),
		pp::Field(text, "entities"));
}


BMessage
EditDraftMessage(const pp::json& message)
{
	const pp::json& text = pp::ContentText(pp::Field(message, "content"));
	pp::json markdown = Execute(pp::request::MarkdownText(text));
	pp::EditDraft draft = pp::EditDraftFrom(pp::Type(markdown) == "formattedText"
		? markdown : text);
	BMessage reply(kMsgEditDraft);
	reply.AddInt64("chat_id", pp::Int(message, "chat_id"));
	reply.AddInt64("message_id", pp::Int(message, "id"));
	reply.AddString("text", draft.text.c_str());
	for (const auto& mention : draft.mentions) {
		reply.AddString("mention_name", mention.first.c_str());
		reply.AddInt64("mention_user", mention.second);
	}
	return reply;
}
