// SPDX-License-Identifier: GPL-3.0-or-later
// The open chat's messages, kept from TDLib history replies and updates,
// and the requests a well-behaved client sends around it.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Format.h"
#include "Json.h"

namespace pp {

// A photo or sticker in a message, as far as showing it goes.
struct Media {
	enum Kind { kNone, kPhoto, kSticker, kVideo, kAnimation, kVideoNote, kVoice } kind = kNone;
	int32_t fileId = 0;        // what to show in the bubble (0: nothing to fetch)
	int32_t fullFileId = 0;    // the largest photo, for opening in a viewer
	int32_t width = 0;         // size of what's shown, in pixels
	int32_t height = 0;
	std::string minithumbnail; // tiny inline JPEG (decoded), shown blurred first
	bool spoiler = false;      // hidden until clicked
	bool secret = false;       // self-destructing: hidden until opened
	std::string emoji;         // stickers: shown when there's no image
	bool bigEmoji = false;     // a message of one emoji, drawn large
	int32_t duration = 0;      // videos and voice messages, seconds
	std::string waveform;      // voice messages: one 0..31 level per byte
};

// Picks the photo size to show in a bubble about `maxSide` pixels wide:
// the smallest that is at least that big, or else the largest.
Media PhotoMedia(const json& photo, int maxSide = 480);
Media StickerMedia(const json& sticker);
// Videos, GIFs and round video messages: their still thumbnail in the
// bubble, the file itself to open in a player.
Media VideoMedia(const json& video, Media::Kind kind, const char* fileField);

// Voice messages: the file to play, its length and its waveform.
Media VoiceMedia(const json& voiceNote);
// Telegram packs a voice message's waveform as 5-bit levels, low bits
// first; this gives one level (0..31) per byte.
std::string UnpackWaveform(const std::string& packed);

std::string DecodeBase64(const std::string& text);

struct Reaction {
	std::string emoji;
	int32_t count = 0;
	bool chosen = false;   // ours
};

// The emoji reactions in an interaction_info (custom and paid ones skipped).
std::vector<Reaction> ParseReactions(const json& interactionInfo);

struct Message {
	int64_t id = 0;
	int64_t senderId = 0;      // user id, or chat id when senderIsChat
	bool senderIsChat = false;
	std::string text;          // full text, or a label such as "Photo"
	int64_t date = 0;
	bool outgoing = false;
	bool edited = false;
	bool pending = false;      // still being sent
	bool failed = false;
	bool service = false;      // "joined the group" and the like

	// Reply: the message answered, in this chat or (with replyText filled
	// from TDLib's copy) another one.
	int64_t replyToId = 0;
	int64_t replyChatId = 0;
	std::string replyQuote;    // the quoted part, if the sender picked one
	std::string replyText;     // TDLib's copy of a message from another chat

	// Forwarded: from a user, a chat, or someone who hides their account.
	bool forwarded = false;
	int64_t forwardUserId = 0;
	int64_t forwardChatId = 0;
	std::string forwardName;

	Media media;
	std::vector<Span> spans;   // formatting and links in `text`
	std::vector<Reaction> reactions;
};

// A profile picture: the small (160 px) file and its tiny inline JPEG.
struct Photo {
	int32_t fileId = 0;
	std::string preview;
};

// Names and profile pictures of users and chats, for message senders.
class Names {
public:
	// Takes updateUser and updateNewChat/updateChatTitle; true if it was one.
	bool Apply(const json& object);
	std::string User(int64_t id) const;
	std::string Chat(int64_t id) const;
	std::string Sender(const Message& message) const;
	Photo SenderPhoto(const Message& message) const;
	Photo UserPhoto(int64_t id) const;
	Photo ChatPhoto(int64_t id) const;
	// "Forwarded from …" name, or "" if unknown.
	std::string ForwardOrigin(const Message& message) const;

private:
	std::map<int64_t, std::string> fUsers;
	std::map<int64_t, std::string> fChats;
	std::map<int64_t, Photo> fUserPhotos;
	std::map<int64_t, Photo> fChatPhotos;
};

class ChatHistory {
public:
	explicit ChatHistory(int64_t chatId = 0) : fChatId(chatId) {}

	int64_t ChatId() const { return fChatId; }

	// Updates about this chat's messages; true if what's shown changed.
	bool Apply(const json& object);
	// A "messages" reply to getChatHistory; returns how many were new.
	int AddHistory(const json& messages);

