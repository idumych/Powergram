// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChatHistory.h"

#include "ChatList.h"

#include <algorithm>

namespace pp {

std::string
DecodeBase64(const std::string& text)
{
	static const std::string chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	int bits = 0;
	unsigned int value = 0;
	for (char c : text) {
		size_t index = chars.find(c);
		if (c == '-' ) index = 62;  // URL-safe variants
		if (c == '_') index = 63;
		if (index == std::string::npos)
			continue;  // padding, whitespace
		value = (value << 6) | (unsigned int)index;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out += (char)((value >> bits) & 0xff);
		}
	}
	return out;
}


Media
PhotoMedia(const json& photo, int maxSide)
{
	Media media;
	media.kind = Media::kPhoto;
	const json& mini = Field(photo, "minithumbnail");
	media.minithumbnail = DecodeBase64(Str(mini, "data"));
	media.width = (int32_t)Int(mini, "width");
	media.height = (int32_t)Int(mini, "height");

	const json& sizes = Field(photo, "sizes");
	if (!sizes.is_array())
		return media;
	const json* best = nullptr;
	const json* largest = nullptr;
	auto side = [](const json& s) { return std::max(Int(s, "width"), Int(s, "height")); };
	for (const json& size : sizes) {
		if (!Field(size, "photo").is_object())
			continue;
		if (largest == nullptr || side(size) > side(*largest))
			largest = &size;
		if (side(size) >= maxSide && (best == nullptr || side(size) < side(*best)))
			best = &size;
	}
	if (best == nullptr)
		best = largest;
	if (best != nullptr) {
		media.fileId = (int32_t)Int(Field(*best, "photo"), "id");
		media.width = (int32_t)Int(*best, "width");
		media.height = (int32_t)Int(*best, "height");
	}
	if (largest != nullptr)
		media.fullFileId = (int32_t)Int(Field(*largest, "photo"), "id");
	return media;
}


Media
StickerMedia(const json& sticker)
{
	Media media;
	media.kind = Media::kSticker;
	media.emoji = Str(sticker, "emoji");
	media.width = (int32_t)Int(sticker, "width");
	media.height = (int32_t)Int(sticker, "height");
	// Haiku decodes WebP; Lottie (.tgs) and video (.webm) stickers show
	// their still thumbnail when it's an image format, else the emoji.
	if (Type(Field(sticker, "format")) == "stickerFormatWebp") {
		media.fileId = (int32_t)Int(Field(sticker, "sticker"), "id");
		return media;
	}
	const json& thumbnail = Field(sticker, "thumbnail");
	std::string format = Type(Field(thumbnail, "format"));
	if (format == "thumbnailFormatWebp" || format == "thumbnailFormatJpeg"
		|| format == "thumbnailFormatPng" || format == "thumbnailFormatGif") {
		media.fileId = (int32_t)Int(Field(thumbnail, "file"), "id");
		if (media.width == 0) {
			media.width = (int32_t)Int(thumbnail, "width");
			media.height = (int32_t)Int(thumbnail, "height");
		}
	}
	return media;
}


Media
VideoMedia(const json& video, Media::Kind kind, const char* fileField)
{
	Media media;
	media.kind = kind;
	const json& mini = Field(video, "minithumbnail");
	media.minithumbnail = DecodeBase64(Str(mini, "data"));
	media.duration = (int32_t)Int(video, "duration");
	media.width = (int32_t)Int(video, "width");
	media.height = (int32_t)Int(video, "height");
	if (kind == Media::kVideoNote)
		media.width = media.height = (int32_t)Int(video, "length");
	const json& thumbnail = Field(video, "thumbnail");
	std::string format = Type(Field(thumbnail, "format"));
	if (format == "thumbnailFormatJpeg" || format == "thumbnailFormatPng"
		|| format == "thumbnailFormatWebp" || format == "thumbnailFormatGif") {
		media.fileId = (int32_t)Int(Field(thumbnail, "file"), "id");
		if (media.width == 0) {
			media.width = (int32_t)Int(thumbnail, "width");
			media.height = (int32_t)Int(thumbnail, "height");
		}
	}
	if (media.width == 0) {
		media.width = (int32_t)Int(mini, "width");
		media.height = (int32_t)Int(mini, "height");
	}
	media.fullFileId = (int32_t)Int(Field(video, fileField), "id");
	return media;
}


