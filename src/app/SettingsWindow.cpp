// SPDX-License-Identifier: GPL-3.0-or-later
#include "SettingsWindow.h"

#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StringView.h>

#include "Messages.h"

namespace {

enum {
	kMsgChanged = 'schg',
	kMsgRevert = 'srev',
	kMsgDefaults = 'sdef'
};

const char* kSizes[] = {"Small", "Normal", "Large"};

} // namespace


void
AppSettings::ToMessage(BMessage* message) const
{
	message->AddBool("notifications", notifications);
	message->AddBool("previews", previews);
	message->AddBool("sound", sound);
	message->AddInt32("text_size", textSize);
	message->AddBool("deskbar", deskbar);
}


AppSettings
AppSettings::FromMessage(const BMessage* message)
{
	AppSettings s;
	s.notifications = message->GetBool("notifications", true);
	s.previews = message->GetBool("previews", true);
	s.sound = message->GetBool("sound", true);
	s.textSize = message->GetInt32("text_size", 1);
	s.deskbar = message->GetBool("deskbar", true);
	return s;
}


SettingsWindow::SettingsWindow(BMessenger app, const AppSettings& current)
	:
	BWindow(BRect(0, 0, 300, 200), "Settings", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fApp(app),
	fRevert(current)
{
	fNotifications = new BCheckBox("notifications", "Show notifications for new messages",
		new BMessage(kMsgChanged));
	fPreviews = new BCheckBox("previews", "Show the message text in notifications",
		new BMessage(kMsgChanged));
	fSound = new BCheckBox("sound", "Play a sound for new messages",
		new BMessage(kMsgChanged));
	fDeskbar = new BCheckBox("deskbar", "Show Powergram in the Deskbar with the unread count",
		new BMessage(kMsgChanged));
	BPopUpMenu* sizes = new BPopUpMenu("sizes");
	for (int32 i = 0; i < 3; i++)
		sizes->AddItem(new BMenuItem(kSizes[i], new BMessage(kMsgChanged)));
	fTextSize = new BMenuField("text size", "Text size:", sizes);
	BStringView* soundHint = new BStringView("sound hint",
		"The sound is chosen in the Sounds preferences.");
	soundHint->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
	fRevertButton = new BButton("revert", "Revert", new BMessage(kMsgRevert));
	fDefaultsButton = new BButton("defaults", "Defaults", new BMessage(kMsgDefaults));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fNotifications)
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(B_USE_ITEM_INSETS, 0, 0, 0)
			.Add(fPreviews)
		.End()
		.Add(fSound)
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(B_USE_ITEM_INSETS, 0, 0, 0)
			.Add(soundHint)
		.End()
		.Add(fDeskbar)
		.Add(fTextSize)
		.AddGroup(B_HORIZONTAL)
			.Add(fDefaultsButton)
			.Add(fRevertButton)
			.AddGlue()
		.End();
	_Show(current);
	CenterOnScreen();
}


void
SettingsWindow::_Show(const AppSettings& s)
{
	fNotifications->SetValue(s.notifications);
	fPreviews->SetValue(s.previews);
	fPreviews->SetEnabled(s.notifications);
	fSound->SetValue(s.sound);
	fDeskbar->SetValue(s.deskbar);
	if (BMenuItem* item = fTextSize->Menu()->ItemAt(s.textSize))
		item->SetMarked(true);
	fRevertButton->SetEnabled(!(s == fRevert));
	fDefaultsButton->SetEnabled(!(s == AppSettings()));
}


AppSettings
SettingsWindow::_Read() const
{
	AppSettings s;
	s.notifications = fNotifications->Value() == B_CONTROL_ON;
	s.previews = fPreviews->Value() == B_CONTROL_ON;
	s.sound = fSound->Value() == B_CONTROL_ON;
	s.deskbar = fDeskbar->Value() == B_CONTROL_ON;
	BMenuItem* marked = fTextSize->Menu()->FindMarked();
	s.textSize = marked != nullptr ? fTextSize->Menu()->IndexOf(marked) : 1;
	return s;
}


void
SettingsWindow::_Apply()
{
	AppSettings s = _Read();
	_Show(s);
	BMessage changed(kMsgSettingsChanged);
	s.ToMessage(&changed);
	fApp.SendMessage(&changed);
}


void
SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgChanged:
			_Apply();
			break;
		case kMsgRevert:
			_Show(fRevert);
			_Apply();
			break;
		case kMsgDefaults:
			_Show(AppSettings());
			_Apply();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


bool
SettingsWindow::QuitRequested()
{
	BMessage closed(kMsgSettingsClosed);
	fApp.SendMessage(&closed);
	return true;
}
