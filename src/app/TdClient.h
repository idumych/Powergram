// SPDX-License-Identifier: GPL-3.0-or-later
// TDLib's JSON client on a thread of its own. Every object TDLib sends is
// posted to the target as a kMsgTdObject message with the JSON in "json".
//
// HaikuPorts' TDLib has no eventfd, so it runs its single-threaded client:
// td_send() and td_receive() must come from the same thread, or TDLib
// aborts ("Check `!scheduler_->has_guard_` failed"). Send() only queues;
// the TDLib thread makes every TDLib call, paced by a pp::SendQueue.
#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include <Messenger.h>
#include <OS.h>

#include "../core/SendQueue.h"

enum {
	kMsgTdObject = 'tdob'
};

class TdClient {
public:
	explicit TdClient(BMessenger target);
	~TdClient();

	status_t Start();
	// Starts a new TDLib instance (after the old one reached
	// authorizationStateClosed); objects from the old one are dropped.
	void NewInstance();
	// Queues a request; the TDLib thread sends it when the throttle allows.
	// Urgent: login steps, close and log out, ahead of everything else.
	void Send(const pp::json& request, bool urgent = false);

private:
	static status_t _Thread(void* data);
	void _Run();

	BMessenger fTarget;
	std::mutex fLock;
	pp::SendQueue fQueue;              // guarded by fLock
	bool fWantNewInstance;             // guarded by fLock
	std::atomic<bool> fQuit;
	thread_id fThread;
};
