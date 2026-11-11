// SPDX-License-Identifier: GPL-3.0-or-later
// Settings: notifications and text size. Changes apply at once; Revert
// goes back to how things were when the window opened, Defaults to the
// defaults (Haiku HIG). The window tells the app with kMsgSettingsChanged.
#pragma once

#include <Messenger.h>
#include <Window.h>

class BButton;
class BCheckBox;
class BMenuField;

struct AppSettings {
	bool notifications = true;
	bool previews = true;       // the message text in notifications
	bool sound = true;          // the "Powergram new message" system sound
	int32 textSize = 1;         // 0 small, 1 normal, 2 large

	bool operator==(const AppSettings& o) const
	{
		return notifications == o.notifications && previews == o.previews
			&& sound == o.sound && textSize == o.textSize;
	}
	void ToMessage(BMessage* message) const;
	static AppSettings FromMessage(const BMessage* message);
	static float Scale(int32 textSize) { return textSize == 0 ? 0.9f : textSize == 2 ? 1.2f : 1.0f; }
};

class SettingsWindow : public BWindow {
public:
	SettingsWindow(BMessenger app, const AppSettings& current);

	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;

private:
	void _Show(const AppSettings& settings);
	AppSettings _Read() const;
	void _Apply();

	BMessenger fApp;
	AppSettings fRevert;     // as when the window opened
	BCheckBox* fNotifications;
	BCheckBox* fPreviews;
	BCheckBox* fSound;
	BMenuField* fTextSize;
	BButton* fRevertButton;
	BButton* fDefaultsButton;
};
