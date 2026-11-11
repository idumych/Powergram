// SPDX-License-Identifier: GPL-3.0-or-later
// Powergram's Deskbar item: the app's icon with the number of unread chats.
// A click brings Powergram to the front; files dropped on it are sent to a
// chat chosen in the app. It's a small add-on of its own (Deskbar loads it
// with BDeskbar::AddItem(entry_ref*)) so that Deskbar never loads the app
// itself, and it only talks to the running app by messages.
#include <Bitmap.h>
#include <IconUtils.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Mime.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <String.h>
#include <View.h>

#include <algorithm>
#include <cmath>

#include "../app/Messages.h"

namespace {

enum {
	kMsgOpenApp = 'dbop',
	kMsgQuitApp = 'dbqt'
};

} // namespace


class _EXPORT PowergramDeskbarItem : public BView {
public:
	PowergramDeskbarItem(BRect frame)
		:
		BView(frame, kDeskbarItemName, B_FOLLOW_NONE, B_WILL_DRAW)
	{
	}

	// Deskbar archives the view instantiate_deskbar_item() gives and puts
	// it on its shelf from the archive: hence these three.
	PowergramDeskbarItem(BMessage* archive)
		:
		BView(archive)
	{
	}

	static BArchivable* Instantiate(BMessage* archive);

	status_t Archive(BMessage* data, bool deep = true) const override
	{
		return BView::Archive(data, deep);
	}

	~PowergramDeskbarItem() override
	{
		delete fIcon;
	}

	void AttachedToWindow() override
	{
		BView::AttachedToWindow();
		AdoptParentColors();
		_LoadIcon();
		_SayHello();

	}

	void Draw(BRect) override
	{
		BRect b = Bounds();
		SetDrawingMode(B_OP_ALPHA);
		if (fIcon != nullptr)
			DrawBitmap(fIcon, b.LeftTop());
		if (fUnread <= 0)
			return;
		// A red badge at the bottom right, with the count when it fits.
		BString count;
		if (fUnread > 99)
			count = "99+";
		else
			count << fUnread;
		BFont font(be_bold_font);
		font.SetSize(std::max(7.0f, floorf(b.Height() * 0.55f)));
		font_height fh;
		font.GetHeight(&fh);
		float h = ceilf(fh.ascent + fh.descent);
		float w = std::max(h, font.StringWidth(count.String()) + 3);
		BRect badge(b.right - w + 1, b.bottom - h + 1, b.right, b.bottom);
		SetHighColor(220, 50, 47);
		FillRoundRect(badge, h / 2, h / 2);
		SetHighColor(255, 255, 255);
		SetFont(&font);
		DrawString(count.String(), BPoint(badge.left + (badge.Width() + 1
			- font.StringWidth(count.String())) / 2, badge.bottom - fh.descent));
	}

	void MouseDown(BPoint where) override
	{
		BPoint point;
		uint32 buttons = 0;
		GetMouse(&point, &buttons, false);
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0) {
			_OpenApp();
			return;
		}
		BPopUpMenu* menu = new BPopUpMenu("Powergram", false, false);
		menu->AddItem(new BMenuItem("Open Powergram", new BMessage(kMsgOpenApp)));
		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem("Quit Powergram", new BMessage(kMsgQuitApp)));
		menu->SetTargetForItems(this);
		menu->SetAsyncAutoDestruct(true);
		menu->Go(ConvertToScreen(where), true, true, true);
	}

	void MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case kMsgDeskbarUnread:
			{
				int32 unread = message->GetInt32("unread", 0);
				if (unread != fUnread) {
					fUnread = unread;
					BString tip(APP_NAME);
					if (unread > 0)
						tip << ": " << unread << (unread == 1 ? " unread chat" : " unread chats");
					SetToolTip(tip.String());
					Invalidate();
				}
				break;
			}
			case B_SIMPLE_DATA:
			{
				// Files from Tracker: the app asks which chat they go to.
				if (!message->HasRef("refs"))
					break;
				BMessage files(*message);
				files.what = kMsgDeskbarFiles;
				_OpenApp();
				BMessenger(APP_SIGNATURE).SendMessage(&files);
				break;
			}
			case kMsgOpenApp:
				_OpenApp();
				break;
			case kMsgQuitApp:
				BMessenger(APP_SIGNATURE).SendMessage(B_QUIT_REQUESTED);
				break;
			default:
				BView::MessageReceived(message);
		}
	}

private:
	void _LoadIcon()
	{
		delete fIcon;
		fIcon = nullptr;
		BRect b = Bounds();
		uint8* data = nullptr;
		size_t size = 0;
		if (BMimeType(APP_SIGNATURE).GetIcon(&data, &size) != B_OK)
			return;
		fIcon = new BBitmap(BRect(0, 0, b.IntegerWidth(), b.IntegerHeight()), B_RGBA32);
		if (BIconUtils::GetVectorIcon(data, size, fIcon) != B_OK) {
			delete fIcon;
			fIcon = nullptr;
		}
		delete[] data;
	}

	// Tells the app where to send the count (it sends it on every change).
	void _SayHello()
	{
		BMessage hello(kMsgDeskbarHello);
		hello.AddMessenger("item", BMessenger(this));
		BMessenger(APP_SIGNATURE).SendMessage(&hello);
	}

	void _OpenApp()
	{
		if (be_roster->IsRunning(APP_SIGNATURE))
			be_roster->ActivateApp(be_roster->TeamFor(APP_SIGNATURE));
		else
			be_roster->Launch(APP_SIGNATURE);
	}

	BBitmap* fIcon = nullptr;
	int32 fUnread = 0;
};


// Defined here, not in the class, so that it's emitted and exported for
// the shelf to find by name.
BArchivable*
PowergramDeskbarItem::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "PowergramDeskbarItem"))
		return nullptr;
	return new PowergramDeskbarItem(archive);
}


// What Deskbar calls for an item added with BDeskbar::AddItem(entry_ref*).
extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{

	float side = std::min(maxWidth, maxHeight);
	return new PowergramDeskbarItem(BRect(0, 0, side - 1, side - 1));
}
