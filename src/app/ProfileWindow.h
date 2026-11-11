// SPDX-License-Identifier: GPL-3.0-or-later
// Your profile: picture, name, bio (username shown), and the devices
// logged in to the account, any of which can be logged out.
#pragma once

#include <Messenger.h>
#include <String.h>
#include <Window.h>

class AvatarView;
class BButton;
class BFilePanel;
class BListView;
class BStringView;
class BTextControl;

class ProfileWindow : public BWindow {
public:
	explicit ProfileWindow(BMessenger app);
	~ProfileWindow() override;

	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;

private:
	void _SetProfile(const BMessage* data);
	void _SetSessions(const BMessage* sessions);
	void _Changed();
	void _Save();
	void _Revert();

	BMessenger fApp;
	AvatarView* fAvatar;
	BButton* fChangePhoto;
	BButton* fRemovePhoto;
	BTextControl* fFirst;
	BTextControl* fLast;
	BStringView* fUsername;
	BTextControl* fBio;
	BStringView* fBioCount;
	BStringView* fProblem;
	BButton* fSave;
	BButton* fRevert;
	BListView* fSessions;
	BButton* fLogOut;
	BButton* fLogOutOthers;
	BFilePanel* fPanel = nullptr;
	// What Telegram has, to compare the fields with.
	BString fSavedFirst, fSavedLast, fSavedBio;
	bool fLoaded = false;
	bool fSaving = false;
};
