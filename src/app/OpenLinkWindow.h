// SPDX-License-Identifier: GPL-3.0-or-later
// "Open username or link": a username, an invite link or any t.me link,
// opened in the app.
#pragma once

#include <Window.h>

class BStringView;
class BTextControl;

class OpenLinkWindow : public BWindow {
public:
	explicit OpenLinkWindow(BWindow* parent);
	void MessageReceived(BMessage* message) override;

private:
	BTextControl* fText;
	BStringView* fProblem;
};
