// SPDX-License-Identifier: GPL-3.0-or-later
#include "Profile.h"

#include <algorithm>

#include "ChatHistory.h"

namespace pp {

bool
Profile::Apply(const json& object)
{
	std::string type = Type(object);
	const json* user = nullptr;
	if (type == "user")
		user = &object;
	else if (type == "updateUser")
		user = &Field(object, "user");
	if (user != nullptr) {
		if (userId != 0 && Int(*user, "id") != userId)
			return false;
		userId = Int(*user, "id");
		firstName = Str(*user, "first_name");
		lastName = Str(*user, "last_name");
		const json& active = Field(Field(*user, "usernames"), "active_usernames");
		username = active.is_array() && !active.empty() && active[0].is_string()
			? active[0].get<std::string>() : std::string();
		const json& photo = Field(*user, "profile_photo");
		photoId = Int(photo, "id");
		photoFileId = (int32_t)Int(Field(photo, "big"), "id");
		photoPreview = DecodeBase64(Str(Field(photo, "minithumbnail"), "data"));
		return true;
	}
	const json* full = nullptr;
	if (type == "userFullInfo")
		full = &object;
	else if (type == "updateUserFullInfo" && Int(object, "user_id") == userId && userId != 0)
		full = &Field(object, "user_full_info");
	if (full != nullptr) {
		bio = Str(Field(*full, "bio"), "text");
		return true;
	}
	return false;
}


size_t
CharacterCount(const std::string& text)
{
	size_t count = 0;
	for (unsigned char c : text)
		count += (c & 0xC0) != 0x80;
	return count;
}


std::string
ProfileProblem(const std::string& first, const std::string& last, const std::string& bio)
{
	if (first.find_first_not_of(" \t") == std::string::npos)
		return "Enter a first name.";
	if (CharacterCount(first) > 64 || CharacterCount(last) > 64)
		return "Names can be at most 64 characters long.";
	if (CharacterCount(bio) > 70)
		return "The bio can be at most 70 characters long.";
	return std::string();
}


std::vector<Session>
ParseSessions(const json& reply)
{
	std::vector<Session> sessions;
	const json& list = Field(reply, "sessions");
	if (!list.is_array())
		return sessions;
	for (const json& s : list) {
		Session session;
		session.id = Int(s, "id");
		session.current = Bool(s, "is_current");
		session.unconfirmed = Bool(s, "is_unconfirmed");
		session.application = Str(s, "application_name");
		std::string version = Str(s, "application_version");
		if (!version.empty())
			session.application += " " + version;
		session.device = Str(s, "device_model");
		std::string system = Str(s, "platform");
		std::string systemVersion = Str(s, "system_version");
		if (!systemVersion.empty())
			system += (system.empty() ? "" : " ") + systemVersion;
		if (!system.empty() && system != session.device)
			session.device += (session.device.empty() ? "" : ", ") + system;
		session.location = Str(s, "location");
		session.lastActive = Int(s, "last_active_date");
		sessions.push_back(session);
	}
	std::stable_sort(sessions.begin(), sessions.end(), [](const Session& a, const Session& b) {
		if (a.current != b.current)
			return a.current;
		return a.lastActive > b.lastActive;
	});
	return sessions;
}


std::string
ActiveText(int64_t lastActive, int64_t now)
{
	int64_t ago = now - lastActive;
	if (ago < 5 * 60)
		return "online";
	if (ago < 3600) {
		int64_t m = ago / 60;
		return std::to_string(m) + " minutes ago";
	}
	if (ago < 24 * 3600) {
		int64_t h = ago / 3600;
		return h == 1 ? "an hour ago" : std::to_string(h) + " hours ago";
	}
	if (ago < 48 * 3600)
		return "yesterday";
	return std::string();
}


namespace request {

json
GetMe(const std::string& extra)
{
	return {{"@type", "getMe"}, {"@extra", extra}};
}


json
UserFullInfo(int64_t userId, const std::string& extra)
{
	return {{"@type", "getUserFullInfo"}, {"user_id", userId}, {"@extra", extra}};
}


json
SetName(const std::string& first, const std::string& last, const std::string& extra)
{
	return {{"@type", "setName"}, {"first_name", first}, {"last_name", last}, {"@extra", extra}};
}


json
SetBio(const std::string& bio, const std::string& extra)
{
	return {{"@type", "setBio"}, {"bio", bio}, {"@extra", extra}};
}


json
SetProfilePhoto(const std::string& path, const std::string& extra)
{
	return {{"@type", "setProfilePhoto"},
		{"photo", {{"@type", "inputChatPhotoStatic"},
			{"photo", {{"@type", "inputFileLocal"}, {"path", path}}}}},
		{"is_public", false}, {"@extra", extra}};
}


json
DeleteProfilePhoto(int64_t photoId, const std::string& extra)
{
	// 64-bit ids go as strings, so nothing on the way rounds them.
	return {{"@type", "deleteProfilePhoto"}, {"profile_photo_id", std::to_string(photoId)},
		{"@extra", extra}};
}


json
ActiveSessions(const std::string& extra)
{
	return {{"@type", "getActiveSessions"}, {"@extra", extra}};
}


json
TerminateSession(int64_t sessionId, const std::string& extra)
{
	return {{"@type", "terminateSession"}, {"session_id", std::to_string(sessionId)},
		{"@extra", extra}};
}


json
TerminateOtherSessions(const std::string& extra)
{
	return {{"@type", "terminateAllOtherSessions"}, {"@extra", extra}};
}

} // namespace request

} // namespace pp