	std::vector<Message> Sorted() const;
	const Message* Find(int64_t id) const;

	// Reply targets in this chat that aren't loaded: fetch each once with
	// getMessage and hand the answer to AddReplyTarget.
	std::vector<int64_t> TakeMissingReplies();
	void AddReplyTarget(const json& message);
	// A "messages" reply to getMessages for reply targets (nulls allowed).
	void AddReplyTargets(const json& messages);
	// The replied-to message for `message`, from the history or the
	// fetched targets; nullptr if not known (yet).
	const Message* ReplyTarget(const Message& message) const;
	int64_t OldestId() const;
	int64_t NewestId() const;

	// Whether the loaded history reaches the chat's newest message. While
	// it doesn't (opened at the first unread one), new messages aren't
	// added: they'd leave a gap; they load when scrolling down.
	bool AtLatest() const { return fAtLatest; }
	void SetAtLatest(bool atLatest) { fAtLatest = atLatest; }
	size_t Size() const { return fMessages.size(); }

	// Incoming messages not yet reported with viewMessages; marks them.
	std::vector<int64_t> TakeUnviewed();

	int64_t LastReadOutbox() const { return fLastReadOutbox; }
	void SetLastReadOutbox(int64_t id) { fLastReadOutbox = id; }

private:
	void _Add(const json& message);

	int64_t fChatId;
	std::map<int64_t, Message> fMessages;
	std::set<int64_t> fViewed;
	std::map<int64_t, Message> fReplyTargets;
	std::set<int64_t> fRequestedReplies;
	int64_t fLastReadOutbox = 0;
	bool fAtLatest = true;
};

// Text for a message as shown in the chat: the whole text of a text
// message, or MessagePreview's label for anything else.
std::string MessageText(const json& content);
// Parses a message's content into `message.media` (or clears it).
void ParseMedia(const json& content, Message& message);

Message ParseMessage(const json& message);

// The message ids in a foundChatMessages (or messages) reply, in its order.
std::vector<int64_t> FoundIds(const json& found);

namespace request {

json OpenChat(int64_t chatId);
json CloseChat(int64_t chatId);
// The page of history before `fromMessageId` (0: the newest).
json History(int64_t chatId, int64_t fromMessageId, int limit);
// `before` messages up to `messageId` and `after` newer ones.
json HistoryAround(int64_t chatId, int64_t messageId, int before, int after);
// Up to `count` messages newer than `messageId`.
json HistoryNewer(int64_t chatId, int64_t messageId, int count);
json ViewMessages(int64_t chatId, const std::vector<int64_t>& ids);
json SendText(int64_t chatId, const std::string& text, int64_t replyTo = 0);
json EditText(int64_t chatId, int64_t messageId, const std::string& text);
json Delete(int64_t chatId, const std::vector<int64_t>& ids, bool forEveryone);
json GetMessage(int64_t chatId, int64_t messageId, const std::string& extra);
json GetMessages(int64_t chatId, const std::vector<int64_t>& ids, const std::string& extra);
json Properties(int64_t chatId, int64_t messageId, const std::string& extra);
// Tells Telegram a message's content was opened: starts the timer of a
// self-destructing photo, as the official apps do.
json OpenContent(int64_t chatId, int64_t messageId);
json Forward(int64_t toChatId, int64_t fromChatId, const std::vector<int64_t>& ids);
// Messages in a chat matching `query` (Telegram searches, case-insensitively),
// newest first.
json SearchChat(int64_t chatId, const std::string& query, int limit);
json React(int64_t chatId, int64_t messageId, const std::string& emoji, bool add);
// Sends a local file: a photo (with its size) or any file as a document.
json SendPhoto(int64_t chatId, const std::string& path, int32_t width, int32_t height,
	const std::string& caption, int64_t replyTo = 0);
json SendDocument(int64_t chatId, const std::string& path, const std::string& caption,
	int64_t replyTo = 0);
json Typing(int64_t chatId, bool typing);
json Online(bool online);

} // namespace request

// Rate limit for typing notifications: true when one should be sent now.
class TypingThrottle {
public:
	bool ShouldSend(double now)
	{
		if (now - fLast < 5.0)
			return false;
		fLast = now;
		return true;
	}
	void Reset() { fLast = -1e9; }

private:
	double fLast = -1e9;
};

} // namespace pp
