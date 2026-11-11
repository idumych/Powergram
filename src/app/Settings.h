// SPDX-License-Identifier: GPL-3.0-or-later
// Settings: a flattened BMessage in ~/config/settings/Powergram/settings.
#pragma once

#include <Message.h>
#include <Path.h>
#include <Rect.h>
#include <String.h>

#include <string>
#include <vector>

class Settings {
public:
	Settings();

	status_t Load();
	status_t Save() const;

	// The folder for settings and TDLib's database.
	static BPath Directory();

	int32 apiId;
	BString apiHash;
	BRect mainFrame;   // invalid until the window was placed once
	std::vector<std::string> recentEmoji;  // most recent first
	BMessage preferences;   // the Settings window's choices (AppSettings)

private:
	bool _ImportApiKeyFile();
};
