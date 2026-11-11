// SPDX-License-Identifier: GPL-3.0-or-later
// Throttling for every request to TDLib: at most a few in flight, a short
// gap between sends, and a full stop when Telegram says "Too Many Requests:
// retry after N" (FLOOD_WAIT) instead of retrying. TDLib already waits out
// most flood limits internally; this keeps the app from piling requests on.
#pragma once

#include <deque>
#include <set>
#include <string>
#include <vector>

#include "Json.h"

namespace pp {

class SendQueue {
public:
	explicit SendQueue(int maxInFlight = 4, double spacing = 0.03);

	// Queues a request. Urgent ones (login steps, close, log out) go first.
	// A request without "@extra" gets one, so its reply can be counted.
	void Push(json request, bool urgent = false);

	// Requests that may be sent at time `now` (seconds, any monotonic clock).
	std::vector<json> Take(double now);

	// Feed every object TDLib returns: replies free their slot; a flood
	// error pauses everything for as long as Telegram asks.
	void OnObject(const json& object, double now);

	void Clear();

	double PausedUntil() const { return fPausedUntil; }
	size_t Pending() const { return fQueue.size(); }
	int InFlight() const { return (int)fOutstanding.size(); }

	// Seconds to wait for a flood error, or -1 if it isn't one.
	static int RetryAfter(const json& error);

private:
	std::deque<json> fQueue;
	std::multiset<std::string> fOutstanding;
	int fMaxInFlight;
	double fSpacing;
	double fNextSend;
	double fPausedUntil;
	unsigned long long fCounter;
};

} // namespace pp