// Formatting of the text MessageText returns for `content`.
static std::vector<Span>
ContentSpans(const json& content, const std::string& text)
{
	std::string type = Type(content);
	const json* formatted = nullptr;
	if (type == "messageText")
		formatted = &Field(content, "text");
	else if (type == "messagePhoto" || type == "messageVideo" || type == "messageAnimation")
		formatted = &Field(content, "caption");
	std::vector<Span> spans;
	if (formatted != nullptr && Str(*formatted, "text") == text)
		spans = ParseEntities(*formatted);
	// Without link entities, links typed in plain text still get found.
	bool hasLink = false;
	for (const Span& s : spans)
		hasLink |= (s.style & kLink) != 0;
	if (!hasLink) {
		std::vector<Span> links = FindLinks(text);
		spans.insert(spans.end(), links.begin(), links.end());
	}
	return spans;
}


void
ParseMedia(const json& content, Message& m)
{
	std::string type = Type(content);
	m.media = Media();
	if (type == "messagePhoto")
		m.media = PhotoMedia(Field(content, "photo"));
	else if (type == "messageSticker")
		m.media = StickerMedia(Field(content, "sticker"));
	else if (type == "messageVideo")
		m.media = VideoMedia(Field(content, "video"), Media::kVideo, "video");
	else if (type == "messageAnimation")
		m.media = VideoMedia(Field(content, "animation"), Media::kAnimation, "animation");
	else if (type == "messageVideoNote")
		m.media = VideoMedia(Field(content, "video_note"), Media::kVideoNote, "video");
	else
		return;
	m.media.spoiler = Bool(content, "has_spoiler");
	m.media.secret = Bool(content, "is_secret");
}


std::string
MessageText(const json& content)
{
	std::string type = Type(content);
	if (type == "messageText")
		return Str(Field(content, "text"), "text");
	// Shown as images: only the caption is text.
	if (type == "messagePhoto" || type == "messageVideo" || type == "messageAnimation")
		return Str(Field(content, "caption"), "text");
	if (type == "messageVideoNote")
		return std::string();
	if (type == "messageDocument") {
		std::string text = "Document: " + Str(Field(content, "document"), "file_name");
		std::string caption = Str(Field(content, "caption"), "text");
		return caption.empty() ? text : text + "\n" + caption;
	}
	if (type == "messageSticker")
		return std::string();
	std::string label = MessagePreview(content);
	// Captions are shown whole too.
	std::string caption = Str(Field(content, "caption"), "text");
	if (!caption.empty() && label.find(',') != std::string::npos)
		return label.substr(0, label.find(',')) + "\n" + caption;
	return label;
}


std::vector<Reaction>
ParseReactions(const json& interactionInfo)
{
	std::vector<Reaction> out;
	const json& list = Field(Field(interactionInfo, "reactions"), "reactions");
	if (!list.is_array())
		return out;
	for (const json& r : list) {
		const json& type = Field(r, "type");
		if (Type(type) != "reactionTypeEmoji")
			continue;
		out.push_back({Str(type, "emoji"), (int32_t)Int(r, "total_count"), Bool(r, "is_chosen")});
	}
	return out;
}


