// SPDX-License-Identifier: GPL-3.0-or-later
// One window for every login step, one card per step, like Paper Plane's
// login stack: API key (ours), phone number or QR code, code, password,
// new account.
#pragma once

#include <Window.h>

class BButton;
class BCardLayout;
class BStringView;
class BTextControl;
class BTextView;
class QrView;

class LoginWindow : public BWindow {
public:
	explicit LoginWindow(bool testDc);

	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;

private:
	enum Card { kCardApiKey, kCardPhone, kCardCode, kCardPassword,
		kCardName, kCardQr, kCardMessage };

	BView* _MakeCard(const char* title, BTextView** description,
		BView* fields, BButton** next, uint32 what, BButton* extra = nullptr);
	void _OpenApiSite();
	void _PasteApiKey();
	void _ShowCard(Card card);
	void _SetBusy(bool busy);
	void _Submit(uint32 what);
	void _SetError(const char* text);

	BCardLayout* fCards;
	Card fCard;

	BTextControl* fApiId;
	BTextControl* fApiHash;
	BTextControl* fPhone;
	BTextControl* fCode;
	BTextControl* fPassword;
	BTextControl* fFirstName;
	BTextControl* fLastName;

	BTextView* fCodeInfo;
	BTextView* fPasswordInfo;
	BTextView* fMessageText;
	QrView* fQr;
	BButton* fQrButton;
	// The QR code was left for the phone number: TDLib keeps sending new
	// codes, which then mustn't take the window back to it.
	bool fPreferPhone = false;

	BButton* fNext[kCardMessage];
	bool fApiSiteOpened = false;   // once by itself, then by the button
	BTextView* fError;
};
