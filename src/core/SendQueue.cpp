// SPDX-License-Identifier: GPL-3.0-or-later
#include "SendQueue.h"

#include <algorithm>
#include <cstdlib>

namespace pp {

// Requests whose replies never count against the in-flight limit: TDLib
// answers them at once, and close must get through whatever happens.
static bool
IsLocal(const std::string& type)
{
	return type == "close" || type == "setOption" || type == "getOption"
		|| type == "setLogVerbosityLevel";
}


SendQueue::SendQueue(int maxInFlight, double spacing)
	:
	fMaxInFlight(maxInFlight),
	fSpacing(spacing),
	fNextSend(0),
	fPausedUntil(0),
	fCounter(0)
{
}


void
SendQueue::Push(json request, bool urgent)
{
	if (!Field(request, "@extra").is_string())
		request["@extra"] = "q" + std::to_string(++fCounter);
	if (urgent)
		fQueue.push_front(std::move(request));
	else
		fQueue.push_back(std::move(request));
}


std::vector<json>
SendQueue::Take(double now)
{
	std::vector<json> out;
	while (!fQueue.empty()) {
		const json& next = fQueue.front();
		std::string type = Type(next);
		bool local = IsLocal(type);
		// close and log out get through a flood pause; nothing else does.
		bool exempt = local || type == "logOut";
		if (!exempt) {
			if (now < fPausedUntil || now < fNextSend)
				break;
			if ((int)fOutstanding.size() >= fMaxInFlight)
				break;
		}
		if (!local)
			fOutstanding.insert(Str(next, "@extra"));
		out.push_back(next);
		fQueue.pop_front();
		if (!exempt) {
			fNextSend = now + fSpacing;
			if (fSpacing > 0)
				break;  // one paced request per call
		}
	}
	return out;
}


void
SendQueue::OnObject(const json& object, double now)
{
	const json& extra = Field(object, "@extra");
	if (!extra.is_string())
		return;  // an update, not a reply
	auto it = fOutstanding.find(extra.get<std::string>());
	if (it != fOutstanding.end())
		fOutstanding.erase(it);

	int wait = RetryAfter(object);
	if (wait > 0)
		fPausedUntil = std::max(fPausedUntil, now + wait);
}


void
SendQueue::Clear()
{
	fQueue.clear();
	fOutstanding.clear();
	fPausedUntil = 0;
	fNextSend = 0;
}


int
SendQueue::RetryAfter(const json& error)
{
	if (Type(error) != "error")
		return -1;
	std::string msg = Str(error, "message");
	// "Too Many Requests: retry after 9", or "FLOOD_WAIT_9" from older servers.
	const char* markers[] = {"retry after ", "FLOOD_WAIT_"};
	for (const char* marker : markers) {
		size_t at = msg.find(marker);
		if (at != std::string::npos)
			return std::max(1, std::atoi(msg.c_str() + at + std::string(marker).size()));
	}
	return Int(error, "code") == 429 ? 5 : -1;
}

} // namespace pp