Message
ParseMessage(const json& message)
{
	Message m;
	m.id = Int(message, "id");
	const json& sender = Field(message, "sender_id");
	if (Type(sender) == "messageSenderChat") {
		m.senderIsChat = true;
		m.senderId = Int(sender, "chat_id");
	} else
		m.senderId = Int(sender, "user_id");
	const json& content = Field(message, "content");
	m.text = MessageText(content);
	m.spans = ContentSpans(content, m.text);
	m.date = Int(message, "date");
	m.reactions = ParseReactions(Field(message, "interaction_info"));
	m.outgoing = Bool(message, "is_outgoing");
	m.edited = Int(message, "edit_date") != 0;
	std::string state = Type(Field(message, "sending_state"));
	m.pending = state == "messageSendingStatePending";
	m.failed = state == "messageSendingStateFailed";
	std::string type = Type(content);
	m.service = type != "messageText" && type.find("messageChat") == 0;
	ParseMedia(content, m);

	const json& reply = Field(message, "reply_to");
	if (Type(reply) == "messageReplyToMessage") {
		m.replyToId = Int(reply, "message_id");
		m.replyChatId = Int(reply, "chat_id");
		m.replyQuote = Str(Field(Field(reply, "quote"), "text"), "text");
		const json& replyContent = Field(reply, "content");
		if (replyContent.is_object())
			m.replyText = MessageText(replyContent);
	}

	const json& forward = Field(message, "forward_info");
	if (forward.is_object()) {
		m.forwarded = true;
		const json& origin = Field(forward, "origin");
		std::string originType = Type(origin);
		if (originType == "messageOriginUser")
			m.forwardUserId = Int(origin, "sender_user_id");
		else if (originType == "messageOriginHiddenUser")
			m.forwardName = Str(origin, "sender_name");
		else if (originType == "messageOriginChat")
			m.forwardChatId = Int(origin, "sender_chat_id");
		else if (originType == "messageOriginChannel")
			m.forwardChatId = Int(origin, "chat_id");
	}
	return m;
}


bool
Names::Apply(const json& object)
{
	std::string type = Type(object);
	if (type == "updateUser") {
		const json& user = Field(object, "user");
		std::string name = Str(user, "first_name");
		std::string last = Str(user, "last_name");
		if (!last.empty())
			name += (name.empty() ? "" : " ") + last;
		if (name.empty())
			name = "Deleted account";
		fUsers[Int(user, "id")] = name;
		const json& photo = Field(user, "profile_photo");
		fUserPhotos[Int(user, "id")] = {(int32_t)Int(Field(photo, "small"), "id"),
			DecodeBase64(Str(Field(photo, "minithumbnail"), "data"))};
		return true;
	}
	if (type == "updateNewChat") {
		const json& chat = Field(object, "chat");
		fChats[Int(chat, "id")] = Str(chat, "title");
		const json& photo = Field(chat, "photo");
		fChatPhotos[Int(chat, "id")] = {(int32_t)Int(Field(photo, "small"), "id"),
			DecodeBase64(Str(Field(photo, "minithumbnail"), "data"))};
		return false;  // the chat list still wants it
	}
	if (type == "updateChatPhoto") {
		const json& photo = Field(object, "photo");
		fChatPhotos[Int(object, "chat_id")] = {(int32_t)Int(Field(photo, "small"), "id"),
			DecodeBase64(Str(Field(photo, "minithumbnail"), "data"))};
		return false;
	}
	if (type == "updateChatTitle") {
		fChats[Int(object, "chat_id")] = Str(object, "title");
		return false;
	}
	return false;
}


std::string
Names::ForwardOrigin(const Message& message) const
{
	if (!message.forwardName.empty())
		return message.forwardName;
	if (message.forwardUserId != 0)
		return User(message.forwardUserId);
	if (message.forwardChatId != 0)
		return Chat(message.forwardChatId);
	return std::string();
}


Photo
Names::UserPhoto(int64_t id) const
{
	auto it = fUserPhotos.find(id);
	return it == fUserPhotos.end() ? Photo() : it->second;
}


Photo
Names::ChatPhoto(int64_t id) const
{
	auto it = fChatPhotos.find(id);
	return it == fChatPhotos.end() ? Photo() : it->second;
}


