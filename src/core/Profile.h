// SPDX-License-Identifier: GPL-3.0-or-later
// The user's own profile (name, username, bio, photo) and the devices
// logged in to the account, for the Profile window.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Json.h"

namespace pp {

struct Profile {
	int64_t userId = 0;
	std::string firstName;
	std::string lastName;
	std::string username;      // first active one, without the @
	std::string bio;
	int64_t photoId = 0;       // for deleteProfilePhoto; 0 without a photo
	int32_t photoFileId = 0;   // the big (640 px) picture
	std::string photoPreview;  // its tiny JPEG

	// Takes the user (getMe, or updateUser for this user) and userFullInfo
	// or updateUserFullInfo for it; true if it was one of them.
	bool Apply(const json& object);
};

// What's wrong with a name and bio before sending them, or "" if nothing:
// Telegram wants a first name, names up to 64 characters, a bio up to 70.
std::string ProfileProblem(const std::string& first, const std::string& last,
	const std::string& bio);
// Characters (not bytes) in UTF-8 text.
size_t CharacterCount(const std::string& text);

struct Session {
	int64_t id = 0;
	bool current = false;
	bool unconfirmed = false;   // a new login waiting to be confirmed
	std::string application;    // "Telegram Desktop 5.2"
	std::string device;         // "MacBook Pro, macOS 14.4"
	std::string location;       // "Berlin, Germany"
	int64_t lastActive = 0;
};

// The sessions in a "sessions" reply: this device first, then the most
// recently active.
std::vector<Session> ParseSessions(const json& sessions);
// "online" (within five minutes), "12 minutes ago", "3 hours ago",
// "yesterday", or a date given by `formatDate`'s caller ("" here).
std::string ActiveText(int64_t lastActive, int64_t now);

namespace request {

json GetMe(const std::string& extra);
json UserFullInfo(int64_t userId, const std::string& extra);
json SetName(const std::string& first, const std::string& last, const std::string& extra);
json SetBio(const std::string& bio, const std::string& extra);
json SetProfilePhoto(const std::string& path, const std::string& extra);
json DeleteProfilePhoto(int64_t photoId, const std::string& extra);
json ActiveSessions(const std::string& extra);
json TerminateSession(int64_t sessionId, const std::string& extra);
json TerminateOtherSessions(const std::string& extra);

} // namespace request

} // namespace pp
