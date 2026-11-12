// SPDX-License-Identifier: GPL-3.0-or-later
// More ways to send: without sound, at a set time or when the other person
// comes online (and the chat's scheduled messages, kept apart from its
// history); polls and quizzes; locations, typed or pasted as a map link.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Json.h"

namespace pp {

struct SendOptions {
	bool silent = false;       // no notification sound for the others
	int64_t sendAt = 0;        // scheduled for this time (unix), 0: now
	bool whenOnline = false;   // scheduled for when the other person is online

	bool Scheduled() const { return sendAt != 0 || whenOnline; }
};

// Adds `options` to a sendMessage or sendMessageAlbum request (nothing
// when they're the defaults).
void ApplySendOptions(json& request, const SendOptions& options);

// Whether a message (or update about one) is a scheduled one: those aren't
// part of the chat's history until they're sent.
bool IsScheduled(const json& message);

// What's wrong with sending at `sendAt`, "" if nothing: Telegram wants a
// time in the future, at most 365 days away.
std::string ScheduleProblem(int64_t sendAt, int64_t now);

// A time of day as typed: "21:30", "9:05", "9.05", "9 pm", "9:30pm",
// "12am"; minutes after midnight, or -1 if it isn't one.
int32_t ParseClock(const std::string& text);

// A chat's scheduled messages, as the Scheduled messages window lists them.
struct ScheduledMessage {
	int64_t id = 0;
	int64_t sendAt = 0;        // 0: when the other person comes online
	std::string preview;       // one line, as the chat list shows messages
};

class ScheduledMessages {
public:
	explicit ScheduledMessages(int64_t chatId = 0) : fChatId(chatId) {}
	int64_t ChatId() const { return fChatId; }

	// The getChatScheduledMessages reply: all of them.
	void Set(const json& messages);
	// updateNewMessage, updateMessageSendSucceeded, updateMessageContent,
	// updateDeleteMessages and updateMessageSchedulingState… for this chat;
	// true if the list changed.
	bool Apply(const json& object);
	// Soonest first; "when online" ones last.
	std::vector<ScheduledMessage> Sorted() const;

private:
	int64_t fChatId;
	std::map<int64_t, ScheduledMessage> fMessages;
};

// A poll or quiz to send.
struct PollDraft {
	std::string question;
	std::vector<std::string> options;   // empty ones are left out
	bool anonymous = true;
	bool multipleAnswers = false;
	bool quiz = false;
	int32_t correctOption = -1;         // quiz: index into the non-empty options
	std::string explanation;            // quiz: shown after a wrong answer
};

// The options that will be sent: trimmed, empty ones left out.
std::vector<std::string> PollOptions(const PollDraft& poll);
// What's wrong with a poll, "" if nothing. Telegram allows a question of
// 1-255 characters, 2 to `maxOptions` options of 1-100 characters, and a
// quiz explanation of up to 200; a quiz has one right answer and can't
// take several; channels only take anonymous polls.
std::string PollProblem(const PollDraft& poll, int32_t maxOptions, bool channel);

// A place from what was typed or pasted: "48.8584, 2.2945", "geo:" URIs,
// and links from OpenStreetMap, Google Maps, Apple Maps and Bing Maps.
// False if there's no valid latitude and longitude in it.
bool ParseLocation(const std::string& text, double* latitude, double* longitude);

// The OpenStreetMap link for a place, to look at it before sending.
std::string MapLink(double latitude, double longitude);

namespace request {

json SendTextWith(int64_t chatId, const std::string& text, int64_t replyTo,
	const json& entities, const SendOptions& options);
json SendPoll(int64_t chatId, const PollDraft& poll, int64_t replyTo,
	const SendOptions& options);
json SendLocation(int64_t chatId, double latitude, double longitude, int64_t replyTo,
	const SendOptions& options);
json ScheduledMessages(int64_t chatId, const std::string& extra);
// Sends a scheduled message now, or moves it to another time.
json SendScheduledNow(int64_t chatId, int64_t messageId);
json Reschedule(int64_t chatId, int64_t messageId, int64_t sendAt);

} // namespace request

} // namespace pp