Photo
Names::SenderPhoto(const Message& message) const
{
	return message.senderIsChat ? ChatPhoto(message.senderId) : UserPhoto(message.senderId);
}


std::string
Names::User(int64_t id) const
{
	auto it = fUsers.find(id);
	return it == fUsers.end() ? std::string() : it->second;
}


std::string
Names::Chat(int64_t id) const
{
	auto it = fChats.find(id);
	return it == fChats.end() ? std::string() : it->second;
}


std::string
Names::Sender(const Message& message) const
{
	return message.senderIsChat ? Chat(message.senderId) : User(message.senderId);
}


void
ChatHistory::_Add(const json& message)
{
	if (!message.is_object() || Int(message, "chat_id") != fChatId)
		return;
	Message m = ParseMessage(message);
	if (m.id != 0)
		fMessages[m.id] = m;
}


int
ChatHistory::AddHistory(const json& messages)
{
	const json& list = Field(messages, "messages");
	if (!list.is_array())
		return 0;
	size_t before = fMessages.size();
	// The array can hold nulls for messages that are gone.
	for (const json& message : list)
		_Add(message);
	return (int)(fMessages.size() - before);
}


bool
ChatHistory::Apply(const json& object)
{
	std::string type = Type(object);

	if (type == "updateNewMessage") {
		const json& message = Field(object, "message");
		if (Int(message, "chat_id") != fChatId || !fAtLatest)
			return false;
		_Add(message);
		return true;
	}

	if (type == "updateMessageSendSucceeded" || type == "updateMessageSendFailed") {
		// These carry the chat id only inside "message".
		const json& message = Field(object, "message");
		if (Int(message, "chat_id") != fChatId)
			return false;
		fMessages.erase(Int(object, "old_message_id"));
		_Add(message);
		return true;
	}

	if (Int(object, "chat_id") != fChatId)
		return false;

	if (type == "updateDeleteMessages") {
		// Also how self-destructing messages go when their timer runs out.
		// from_cache: only dropped from TDLib's cache, still on the server.
		if (Bool(object, "from_cache") && !Bool(object, "is_permanent"))
			return false;
		bool changed = false;
		const json& ids = Field(object, "message_ids");
		if (ids.is_array())
			for (const json& id : ids)
				changed |= fMessages.erase(ToInt64(id)) > 0;
		return changed;
	}
	if (type == "updateMessageContent") {
		auto it = fMessages.find(Int(object, "message_id"));
		if (it == fMessages.end())
			return false;
		const json& content = Field(object, "new_content");
		it->second.text = MessageText(content);
		it->second.spans = ContentSpans(content, it->second.text);
		ParseMedia(content, it->second);
		return true;
	}
	if (type == "updateMessageEdited") {
		auto it = fMessages.find(Int(object, "message_id"));
		if (it == fMessages.end())
			return false;
		it->second.edited = true;
		return true;
	}
	if (type == "updateMessageInteractionInfo") {
		auto it = fMessages.find(Int(object, "message_id"));
		if (it == fMessages.end())
			return false;
		it->second.reactions = ParseReactions(Field(object, "interaction_info"));
		return true;
	}
	if (type == "updateChatReadOutbox") {
		fLastReadOutbox = Int(object, "last_read_outbox_message_id");
		return true;
	}
	return false;
}


std::vector<Message>
ChatHistory::Sorted() const
{
	std::vector<Message> out;
	for (const auto& [id, message] : fMessages)
		out.push_back(message);
	return out;  // std::map keeps ids ascending: oldest first
}


const Message*
ChatHistory::Find(int64_t id) const
{
	auto it = fMessages.find(id);
	return it == fMessages.end() ? nullptr : &it->second;
}


