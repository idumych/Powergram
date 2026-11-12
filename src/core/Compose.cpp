// SPDX-License-Identifier: GPL-3.0-or-later
#include "Compose.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ChatHistory.h"
#include "ChatList.h"
#include "Profile.h"

namespace pp {

namespace {

std::string
Trimmed(const std::string& text)
{
	size_t start = text.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return std::string();
	size_t end = text.find_last_not_of(" \t\r\n");
	return text.substr(start, end - start + 1);
}


json
Formatted(const std::string& text)
{
	return {{"@type", "formattedText"}, {"text", text}, {"entities", json::array()}};
}


json
ReplyTo(int64_t replyTo)
{
	return {{"@type", "inputMessageReplyToMessage"}, {"message_id", replyTo}};
}


json
Send(int64_t chatId, const json& content, int64_t replyTo, const SendOptions& options)
{
	json request = {{"@type", "sendMessage"}, {"chat_id", chatId},
		{"input_message_content", content}};
	if (replyTo != 0)
		request["reply_to"] = ReplyTo(replyTo);
	ApplySendOptions(request, options);
	return request;
}


bool
ValidPlace(double latitude, double longitude)
{
	return std::isfinite(latitude) && std::isfinite(longitude) && latitude >= -90
		&& latitude <= 90 && longitude >= -180 && longitude <= 180
		&& !(latitude == 0 && longitude == 0);
}


// Two numbers separated by `separator` (and maybe spaces) at the start of
// `text`: "48.85,2.29", "48.85, 2.29", "48.85/2.29".
bool
ParsePair(const char* text, char separator, double* first, double* second)
{
	char* end;
	double a = strtod(text, &end);
	if (end == text)
		return false;
	while (*end == ' ')
		end++;
	if (*end != separator)
		return false;
	end++;
	while (*end == ' ')
		end++;
	const char* rest = end;
	double b = strtod(rest, &end);
	if (end == rest)
		return false;
	*first = a;
	*second = b;
	return true;
}


// The value of `key=` in a URL's query or fragment, "" if none.
std::string
QueryValue(const std::string& url, const std::string& key)
{
	for (size_t at = 0; (at = url.find(key + "=", at)) != std::string::npos; at++) {
		if (at > 0 && url[at - 1] != '?' && url[at - 1] != '&' && url[at - 1] != '#'
			&& url[at - 1] != ';')
			continue;
		size_t start = at + key.size() + 1;
		size_t end = url.find_first_of("&#;", start);
		std::string value = url.substr(start, end == std::string::npos ? std::string::npos
			: end - start);
		// %2C is a comma.
		for (size_t p; (p = value.find("%2C")) != std::string::npos
			|| (p = value.find("%2c")) != std::string::npos;)
			value.replace(p, 3, ",");
		return value;
	}
	return std::string();
}

} // namespace


void
ApplySendOptions(json& request, const SendOptions& options)
{
	if (!options.silent && !options.Scheduled())
		return;
	json send = {{"@type", "messageSendOptions"}, {"disable_notification", options.silent}};
	if (options.whenOnline)
		send["scheduling_state"] = {{"@type", "messageSchedulingStateSendWhenOnline"}};
	else if (options.sendAt != 0)
		send["scheduling_state"] = {{"@type", "messageSchedulingStateSendAtDate"},
			{"send_date", options.sendAt}, {"repeat_period", 0}};
	request["options"] = send;
}


bool
IsScheduled(const json& message)
{
	return Field(message, "scheduling_state").is_object();
}


std::string
ScheduleProblem(int64_t sendAt, int64_t now)
{
	if (sendAt <= now + 10)
		return "Choose a time in the future.";
	if (sendAt > now + 365 * 86400)
		return "Messages can be scheduled up to a year ahead.";
	return std::string();
}


int32_t
ParseClock(const std::string& input)
{
	// ICU writes "9:30\u202FPM" (a narrow no-break space); no-break spaces
	// count as spaces.
	std::string spaced = input;
	for (const char* space : {"\xE2\x80\xAF", "\xC2\xA0"}) {
		for (size_t p; (p = spaced.find(space)) != std::string::npos;)
			spaced.replace(p, strlen(space), " ");
	}
	std::string text;
	for (char c : Trimmed(spaced))
		text += (c >= 'A' && c <= 'Z') ? c + 32 : c;
	int hour = 0, minute = 0;
	size_t i = 0;
	auto digits = [&](int maxCount, int* value) {
		int count = 0;
		*value = 0;
		while (i < text.size() && text[i] >= '0' && text[i] <= '9' && count < maxCount) {
			*value = *value * 10 + (text[i++] - '0');
			count++;
		}
		return count;
	};
	if (digits(2, &hour) == 0)
		return -1;
	if (i < text.size() && (text[i] == ':' || text[i] == '.')) {
		i++;
		if (digits(2, &minute) != 2)
			return -1;
	}
	while (i < text.size() && text[i] == ' ')
		i++;
	std::string suffix = text.substr(i);
	if (suffix == "am" || suffix == "a.m." || suffix == "a") {
		if (hour < 1 || hour > 12)
			return -1;
		hour %= 12;
	} else if (suffix == "pm" || suffix == "p.m." || suffix == "p") {
		if (hour < 1 || hour > 12)
			return -1;
		hour = hour % 12 + 12;
	} else if (!suffix.empty())
		return -1;
	if (hour > 23 || minute > 59)
		return -1;
	return hour * 60 + minute;
}


namespace {

ScheduledMessage
ParseScheduled(const json& message)
{
	ScheduledMessage m;
	m.id = Int(message, "id");
	const json& state = Field(message, "scheduling_state");
	if (Type(state) == "messageSchedulingStateSendAtDate")
		m.sendAt = Int(state, "send_date");
	m.preview = MessagePreview(Field(message, "content"));
	return m;
}

} // namespace


void
ScheduledMessages::Set(const json& messages)
{
	fMessages.clear();
	const json& list = Field(messages, "messages");
	if (!list.is_array())
		return;
	for (const json& message : list) {
		if (Int(message, "chat_id") == fChatId && IsScheduled(message)) {
			ScheduledMessage m = ParseScheduled(message);
			fMessages[m.id] = m;
		}
	}
}


bool
ScheduledMessages::Apply(const json& object)
{
	std::string type = Type(object);
	if (type == "updateNewMessage" || type == "updateMessageSendSucceeded") {
		const json& message = Field(object, "message");
		if (Int(message, "chat_id") != fChatId)
			return false;
		bool changed = type == "updateMessageSendSucceeded"
			&& fMessages.erase(Int(object, "old_message_id")) > 0;
		if (!IsScheduled(message))
			return changed;
		ScheduledMessage m = ParseScheduled(message);
		fMessages[m.id] = m;
		return true;
	}
	if (Int(object, "chat_id") != fChatId)
		return false;
	if (type == "updateDeleteMessages") {
		bool changed = false;
		const json& ids = Field(object, "message_ids");
		if (ids.is_array()) {
			for (const json& id : ids)
				changed |= fMessages.erase(ToInt64(id)) > 0;
		}
		return changed;
	}
	if (type == "updateMessageContent") {
		auto it = fMessages.find(Int(object, "message_id"));
		if (it == fMessages.end())
			return false;
		it->second.preview = MessagePreview(Field(object, "new_content"));
		return true;
	}
	return false;
}


std::vector<ScheduledMessage>
ScheduledMessages::Sorted() const
{
	std::vector<ScheduledMessage> out;
	for (const auto& [id, message] : fMessages)
		out.push_back(message);
	std::stable_sort(out.begin(), out.end(), [](const ScheduledMessage& a,
			const ScheduledMessage& b) {
		if ((a.sendAt == 0) != (b.sendAt == 0))
			return b.sendAt == 0;
		return a.sendAt < b.sendAt;
	});
	return out;
}


std::vector<std::string>
PollOptions(const PollDraft& poll)
{
	std::vector<std::string> options;
	for (const std::string& option : poll.options) {
		std::string trimmed = Trimmed(option);
		if (!trimmed.empty())
			options.push_back(trimmed);
	}
	return options;
}


std::string
PollProblem(const PollDraft& poll, int32_t maxOptions, bool channel)
{
	std::string question = Trimmed(poll.question);
	if (question.empty())
		return "Enter a question.";
	if (CharacterCount(question) > 255)
		return "The question can be at most 255 characters long.";
	std::vector<std::string> options = PollOptions(poll);
	if (options.size() < 2)
		return "Enter at least two answers.";
	if ((int32_t)options.size() > maxOptions)
		return "A poll can have at most " + std::to_string(maxOptions) + " answers.";
	for (const std::string& option : options) {
		if (CharacterCount(option) > 100)
			return "Answers can be at most 100 characters long.";
	}
	for (size_t i = 0; i < options.size(); i++) {
		for (size_t j = i + 1; j < options.size(); j++) {
			if (options[i] == options[j])
				return "Each answer must be different.";
		}
	}
	if (channel && !poll.anonymous)
		return "Polls in channels are anonymous.";
	if (poll.quiz) {
		if (poll.correctOption < 0 || poll.correctOption >= (int32_t)options.size())
			return "Choose the right answer.";
		if (CharacterCount(Trimmed(poll.explanation)) > 200)
			return "The explanation can be at most 200 characters long.";
	}
	return std::string();
}


bool
ParseLocation(const std::string& input, double* latitude, double* longitude)
{
	std::string text = Trimmed(input);
	double lat = 0, lon = 0;
	bool found = false;
	auto tryPair = [&](const std::string& value, char separator) {
		if (!found && !value.empty() && ParsePair(value.c_str(), separator, &lat, &lon)
			&& ValidPlace(lat, lon))
			found = true;
	};
	// "geo:48.85,2.29" and "geo:0,0?q=48.85,2.29(Label)"
	if (text.compare(0, 4, "geo:") == 0) {
		tryPair(QueryValue(text, "q"), ',');
		tryPair(text.substr(4), ',');
	}
	if (text.find("://") != std::string::npos) {
		// Google: "/@48.85,2.29,17z", "?q=48.85,2.29", "?query=…", "?ll=…"
		size_t at = text.find("/@");
		if (at != std::string::npos)
			tryPair(text.substr(at + 2), ',');
		for (const char* key : {"q", "query", "ll", "sll", "daddr", "destination", "center"})
			tryPair(QueryValue(text, key), ',');
		// Bing: "cp=48.85~2.29"
		tryPair(QueryValue(text, "cp"), '~');
		// OpenStreetMap: "?mlat=48.85&mlon=2.29" or "#map=17/48.85/2.29"
		std::string mlat = QueryValue(text, "mlat"), mlon = QueryValue(text, "mlon");
		if (!found && !mlat.empty() && !mlon.empty())
			tryPair(mlat + "," + mlon, ',');
		std::string map = QueryValue(text, "map");
		size_t slash = map.find('/');
		if (slash != std::string::npos)
			tryPair(map.substr(slash + 1), '/');
	} else
		tryPair(text, ',');
	if (!found)
		return false;
	*latitude = lat;
	*longitude = lon;
	return true;
}


std::string
MapLink(double latitude, double longitude)
{
	char link[128];
	snprintf(link, sizeof(link),
		"https://www.openstreetmap.org/?mlat=%.6f&mlon=%.6f#map=16/%.6f/%.6f",
		latitude, longitude, latitude, longitude);
	return link;
}


namespace request {

json
SendTextWith(int64_t chatId, const std::string& text, int64_t replyTo, const json& entities,
	const SendOptions& options)
{
	json request = SendText(chatId, text, replyTo, entities);
	ApplySendOptions(request, options);
	return request;
}


json
SendPoll(int64_t chatId, const PollDraft& poll, int64_t replyTo, const SendOptions& options)
{
	json answers = json::array();
	for (const std::string& option : PollOptions(poll))
		answers.push_back({{"@type", "inputPollOption"}, {"text", Formatted(option)}});
	json type;
	if (poll.quiz) {
		type = {{"@type", "inputPollTypeQuiz"}, {"correct_option_ids", {poll.correctOption}},
			{"explanation", Formatted(Trimmed(poll.explanation))}};
	} else
		type = {{"@type", "inputPollTypeRegular"}, {"allow_adding_options", false}};
	json content = {{"@type", "inputMessagePoll"}, {"question", Formatted(Trimmed(poll.question))},
		{"options", answers}, {"is_anonymous", poll.anonymous},
		{"allows_multiple_answers", poll.multipleAnswers && !poll.quiz},
		{"allows_revoting", false}, {"members_only", false}, {"country_codes", json::array()},
		{"shuffle_options", false}, {"hide_results_until_closes", false}, {"type", type},
		{"open_period", 0}, {"close_date", 0}, {"is_closed", false}};
	json request = Send(chatId, content, replyTo, options);
	request["@extra"] = "send_poll";
	return request;
}


json
SendLocation(int64_t chatId, double latitude, double longitude, int64_t replyTo,
	const SendOptions& options)
{
	json content = {{"@type", "inputMessageLocation"}, {"location", {{"@type", "location"},
		{"latitude", latitude}, {"longitude", longitude}, {"horizontal_accuracy", 0}}}};
	json request = Send(chatId, content, replyTo, options);
	request["@extra"] = "send_location";
	return request;
}


json
ScheduledMessages(int64_t chatId, const std::string& extra)
{
	return {{"@type", "getChatScheduledMessages"}, {"chat_id", chatId}, {"@extra", extra}};
}


json
SendScheduledNow(int64_t chatId, int64_t messageId)
{
	return {{"@type", "editMessageSchedulingState"}, {"chat_id", chatId},
		{"message_id", messageId}, {"@extra", "reschedule"}};
}


json
Reschedule(int64_t chatId, int64_t messageId, int64_t sendAt)
{
	return {{"@type", "editMessageSchedulingState"}, {"chat_id", chatId},
		{"message_id", messageId}, {"scheduling_state", {{"@type",
			"messageSchedulingStateSendAtDate"}, {"send_date", sendAt}, {"repeat_period", 0}}},
		{"@extra", "reschedule"}};
}

} // namespace request

} // namespace pp
