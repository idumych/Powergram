// SPDX-License-Identifier: GPL-3.0-or-later
#include "AuthFlow.h"

#include <cctype>

namespace pp {

const char* const kAuthExtra = "auth";

static json
Tagged(json request)
{
	request["@extra"] = kAuthExtra;
	return request;
}


static std::string
CodeTypeText(const json& type)
{
	std::string t = Type(type);
	if (t == "authenticationCodeTypeTelegramMessage")
		return "to your Telegram app on another device";
	if (t == "authenticationCodeTypeSms" || t == "authenticationCodeTypeSmsWord"
		|| t == "authenticationCodeTypeSmsPhrase")
		return "by SMS";
	if (t == "authenticationCodeTypeCall")
		return "by phone call";
	if (t == "authenticationCodeTypeFlashCall"
		|| t == "authenticationCodeTypeMissedCall")
		return "as a missed call";
	if (t == "authenticationCodeTypeFragment")
		return "to Fragment";
	if (t == "authenticationCodeTypeFirebaseAndroid"
		|| t == "authenticationCodeTypeFirebaseIos")
		return "by SMS";
	return "by Telegram";
}


bool
AuthFlow::Handle(const json& object, std::vector<json>& out)
{
	if (Type(object) != "updateAuthorizationState")
		return false;

	const json& s = Field(object, "authorization_state");
	std::string type = Type(s);
	AuthState next;
	next.tdType = type;

	if (type == "authorizationStateWaitTdlibParameters") {
		if (fParams.HasApiKey()) {
			next.step = AuthStep::Starting;
			out.push_back(ParametersRequest());
		} else
			next.step = AuthStep::NeedApiKey;
	} else if (type == "authorizationStateWaitPhoneNumber") {
		next.step = AuthStep::Phone;
	} else if (type == "authorizationStateWaitCode") {
		next.step = AuthStep::Code;
		const json& codeType = Field(Field(s, "code_info"), "type");
		next.codeInfo = CodeTypeText(codeType);
		next.codeLength = Int(codeType, "length");
	} else if (type == "authorizationStateWaitPassword") {
		next.step = AuthStep::Password;
		next.passwordHint = Str(s, "password_hint");
	} else if (type == "authorizationStateWaitRegistration") {
		next.step = AuthStep::Registration;
	} else if (type == "authorizationStateWaitOtherDeviceConfirmation") {
		next.step = AuthStep::OtherDevice;
		next.link = Str(s, "link");
	} else if (type == "authorizationStateWaitEmailAddress"
		|| type == "authorizationStateWaitEmailCode") {
		next.step = AuthStep::Email;
	} else if (type == "authorizationStateReady") {
		next.step = AuthStep::Ready;
	} else if (type == "authorizationStateLoggingOut") {
		next.step = AuthStep::LoggingOut;
	} else if (type == "authorizationStateClosing") {
		next.step = AuthStep::Closing;
	} else if (type == "authorizationStateClosed") {
		next.step = AuthStep::Closed;
	} else {
		next.step = AuthStep::Unsupported;
	}

	fState = next;
	return true;
}


json
AuthFlow::SetApiKey(int32_t apiId, const std::string& apiHash)
{
	fParams.apiId = apiId;
	fParams.apiHash = apiHash;
	fState.step = AuthStep::Starting;
	return ParametersRequest();
}


json
AuthFlow::ParametersRequest() const
{
	return Tagged({
		{"@type", "setTdlibParameters"},
		{"use_test_dc", fParams.testDc},
		{"database_directory", fParams.databaseDir},
		{"files_directory", ""},
		{"database_encryption_key", ""},
		{"use_file_database", true},
		{"use_chat_info_database", true},
		{"use_message_database", true},
		{"use_secret_chats", false},
		{"api_id", fParams.apiId},
		{"api_hash", fParams.apiHash},
		{"system_language_code", fParams.language},
		{"device_model", fParams.deviceModel},
		{"system_version", fParams.systemVersion},
		{"application_version", fParams.appVersion}
	});
}


json
AuthFlow::PhoneRequest(const std::string& phone) const
{
	return Tagged({{"@type", "setAuthenticationPhoneNumber"},
		{"phone_number", NormalizePhone(phone)}});
}


json
AuthFlow::CodeRequest(const std::string& code) const
{
	std::string digits;
	for (char c : code)
		if (std::isdigit(static_cast<unsigned char>(c)))
			digits += c;
	return Tagged({{"@type", "checkAuthenticationCode"}, {"code", digits}});
}


json
AuthFlow::PasswordRequest(const std::string& password) const
{
	return Tagged({{"@type", "checkAuthenticationPassword"},
		{"password", password}});
}


json
AuthFlow::RegisterRequest(const std::string& first, const std::string& last) const
{
	return Tagged({{"@type", "registerUser"}, {"first_name", first},
		{"last_name", last}, {"disable_notification", true}});
}


json
AuthFlow::QrRequest() const
{
	return Tagged({{"@type", "requestQrCodeAuthentication"},
		{"other_user_ids", json::array()}});
}


json
AuthFlow::LogOutRequest()
{
	return Tagged({{"@type", "logOut"}});
}


bool
AuthFlow::IsAuthReply(const json& object)
{
	return Str(object, "@extra") == kAuthExtra;
}


std::string
AuthFlow::ErrorText(const json& error)
{
	std::string msg = Str(error, "message");
	struct { const char* code; const char* text; } known[] = {
		{"PHONE_NUMBER_INVALID", "That phone number is not valid. Include the country code, for example +44."},
		{"PHONE_NUMBER_BANNED", "This phone number is banned from Telegram."},
		{"PHONE_CODE_INVALID", "That code is wrong. Check it and try again."},
		{"PHONE_CODE_EXPIRED", "That code has expired. Go back and ask for a new one."},
		{"PASSWORD_HASH_INVALID", "That password is wrong."},
		{"API_ID_INVALID", "Telegram does not accept this API ID and hash. Check them at my.telegram.org."},
		{"API_ID_PUBLISHED_FLOOD", "Telegram has blocked this API ID. Register your own at my.telegram.org."},
		{"FIRSTNAME_INVALID", "That first name is not allowed."},
	};
	for (const auto& k : known)
		if (msg.find(k.code) != std::string::npos)
			return k.text;
	if (msg.rfind("Too Many Requests", 0) == 0 || msg.find("FLOOD_WAIT") != std::string::npos)
		return "Telegram asks you to wait before trying again. (" + msg + ")";
	if (msg.empty())
		return "Telegram reported an error.";
	return "Telegram reported an error: " + msg;
}


bool
AuthFlow::IsApiKeyError(const json& error)
{
	std::string msg = Str(error, "message");
	return msg.find("API_ID_INVALID") != std::string::npos
		|| msg.find("API_ID_PUBLISHED_FLOOD") != std::string::npos;
}


std::string
NormalizePhone(const std::string& typed)
{
	std::string out;
	for (char c : typed) {
		if (std::isdigit(static_cast<unsigned char>(c)))
			out += c;
		else if (c == '+' && out.empty())
			out += c;
	}
	return out;
}

} // namespace pp
