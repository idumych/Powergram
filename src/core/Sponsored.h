// SPDX-License-Identifier: GPL-3.0-or-later
// Sponsored messages in channels (https://core.telegram.org/api/sponsored-messages),
// which every app using the Telegram API must show: fetched each time a
// channel opens, shown below its newest post as "Sponsored" (or
// "Recommended"), reported as viewed once all of its text was on screen and
// as clicked before its link opens, and reportable.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ChatHistory.h"
#include "Json.h"

namespace pp {

struct SponsoredMessage {
	Message message;           // id, text, formatting and media
	bool recommended = false;  // "Recommended" rather than "Sponsored"
	bool canReport = false;
	std::string title;         // shown where a post's sender is
	std::string buttonText;    // "" for none
	std::string url;           // what a click opens
	std::string sponsorInfo;   // who placed it, for "About this ad"
	std::string additionalInfo;

	const char* Label() const { return recommended ? "Recommended" : "Sponsored"; }
	// The text "About this ad" shows: the sponsor info and additional
	// info, each on its own paragraph; "" if neither is set.
	std::string AboutText() const;
};

// The sponsored messages of one channel.
class SponsoredMessages {
public:
	explicit SponsoredMessages(int64_t chatId = 0) : fChatId(chatId) {}

	int64_t ChatId() const { return fChatId; }

	// The sponsoredMessages reply to request::Sponsored.
	void Set(const json& sponsoredMessages);
	// The one shown below the newest post (nullptr: none).
	const SponsoredMessage* Shown() const;
	const SponsoredMessage* Find(int64_t messageId) const;
	// Takes one away after it was reported or ads were hidden.
	void Remove(int64_t messageId);
	// True the first time only: report the view then.
	bool MarkViewed(int64_t messageId);

private:
	int64_t fChatId;
	std::vector<SponsoredMessage> fList;
	std::vector<int64_t> fViewed;
};

SponsoredMessage ParseSponsoredMessage(const json& sponsoredMessage);

// Whether a sponsored message's link opens without asking first: tg:
// links, and https links to Telegram's own sites (telegram.org, t.me,
// telegra.ph, fragment.com and the others the spec names).
bool OpensWithoutConfirming(const std::string& url);

// What reportChatSponsoredMessage answered.
struct SponsoredReport {
	enum Kind { kReported, kChooseOption, kAdsHidden, kPremiumRequired, kFailed }
		kind = kFailed;
	std::string title;              // kChooseOption: the question
	struct Option {
		std::string id;             // as TDLib sent it (base64), to send back
		std::string text;
	};
	std::vector<Option> options;    // kChooseOption: the answers
};
SponsoredReport ParseSponsoredReport(const json& reportSponsoredResult);

namespace request {

// Replies carry "@extra" "sponsored:<chat id>".
json Sponsored(int64_t chatId);
json ClickSponsored(int64_t chatId, int64_t messageId, bool mediaClick);
// The first report goes with optionId ""; the answer may ask to choose
// one of its options, which goes back with the next. Replies carry "@extra"
// "sponsored_report:<chat id>:<message id>".
json ReportSponsored(int64_t chatId, int64_t messageId, const std::string& optionId);

} // namespace request

} // namespace pp
