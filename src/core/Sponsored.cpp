// SPDX-License-Identifier: GPL-3.0-or-later
#include "Sponsored.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace pp {

std::string
SponsoredMessage::AboutText() const
{
	std::string text = sponsorInfo;
	if (!additionalInfo.empty())
		text += (text.empty() ? "" : "\n\n") + additionalInfo;
	return text;
}


SponsoredMessage
ParseSponsoredMessage(const json& sponsored)
{
	SponsoredMessage s;
	// A message of its own, without a sender, date or reply: ParseMessage
	// reads its text, formatting and media like any other.
	s.message = ParseMessage({{"@type", "message"}, {"id", Int(sponsored, "message_id")},
		{"content", Field(sponsored, "content")}});
	s.recommended = Bool(sponsored, "is_recommended");
	s.canReport = Bool(sponsored, "can_be_reported");
	s.title = Str(sponsored, "title");
	s.buttonText = Str(sponsored, "button_text");
	const json& sponsor = Field(sponsored, "sponsor");
	s.url = Str(sponsor, "url");
	s.sponsorInfo = Str(sponsor, "info");
	s.additionalInfo = Str(sponsored, "additional_info");
	return s;
}


void
SponsoredMessages::Set(const json& sponsoredMessages)
{
	fList.clear();
	const json& messages = Field(sponsoredMessages, "messages");
	if (messages.is_array()) {
		for (const json& m : messages)
			fList.push_back(ParseSponsoredMessage(m));
	}
}


const SponsoredMessage*
SponsoredMessages::Shown() const
{
	return fList.empty() ? nullptr : &fList.front();
}


const SponsoredMessage*
SponsoredMessages::Find(int64_t messageId) const
{
	for (const SponsoredMessage& s : fList)
		if (s.message.id == messageId)
			return &s;
	return nullptr;
}


void
SponsoredMessages::Remove(int64_t messageId)
{
	fList.erase(std::remove_if(fList.begin(), fList.end(),
		[&](const SponsoredMessage& s) { return s.message.id == messageId; }), fList.end());
}


bool
SponsoredMessages::MarkViewed(int64_t messageId)
{
	if (Find(messageId) == nullptr
		|| std::find(fViewed.begin(), fViewed.end(), messageId) != fViewed.end())
		return false;
	fViewed.push_back(messageId);
	return true;
}


bool
OpensWithoutConfirming(const std::string& url)
{
	std::string u = url;
	for (char& c : u)
		c = (char)std::tolower((unsigned char)c);
	if (u.rfind("tg:", 0) == 0)
		return true;
	size_t from;
	if (u.rfind("https://", 0) == 0)
		from = 8;
	else if (u.rfind("http://", 0) == 0)
		from = 7;
	else
		return false;
	std::string host = u.substr(from, u.find_first_of("/?#", from) - from);
	size_t at = host.rfind('@');
	if (at != std::string::npos)
		host = host.substr(at + 1);
	size_t colon = host.find(':');
	if (colon != std::string::npos)
		host = host.substr(0, colon);
	if (!host.empty() && host.back() == '.')
		host.pop_back();
	// The spec's list of Telegram's own domains, and their subdomains.
	static const std::regex kTelegram(
		"(^|\\.)(telegram\\.(org|me|dog)|t\\.me|te\\.?legra\\.ph|graph\\.org"
		"|fragment\\.com|telesco\\.pe)$");
	return std::regex_search(host, kTelegram);
}


SponsoredReport
ParseSponsoredReport(const json& result)
{
	SponsoredReport report;
	std::string type = Type(result);
	if (type == "reportSponsoredResultOk")
		report.kind = SponsoredReport::kReported;
	else if (type == "reportSponsoredResultAdsHidden")
		report.kind = SponsoredReport::kAdsHidden;
	else if (type == "reportSponsoredResultPremiumRequired")
		report.kind = SponsoredReport::kPremiumRequired;
	else if (type == "reportSponsoredResultOptionRequired") {
		report.kind = SponsoredReport::kChooseOption;
		report.title = Str(result, "title");
		const json& options = Field(result, "options");
		if (options.is_array()) {
			for (const json& o : options)
				report.options.push_back({Str(o, "id"), Str(o, "text")});
		}
		if (report.options.empty())
			report.kind = SponsoredReport::kFailed;
	}
	return report;
}


namespace request {

json
Sponsored(int64_t chatId)
{
	return {{"@type", "getChatSponsoredMessages"}, {"chat_id", chatId},
		{"@extra", "sponsored:" + std::to_string(chatId)}};
}


json
ClickSponsored(int64_t chatId, int64_t messageId, bool mediaClick)
{
	return {{"@type", "clickChatSponsoredMessage"}, {"chat_id", chatId},
		{"message_id", messageId}, {"is_media_click", mediaClick},
		{"from_fullscreen", false}, {"@extra", "sponsored_click"}};
}


json
ReportSponsored(int64_t chatId, int64_t messageId, const std::string& optionId)
{
	return {{"@type", "reportChatSponsoredMessage"}, {"chat_id", chatId},
		{"message_id", messageId}, {"option_id", optionId},
		{"@extra", "sponsored_report:" + std::to_string(chatId) + ":"
			+ std::to_string(messageId)}};
}

} // namespace request

} // namespace pp
