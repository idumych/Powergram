// SPDX-License-Identifier: GPL-3.0-or-later
// Login state machine over TDLib's JSON interface. No Haiku headers: the
// app feeds it TDLib objects and sends the requests it returns.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Json.h"

namespace pp {

struct TdParams {
	int32_t apiId = 0;
	std::string apiHash;
	std::string databaseDir;
	bool testDc = false;
	std::string deviceModel = "Computer";
	std::string systemVersion;
	std::string appVersion = "0.1";
	std::string language = "en";

	bool HasApiKey() const { return apiId > 0 && !apiHash.empty(); }
};

enum class AuthStep {
	Starting,      // waiting for TDLib
	NeedApiKey,    // TDLib wants parameters but we have no api_id/api_hash
	Phone,
	Code,
	Password,
	Registration,  // new account: first and last name
	OtherDevice,   // QR code login; link in State::link
	Email,         // not supported by the proof of concept
	Ready,
	LoggingOut,
	Closing,
	Closed,
	Unsupported
};

struct AuthState {
	AuthStep step = AuthStep::Starting;
	std::string codeInfo;      // where the code was sent ("Telegram app", "SMS")
	int codeLength = 0;
	std::string passwordHint;
	std::string link;          // tg://login?token=… for OtherDevice
	std::string tdType;        // the TDLib @type, for messages about Unsupported
};

// Tag on every request this class makes, so the app can route the answer
// (an "error" or "ok" object carries it back in "@extra").
extern const char* const kAuthExtra;

class AuthFlow {
public:
	explicit AuthFlow(TdParams params) : fParams(std::move(params)) {}

	// Feed any TDLib object. Returns true when it was an authorization
	// update (and the state changed); requests to send right away are
	// appended to `out`.
	bool Handle(const json& object, std::vector<json>& out);

	const AuthState& State() const { return fState; }
	const TdParams& Params() const { return fParams; }

	// Supplies the API key after NeedApiKey; returns the request to send.
	json SetApiKey(int32_t apiId, const std::string& apiHash);

	json PhoneRequest(const std::string& phone) const;
	json CodeRequest(const std::string& code) const;
	json PasswordRequest(const std::string& password) const;
	json RegisterRequest(const std::string& first, const std::string& last) const;
	json QrRequest() const;
	static json LogOutRequest();

	json ParametersRequest() const;

	// True when `object` is an answer to one of our requests.
	static bool IsAuthReply(const json& object);
	// Human-readable text of a TDLib error ("PHONE_CODE_INVALID" → …).
	static std::string ErrorText(const json& error);
	// True when the error means the API ID or hash is no good.
	static bool IsApiKeyError(const json& error);

private:
	TdParams fParams;
	AuthState fState;
};

// Normalises a phone number as typed: keeps digits and a leading '+'.
std::string NormalizePhone(const std::string& typed);

} // namespace pp
