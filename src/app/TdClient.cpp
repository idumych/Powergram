// SPDX-License-Identifier: GPL-3.0-or-later
#include "TdClient.h"

#include <Message.h>

#include <cstdlib>

#include <td/telegram/td_json_client.h>

// How long td_receive waits before the thread looks at the outbox again.
static const double kReceiveTimeout = 0.05;


TdClient::TdClient(BMessenger target)
	:
	fTarget(target),
	fWantNewInstance(true),
	fQuit(false),
	fThread(-1)
{
}


TdClient::~TdClient()
{
	fQuit = true;
	if (fThread >= 0) {
		status_t result;
		wait_for_thread(fThread, &result);
	}
}


status_t
TdClient::Start()
{
	fThread = spawn_thread(_Thread, "tdlib", B_NORMAL_PRIORITY, this);
	if (fThread < 0)
		return fThread;
	return resume_thread(fThread);
}


void
TdClient::NewInstance()
{
	std::lock_guard<std::mutex> lock(fLock);
	fQueue.Clear();
	fWantNewInstance = true;
}


void
TdClient::Send(const pp::json& request, bool urgent)
{
	std::lock_guard<std::mutex> lock(fLock);
	fQueue.Push(request, urgent);
}


status_t
TdClient::_Thread(void* data)
{
	static_cast<TdClient*>(data)->_Run();
	return B_OK;
}


void
TdClient::_Run()
{
	// Fatal errors only: at level 1 TDLib on Haiku repeats "Error code value
	// is altered" for every errno, which are negative there.
	// POWERGRAM_TD_VERBOSITY=1…5 for debugging. Level 3 and up log every
	// request in full, the API hash and login codes included: keep those
	// logs private and delete them afterwards.
	const char* level = getenv("POWERGRAM_TD_VERBOSITY");
	pp::json verbosity = {{"@type", "setLogVerbosityLevel"},
		{"new_verbosity_level", level != nullptr ? atoi(level) : 0}};
	td_execute(verbosity.dump().c_str());

	int clientId = 0;
	while (!fQuit) {
		std::vector<pp::json> outbox;
		bool newInstance;
		double now = system_time() / 1e6;
		{
			std::lock_guard<std::mutex> lock(fLock);
			newInstance = fWantNewInstance;
			fWantNewInstance = false;
			outbox = fQueue.Take(now);
		}
		if (newInstance) {
			clientId = td_create_client_id();
			// An instance starts on its first request.
			td_send(clientId, "{\"@type\":\"getOption\",\"name\":\"version\"}");
		}
		for (const pp::json& request : outbox)
			td_send(clientId, request.dump().c_str());

		const char* text = td_receive(kReceiveTimeout);
		if (text == nullptr)
			continue;
		pp::json object = pp::json::parse(text, nullptr, false);
		if (object.is_discarded() || pp::Int(object, "@client_id") != clientId)
			continue;
		{
			std::lock_guard<std::mutex> lock(fLock);
			fQueue.OnObject(object, system_time() / 1e6);
		}
		BMessage message(kMsgTdObject);
		message.AddString("json", text);
		fTarget.SendMessage(&message);
	}
}