std::vector<int64_t>
ChatHistory::TakeMissingReplies()
{
	std::vector<int64_t> out;
	for (const auto& [id, message] : fMessages) {
		int64_t target = message.replyToId;
		bool sameChat = message.replyChatId == 0 || message.replyChatId == fChatId;
		if (target == 0 || !sameChat || !message.replyText.empty())
			continue;
		if (fMessages.count(target) || fReplyTargets.count(target)
			|| fRequestedReplies.count(target))
			continue;
		fRequestedReplies.insert(target);
		out.push_back(target);
	}
	return out;
}


void
ChatHistory::AddReplyTarget(const json& message)
{
	if (Type(message) != "message" || Int(message, "chat_id") != fChatId)
		return;
	Message m = ParseMessage(message);
	fReplyTargets[m.id] = m;
}


void
ChatHistory::AddReplyTargets(const json& messages)
{
	const json& list = Field(messages, "messages");
	if (!list.is_array())
		return;
	for (const json& message : list)
		AddReplyTarget(message);
}


const Message*
ChatHistory::ReplyTarget(const Message& message) const
{
	if (message.replyToId == 0)
		return nullptr;
	if (const Message* m = Find(message.replyToId))
		return m;
	auto it = fReplyTargets.find(message.replyToId);
	return it == fReplyTargets.end() ? nullptr : &it->second;
}


int64_t
ChatHistory::NewestId() const
{
	return fMessages.empty() ? 0 : fMessages.rbegin()->first;
}


int64_t
ChatHistory::OldestId() const
{
	return fMessages.empty() ? 0 : fMessages.begin()->first;
}


std::vector<int64_t>
ChatHistory::TakeUnviewed()
{
	std::vector<int64_t> out;
	for (const auto& [id, message] : fMessages) {
		if (message.outgoing || message.pending || fViewed.count(id))
			continue;
		fViewed.insert(id);
		out.push_back(id);
	}
	return out;
}


