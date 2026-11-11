// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenLinkWindow.h"

#include <Application.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>

#include "../core/Links.h"
#include "Messages.h"

static const char* kHint = "For example @username, t.me/username or an invite link.";

enum {
	kMsgOpen = 'open',
	kMsgEdited = 'edtd'
};

OpenLinkWindow::OpenLinkWindow(BWindow* parent)
	:
	BWindow(BRect(0, 0, 100, 50), "Open username or link", B_FLOATING_WINDOW_LOOK,
		B_FLOATING_SUBSET_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE)
{
	AddToSubset(parent);
	fText = new BTextControl("text", "Username or link:", "", nullptr);
	fText->SetModificationMessage(new BMessage(kMsgEdited));
	fText->TextView()->SetExplicitMinSize(BSize(be_plain_font->StringWidth("m") * 24,
		B_SIZE_UNSET));
	// A hint, or what's wrong with what was typed.
	fProblem = new BStringView("problem", kHint);
	fProblem->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
	BButton* open = new BButton("open", "Open", new BMessage(kMsgOpen));
	BButton* cancel = new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED));
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fText)
		.Add(fProblem)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(open)
		.End();
	SetDefaultButton(open);
	fText->MakeFocus(true);

	// Centred over the main window.
	BRect frame = parent->Frame();
	CenterIn(frame);
}


void
OpenLinkWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgEdited:
			fProblem->SetText(kHint);
			fProblem->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
			break;
		case kMsgOpen:
		{
			std::string url = pp::NormalizeLink(fText->Text());
			if (url.empty()) {
				fProblem->SetText("That is not a username or a Telegram link.");
				fProblem->SetHighUIColor(B_FAILURE_COLOR);
				break;
			}
			BMessage open(kMsgOpenLink);
			open.AddString("url", url.c_str());
			be_app->PostMessage(&open);
			Quit();
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}
