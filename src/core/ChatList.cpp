// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChatList.h"

#include "ChatHistory.h"  // DecodeBase64

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace pp {

static std::string
FirstLine(const std::string& text)
{
	std::string out = text.substr(0, text.find('\n'));
	return out;
}


std::string
MessagePreview(const json& content)
{
	std::string type = Type(content);
	auto caption = [&](const char* label) {
		std::string text;
		text = Str(Field(content, "caption"), "text");
		return text.empty() ? std::string(label) : std::string(label) + ", " + FirstLine(text);
	};

	if (type == "messageText")
		return FirstLine(Str(Field(content, "text"), "text"));
	if (type == "messagePhoto")
		return caption("Photo");
	if (type == "messageVideo")
		return caption("Video");
	if (type == "messageAnimation")
		return caption("GIF");
	if (type == "messageDocument")
		return caption("Document");
	if (type == "messageAudio")
		return caption("Audio");
	if (type == "messageVoiceNote")
		return caption("Voice message");
	if (type == "messageVideoNote")
		return "Video message";
	if (type == "messageSticker") {
		std::string emoji = Str(Field(content, "sticker"), "emoji");
		return emoji.empty() ? "Sticker" : emoji + " Sticker";
	}
	if (type == "messageLocation" || type == "messageVenue")
		return "Location";
	if (type == "messageContact")
		return "Contact";
	if (type == "messagePoll")
		return "Poll";
	if (type == "messageCall")
		return "Call";
	if (type == "messageChatAddMembers")
		return "joined the group";
	if (type == "messageContactRegistered")
		return "joined Telegram";
	if (type == "messagePinMessage")
		return "pinned a message";
	if (type == "messageBasicGroupChatCreate" || type == "messageSupergroupChatCreate")
		return "created the group";
	return "Message";
}


std::string
Initials(const std::string& title)
{
	// Take the first UTF-8 character of the first two words.
	std::string out;
	bool atWordStart = true;
	int words = 0;
	for (size_t i = 0; i < title.size() && words < 2;) {
		unsigned char c = title[i];
		size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
		if (c == ' ') {
			atWordStart = true;
			i++;
			continue;
		}
		if (atWordStart) {
			std::string ch = title.substr(i, len);
			if (len == 1)
				ch[0] = std::toupper(c);
			out += ch;
			words++;
			atWordStart = false;
		}
		i += len;
	}
	return out;
}


void
ChatList::ApplyPositions(Chat& chat, const json& positions)
{
	if (!positions.is_array())
		return;
	for (const json& p : positions) {
		if (Type(Field(p, "list")) != "chatListMain")
			continue;
		chat.order = Int(p, "order");
		chat.pinned = Bool(p, "is_pinned");
	}
}


static void
ApplyNotificationSettings(Chat& chat, const json& settings)
{
	if (!settings.is_object())
		return;
	chat.useDefaultMute = Bool(settings, "use_default_mute_for", true);
	chat.muteFor = (int32_t)Int(settings, "mute_for");
	chat.useDefaultPreview = Bool(settings, "use_default_show_preview", true);
	chat.showPreview = Bool(settings, "show_preview", true);
}


static std::string
DraftText(const json& draft)
{
	const json& content = Field(draft, "content");
	if (Type(content) != "draftMessageContentText")
		return std::string();
	return Str(Field(content, "text"), "text");
}


static void
ApplyPhoto(Chat& chat, const json& photo)
{
	chat.photoFileId = (int32_t)Int(Field(photo, "small"), "id");
	chat.photoPreview = DecodeBase64(Str(Field(photo, "minithumbnail"), "data"));
}


static Chat::Kind
KindOf(const json& type)
{
	std::string t = Type(type);
	if (t == "chatTypeBasicGroup")
		return Chat::kGroup;
	if (t == "chatTypeSupergroup")
		return Bool(type, "is_channel") ? Chat::kChannel : Chat::kGroup;
	return Chat::kPrivate;  // private and secret chats
}


void
ChatList::ApplyLastMessage(Chat& chat, const json& message)
{
	if (!message.is_object()) {
		chat.lastText.clear();
		chat.lastSender.clear();
		chat.lastDate = 0;
		chat.outgoing = false;
		return;
	}
	chat.lastText = MessagePreview(Field(message, "content"));
	chat.lastDate = Int(message, "date");
	chat.outgoing = Bool(message, "is_outgoing");
	chat.lastSender = chat.outgoing ? "You" : "";
}


bool
ChatList::Apply(const json& object)
{
	std::string type = Type(object);

	if (type == "updateNewChat") {
		const json& c = Field(object, "chat");
		Chat chat;
		chat.id = Int(c, "id");
		if (chat.id == 0)
			return false;
		chat.title = Str(c, "title");
		chat.kind = KindOf(Field(c, "type"));
		chat.draft = DraftText(Field(c, "draft_message"));
		ApplyPhoto(chat, Field(c, "photo"));
		ApplyNotificationSettings(chat, Field(c, "notification_settings"));
		chat.unread = Int(c, "unread_count");
		chat.unreadMentions = Int(c, "unread_mention_count");
		ApplyPositions(chat, Field(c, "positions"));
		const json& last = Field(c, "last_message");
		ApplyLastMessage(chat, last);
		chat.lastReadOutbox = Int(c, "last_read_outbox_message_id");
		if (chat.outgoing)
			chat.outgoingRead = Int(last, "id") <= chat.lastReadOutbox;
		fChats[chat.id] = chat;
		return chat.order != 0;
	}

	if (type == "updateScopeNotificationSettings") {
		std::string scope = Type(Field(object, "scope"));
		int index = scope == "notificationSettingsScopeGroupChats" ? Chat::kGroup
			: scope == "notificationSettingsScopeChannelChats" ? Chat::kChannel
			: Chat::kPrivate;
		const json& settings = Field(object, "notification_settings");
		fScopes[index].muteFor = (int32_t)Int(settings, "mute_for");
		fScopes[index].showPreview = Bool(settings, "show_preview", true);
		return false;
	}

	auto it = fChats.find(Int(object, "chat_id"));
	if (it == fChats.end())
		return false;
	Chat& chat = it->second;
	bool wasVisible = chat.order != 0;

	if (type == "updateChatTitle") {
		chat.title = Str(object, "title");
	} else if (type == "updateChatPosition") {
		const json& position = Field(object, "position");
		if (Type(Field(position, "list")) != "chatListMain")
			return false;
		ApplyPositions(chat, json::array({position}));
	} else if (type == "updateChatLastMessage") {
		// No "last_message" field means the chat has none now.
		ApplyLastMessage(chat, Field(object, "last_message"));
		chat.outgoingRead = false;
		ApplyPositions(chat, Field(object, "positions"));
	} else if (type == "updateChatReadInbox") {
		chat.unread = Int(object, "unread_count");
	} else if (type == "updateChatReadOutbox") {
		chat.lastReadOutbox = Int(object, "last_read_outbox_message_id");
		chat.outgoingRead = chat.outgoing;
	} else if (type == "updateChatDraftMessage") {
		chat.draft = DraftText(Field(object, "draft_message"));
		ApplyPositions(chat, Field(object, "positions"));
	} else if (type == "updateChatPhoto") {
		ApplyPhoto(chat, Field(object, "photo"));
	} else if (type == "updateChatNotificationSettings") {
		ApplyNotificationSettings(chat, Field(object, "notification_settings"));
		return false;
	} else if (type == "updateChatUnreadMentionCount") {
		chat.unreadMentions = Int(object, "unread_mention_count");
	} else
		return false;

	return wasVisible || chat.order != 0;
}


std::vector<Chat>
ChatList::Sorted() const
{
	std::vector<Chat> out;
	for (const auto& [id, chat] : fChats)
		if (chat.order != 0)
			out.push_back(chat);
	// Telegram's order already puts pinned chats first; ties by id keep it stable.
	std::sort(out.begin(), out.end(), [](const Chat& a, const Chat& b) {
		return a.order != b.order ? a.order > b.order : a.id > b.id;
	});
	return out;
}


bool
ChatList::IsMuted(int64_t chatId) const
{
	const Chat* chat = Find(chatId);
	if (chat == nullptr)
		return false;
	int32_t muteFor = chat->useDefaultMute ? fScopes[chat->kind].muteFor : chat->muteFor;
	return muteFor > 0;
}


bool
ChatList::ShowsPreview(int64_t chatId) const
{
	const Chat* chat = Find(chatId);
	if (chat == nullptr)
		return true;
	return chat->useDefaultPreview ? fScopes[chat->kind].showPreview : chat->showPreview;
}


bool
ChatList::ShouldNotify(const json& message, int64_t visibleChat, int64_t now)
{
	int64_t chatId = Int(message, "chat_id");
	if (Bool(message, "is_outgoing") || chatId == 0 || chatId == visibleChat)
		return false;
	if (Find(chatId) == nullptr || IsMuted(chatId))
		return false;
	if (now - Int(message, "date") > 300)
		return false;
	int64_t& last = fLastNotified[chatId];
	if (now - last < 3)
		return false;
	last = now;
	return true;
}


const Chat*
ChatList::Find(int64_t id) const
{
	auto it = fChats.find(id);
	return it == fChats.end() ? nullptr : &it->second;
}


json
ChatList::DraftRequest(int64_t chatId, const std::string& text)
{
	json request = {{"@type", "setChatDraftMessage"}, {"chat_id", chatId}};
	if (!text.empty())
		request["draft_message"] = {{"@type", "draftMessage"},
			{"content", {{"@type", "draftMessageContentText"},
				{"text", {{"@type", "formattedText"}, {"text", text}}}}}};
	return request;
}


json
ChatList::LoadRequest(int limit)
{
	return {{"@type", "loadChats"}, {"chat_list", {{"@type", "chatListMain"}}},
		{"limit", limit}, {"@extra", "loadChats"}};
}

} // namespace pp