namespace request {

json
OpenChat(int64_t chatId)
{
	return {{"@type", "openChat"}, {"chat_id", chatId}};
}


json
CloseChat(int64_t chatId)
{
	return {{"@type", "closeChat"}, {"chat_id", chatId}};
}


json
History(int64_t chatId, int64_t fromMessageId, int limit)
{
	return {{"@type", "getChatHistory"}, {"chat_id", chatId},
		{"from_message_id", fromMessageId}, {"offset", 0}, {"limit", limit},
		{"only_local", false}, {"@extra", "history:" + std::to_string(chatId)}};
}


json
HistoryAround(int64_t chatId, int64_t messageId, int before, int after)
{
	// TDLib: a negative offset includes that many newer messages; the limit
	// counts them too and must be larger.
	return {{"@type", "getChatHistory"}, {"chat_id", chatId},
		{"from_message_id", messageId}, {"offset", -after}, {"limit", before + after + 1},
		{"only_local", false}, {"@extra", "history:" + std::to_string(chatId)}};
}


json
HistoryNewer(int64_t chatId, int64_t messageId, int count)
{
	return HistoryAround(chatId, messageId, 0, count);
}


json
ViewMessages(int64_t chatId, const std::vector<int64_t>& ids)
{
	return {{"@type", "viewMessages"}, {"chat_id", chatId}, {"message_ids", ids},
		{"force_read", false}};
}


static json
TextContent(const std::string& text)
{
	// clear_draft: a sent message replaces the chat's draft, as in the
	// official apps.
	return {{"@type", "inputMessageText"},
		{"text", {{"@type", "formattedText"}, {"text", text}}}, {"clear_draft", true}};
}


json
SendText(int64_t chatId, const std::string& text, int64_t replyTo)
{
	json request = {{"@type", "sendMessage"}, {"chat_id", chatId},
		{"input_message_content", TextContent(text)}};
	if (replyTo != 0)
		request["reply_to"] = {{"@type", "inputMessageReplyToMessage"},
			{"message_id", replyTo}};
	return request;
}


json
EditText(int64_t chatId, int64_t messageId, const std::string& text)
{
	return {{"@type", "editMessageText"}, {"chat_id", chatId},
		{"message_id", messageId}, {"input_message_content", TextContent(text)},
		{"@extra", "edit"}};
}


json
Delete(int64_t chatId, const std::vector<int64_t>& ids, bool forEveryone)
{
	return {{"@type", "deleteMessages"}, {"chat_id", chatId},
		{"message_ids", ids}, {"revoke", forEveryone}, {"@extra", "delete"}};
}


json
GetMessage(int64_t chatId, int64_t messageId, const std::string& extra)
{
	return {{"@type", "getMessage"}, {"chat_id", chatId},
		{"message_id", messageId}, {"@extra", extra}};
}


json
GetMessages(int64_t chatId, const std::vector<int64_t>& ids, const std::string& extra)
{
	return {{"@type", "getMessages"}, {"chat_id", chatId}, {"message_ids", ids},
		{"@extra", extra}};
}


json
Forward(int64_t toChatId, int64_t fromChatId, const std::vector<int64_t>& ids)
{
	return {{"@type", "forwardMessages"}, {"chat_id", toChatId}, {"from_chat_id", fromChatId},
		{"message_ids", ids}, {"send_copy", false}, {"remove_caption", false},
		{"@extra", "forward"}};
}


json
React(int64_t chatId, int64_t messageId, const std::string& emoji, bool add)
{
	json request = {{"@type", add ? "addMessageReaction" : "removeMessageReaction"},
		{"chat_id", chatId}, {"message_id", messageId},
		{"reaction_type", {{"@type", "reactionTypeEmoji"}, {"emoji", emoji}}}};
	if (add) {
		request["is_big"] = false;
		request["update_recent_reactions"] = true;
	}
	return request;
}


json
OpenContent(int64_t chatId, int64_t messageId)
{
	return {{"@type", "openMessageContent"}, {"chat_id", chatId},
		{"message_id", messageId}};
}


static json
Caption(const std::string& text)
{
	return {{"@type", "formattedText"}, {"text", text}};
}


static void
AddReply(json& request, int64_t replyTo)
{
	if (replyTo != 0)
		request["reply_to"] = {{"@type", "inputMessageReplyToMessage"},
			{"message_id", replyTo}};
}


json
SendPhoto(int64_t chatId, const std::string& path, int32_t width, int32_t height,
	const std::string& caption, int64_t replyTo)
{
	json request = {{"@type", "sendMessage"}, {"chat_id", chatId},
		{"input_message_content", {{"@type", "inputMessagePhoto"},
			{"photo", {{"@type", "inputPhoto"},
				{"photo", {{"@type", "inputFileLocal"}, {"path", path}}},
				{"width", width}, {"height", height}}},
			{"caption", Caption(caption)}}}};
	AddReply(request, replyTo);
	return request;
}


json
SendDocument(int64_t chatId, const std::string& path, const std::string& caption,
	int64_t replyTo)
{
	json request = {{"@type", "sendMessage"}, {"chat_id", chatId},
		{"input_message_content", {{"@type", "inputMessageDocument"},
			{"document", {{"@type", "inputDocument"},
				{"document", {{"@type", "inputFileLocal"}, {"path", path}}}}},
			{"caption", Caption(caption)}}}};
	AddReply(request, replyTo);
	return request;
}


json
Properties(int64_t chatId, int64_t messageId, const std::string& extra)
{
	return {{"@type", "getMessageProperties"}, {"chat_id", chatId},
		{"message_id", messageId}, {"@extra", extra}};
}


json
Typing(int64_t chatId, bool typing)
{
	return {{"@type", "sendChatAction"}, {"chat_id", chatId},
		{"action", {{"@type", typing ? "chatActionTyping" : "chatActionCancel"}}}};
}


json
Online(bool online)
{
	return {{"@type", "setOption"}, {"name", "online"},
		{"value", {{"@type", "optionValueBoolean"}, {"value", online}}}};
}

} // namespace request

} // namespace pp
