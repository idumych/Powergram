// SPDX-License-Identifier: GPL-3.0-or-later
#include "LoginWindow.h"

#include <Application.h>
#include <Button.h>
#include <CardLayout.h>
#include <GridView.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Url.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "../core/AuthFlow.h"
#include "../core/QrCode.h"
#include "Messages.h"

namespace {
enum {
	kMsgOpenApiSite = 'oapi',
	kMsgApiKeyEdited = 'eapi',
	kMsgUsePhone = 'uphn'
};
// my.telegram.org's "API development tools" page (it asks for a login first).
const char* kApiSite = "https://my.telegram.org/apps";
}

// Width of every text block; heights follow the wrapped text.
static float
TextWidth()
{
	return ceilf(be_plain_font->StringWidth("m") * 30);
}


// Sets wrapped text and makes the view exactly as tall as it needs, so the
// window's size limits (B_AUTO_UPDATE_SIZE_LIMITS) grow to fit it.
static void
SetWrappedText(BTextView* view, const char* text, int32 minLines = 1)
{
	rgb_color color = ui_color(B_PANEL_TEXT_COLOR);
	view->SetText(text);
	view->SetFontAndColor(0, view->TextLength(), be_plain_font, B_FONT_ALL,
		&color);

	// Measure on a detached view: setting a text rect on the shown view
	// before it is laid out left it drawing nothing.
	float width = TextWidth();
	BRect rect(0, 0, width, 100000);
	BTextView measure(rect, "measure", rect, B_FOLLOW_NONE, 0);
	measure.SetWordWrap(true);
	measure.SetFontAndColor(be_plain_font, B_FONT_ALL);
	measure.SetText(text);
	float height = measure.TextHeight(0, measure.CountLines());
	height = ceilf(std::max(height, measure.LineHeight(0) * minLines));
	view->SetExplicitSize(BSize(width, height));
}


// The login link as a QR code, dark on white whatever the colours, with
// the light margin readers need around it.
class QrView : public BView {
public:
	QrView()
		:
		BView("qr code", B_WILL_DRAW)
	{
		float side = ceilf(be_plain_font->Size() * 16);
		SetExplicitSize(BSize(side, side));
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	void SetLink(const char* link)
	{
		if (fLink == link)
			return;
		fLink = link;
		fQr = pp::EncodeQr(link);
		Invalidate();
	}

	void Draw(BRect) override
	{
		BRect bounds = Bounds();
		SetHighColor(255, 255, 255);
		FillRect(bounds);
		if (fQr.size == 0)
			return;
		// Whole pixels a module, so every module comes out the same size.
		const int margin = 4;
		int cells = fQr.size + 2 * margin;
		float module = floorf(std::min(bounds.Width() + 1, bounds.Height() + 1) / cells);
		if (module < 1)
			return;
		BPoint origin(floorf((bounds.Width() + 1 - module * fQr.size) / 2),
			floorf((bounds.Height() + 1 - module * fQr.size) / 2));
		SetHighColor(0, 0, 0);
		for (int y = 0; y < fQr.size; y++) {
			for (int x = 0; x < fQr.size; x++) {
				if (fQr.Dark(x, y))
					FillRect(BRect(origin.x + x * module, origin.y + y * module,
						origin.x + (x + 1) * module - 1, origin.y + (y + 1) * module - 1));
			}
		}
	}

private:
	BString fLink;
	pp::QrMatrix fQr;
};


LoginWindow::LoginWindow(bool testDc)
	:
	BWindow(BRect(0, 0, 400, 260),
		testDc ? APP_NAME " (test servers)" : APP_NAME,
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS),
	fCard(kCardPhone)
{
	BTextView* apiInfo;
	BTextView* phoneInfo;
	BTextView* nameInfo;
	BTextView* qrInfo;

	fApiId = new BTextControl("App api_id:", "", nullptr);
	fApiHash = new BTextControl("App api_hash:", "", nullptr);
	// Pasting the whole page (or both lines) into either field fills both.
	fApiId->SetModificationMessage(new BMessage(kMsgApiKeyEdited));
	fApiHash->SetModificationMessage(new BMessage(kMsgApiKeyEdited));
	fPhone = new BTextControl("Phone number:", "", nullptr);
	fCode = new BTextControl("Code:", "", nullptr);
	fPassword = new BTextControl("Password:", "", nullptr);
	fPassword->TextView()->HideTyping(true);
	fFirstName = new BTextControl("First name:", "", nullptr);
	fLastName = new BTextControl("Last name:", "", nullptr);

	// The code field takes any printable text (other layouts, a pasted
	// "Login code: 12345"): only its digits are sent (AuthFlow::CodeRequest).
	// Control characters stay out: Hokku (Wayland) types a NUL for a keypad
	// key with NumLock off, which hid everything typed after it.
	for (uint32 c = 0; c < 0x20; c++)
		fCode->TextView()->DisallowChar(c);

	auto fields = [](std::initializer_list<BTextControl*> controls) {
		BGridView* grid = new BGridView(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING);
		int32 row = 0;
		for (BTextControl* c : controls) {
			grid->GridLayout()->AddItem(c->CreateLabelLayoutItem(), 0, row);
			grid->GridLayout()->AddItem(c->CreateTextViewLayoutItem(), 1, row);
			row++;
		}
		return grid;
	};

	fCards = new BCardLayout();
	BView* cards = new BView("cards", 0, fCards);

	BButton* openSite = new BButton("open site", "Open my.telegram.org",
		new BMessage(kMsgOpenApiSite));
	fCards->AddView(_MakeCard("Telegram API key", &apiInfo,
		fields({fApiId, fApiHash}), &fNext[kCardApiKey], kMsgSubmitApiKey, openSite));
	fQrButton = new BButton("qr", "Log in with a QR code", new BMessage(kMsgSubmitQr));
	fCards->AddView(_MakeCard("Welcome", &phoneInfo,
		fields({fPhone}), &fNext[kCardPhone], kMsgSubmitPhone, fQrButton));
	fCards->AddView(_MakeCard("Enter the verification code", &fCodeInfo,
		fields({fCode}), &fNext[kCardCode], kMsgSubmitCode));
	fCards->AddView(_MakeCard("Enter your password", &fPasswordInfo,
		fields({fPassword}), &fNext[kCardPassword], kMsgSubmitPassword));
	fCards->AddView(_MakeCard("Create an account", &nameInfo,
		fields({fFirstName, fLastName}), &fNext[kCardName], kMsgSubmitName));
	fQr = new QrView();
	BGroupView* qrBox = new BGroupView(B_HORIZONTAL);
	BLayoutBuilder::Group<>(qrBox).AddGlue().Add(fQr).AddGlue();
	fCards->AddView(_MakeCard("Log in with a QR code", &qrInfo, qrBox,
		&fNext[kCardQr], kMsgUsePhone));
	fNext[kCardQr]->SetLabel("Use phone number");
	fCards->AddView(_MakeCard("Connecting", &fMessageText, nullptr, nullptr, 0));

	SetWrappedText(apiInfo, "Powergram needs a Telegram API key of your own. "
		"Log in at my.telegram.org (it opens in your browser) with your phone "
		"number: the code arrives in Telegram on your phone. Choose \"API development tools\" and create an app (any name "
		"will do). Then copy its App api_id and App api_hash here. The key "
		"stays on this computer.");
	SetWrappedText(phoneInfo, testDc
		? "Enter the phone number of an account on Telegram's test servers. "
			"Telegram no longer accepts its 99966 test numbers: create a test "
			"account first with Telegram's mobile app (iOS Debug menu, or the "
			"Android beta)."
		: "Enter your phone number with the country code.");
	SetWrappedText(qrInfo, "Open Telegram on your phone, go to Settings > "
		"Devices > Link Desktop Device and point the camera at this code. "
		"It changes every half minute.");
	SetWrappedText(nameInfo, "This phone number has no Telegram account yet. "
		"Enter the name to show to other people.");
	SetWrappedText(fMessageText, "Connecting to Telegram…");
	SetWrappedText(fCodeInfo, "", 2);
	SetWrappedText(fPasswordInfo, "", 2);

	// Wraps instead of widening the window; room for two lines.
	fError = new BTextView("error");
	fError->MakeEditable(false);
	fError->MakeSelectable(false);
	fError->SetWordWrap(true);
	fError->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fError->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	rgb_color failure = ui_color(B_FAILURE_COLOR);
	fError->SetFontAndColor(be_plain_font, B_FONT_ALL, &failure);
	SetWrappedText(fError, "", 2);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(cards)
		.Add(fError);

	_ShowCard(kCardMessage);
	CenterOnScreen();
}


BView*
LoginWindow::_MakeCard(const char* title, BTextView** description,
	BView* fields, BButton** next, uint32 what, BButton* extra)
{
	BStringView* heading = new BStringView("title", title);
	BFont font(be_bold_font);
	font.SetSize(font.Size() * 1.4f);
	heading->SetFont(&font);

	BTextView* text = new BTextView("description");
	text->MakeEditable(false);
	text->MakeSelectable(false);
	text->SetWordWrap(true);
	text->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	text->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	text->SetFontAndColor(be_plain_font, B_FONT_ALL);
	*description = text;

	BView* card = new BView(title, 0);
	BLayoutBuilder::Group<> builder(card, B_VERTICAL, B_USE_DEFAULT_SPACING);
	builder.Add(heading).Add(text);
	if (fields != nullptr)
		builder.Add(fields);
	if (next != nullptr) {
		*next = new BButton("next", "Next", new BMessage(what));
		BLayoutBuilder::Group<>::GroupBuilder buttons = builder.AddGroup(B_HORIZONTAL);
		if (extra != nullptr)
			buttons.Add(extra);
		buttons.AddGlue()
			.Add(*next)
		.End();
	}
	builder.AddGlue();
	return card;
}


void
LoginWindow::_ShowCard(Card card)
{
	fCard = card;
	fCards->SetVisibleItem((int32)card);
	_SetBusy(false);
	SetDefaultButton(card == kCardMessage || card == kCardQr ? nullptr : fNext[card]);

	BTextControl* focus = nullptr;
	switch (card) {
		case kCardApiKey: focus = fApiId; break;
		case kCardPhone: focus = fPhone; break;
		case kCardCode: focus = fCode; break;
		case kCardPassword: focus = fPassword; break;
		case kCardName: focus = fFirstName; break;
		default: break;
	}
	if (focus != nullptr) {
		focus->MakeFocus(true);
		focus->TextView()->SelectAll();
	}
}


void
LoginWindow::_OpenApiSite()
{
	fApiSiteOpened = true;
	BUrl url(kApiSite, true);
	if (url.OpenWithPreferredApplication(false) != B_OK)
		_SetError("Couldn't open a web browser: go to my.telegram.org.");
}


// my.telegram.org shows "App api_id: 123" and "App api_hash: 0123…" on
// separate lines; pasting both into one field puts each where it goes.
void
LoginWindow::_PasteApiKey()
{
	BString all(fApiId->Text());
	all << "\n" << fApiHash->Text();
	BString id, hash;
	int32 start = 0;
	auto isHex = [](char c) {
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
	};
	// The first run of 32 hex digits is the hash; a run of digits before
	// "hash" (or anywhere, failing that) is the id.
	while (start < all.Length()) {
		int32 end = start;
		while (end < all.Length() && isHex(all[end]))
			end++;
		if (end - start == 32 && hash.IsEmpty())
			all.CopyInto(hash, start, 32);
		else if (end > start && id.IsEmpty()) {
			BString run;
			all.CopyInto(run, start, end - start);
			bool digits = true;
			for (int32 i = 0; i < run.Length(); i++)
				digits &= run[i] >= '0' && run[i] <= '9';
			if (digits && run.Length() <= 10)
				id = run;
		}
		start = std::max(end, start + 1);
	}
	// Only rewrite the fields when the paste had more than one field's worth.
	bool idOnly = BString(fApiId->Text()).Trim() == id;
	bool hashOnly = BString(fApiHash->Text()).Trim() == hash;
	if (id.IsEmpty() || hash.IsEmpty() || (idOnly && hashOnly))
		return;
	fApiId->SetModificationMessage(nullptr);
	fApiHash->SetModificationMessage(nullptr);
	fApiId->SetText(id);
	fApiHash->SetText(hash);
	fApiId->SetModificationMessage(new BMessage(kMsgApiKeyEdited));
	fApiHash->SetModificationMessage(new BMessage(kMsgApiKeyEdited));
	fNext[kCardApiKey]->MakeFocus(true);
}


void
LoginWindow::_SetBusy(bool busy)
{
	fBusy = busy;
	if (fCard != kCardMessage)
		fNext[fCard]->SetEnabled(!busy);
	if (fCard == kCardPhone)
		fQrButton->SetEnabled(!busy);
}


void
LoginWindow::_SetError(const char* text)
{
	rgb_color failure = ui_color(B_FAILURE_COLOR);
	SetWrappedText(fError, text, 2);
	fError->SetFontAndColor(0, fError->TextLength(), be_plain_font, B_FONT_ALL,
		&failure);
}


void
LoginWindow::_Submit(uint32 what)
{
	// One at a time: TDLib refuses a second step while the first is on
	// its way ("Call to setAuthenticationPhoneNumber unexpected").
	if (fBusy)
		return;
	BMessage message(what);
	switch (what) {
		case kMsgSubmitApiKey:
		{
			BString idText(fApiId->Text());
			int32 id = atoi(idText.Trim().String());
			BString hash(fApiHash->Text());
			hash.Trim();
			if (id <= 0 || hash.Length() != 32) {
				_SetError("The api_id is a number and the api_hash has 32 characters.");
				return;
			}
			message.AddInt32("api_id", id);
			message.AddString("api_hash", hash);
			break;
		}
		case kMsgSubmitPhone:
			if (pp::NormalizePhone(fPhone->Text()).size() < 5) {
				_SetError("Enter the whole number, with the country code.");
				return;
			}
			message.AddString("text", fPhone->Text());
			break;
		case kMsgSubmitCode:
			if (strpbrk(fCode->Text(), "0123456789") == nullptr) {
				_SetError("Enter the code's digits.");
				return;
			}
			message.AddString("text", fCode->Text());
			break;
		case kMsgSubmitPassword:
			message.AddString("text", fPassword->Text());
			break;
		case kMsgSubmitQr:
			fPreferPhone = false;
			break;
		case kMsgSubmitName:
		{
			BString first(fFirstName->Text());
			if (first.Trim().IsEmpty()) {
				_SetError("Enter at least a first name.");
				return;
			}
			message.AddString("first", first);
			message.AddString("last", fLastName->Text());
			break;
		}
	}
	_SetError("");
	_SetBusy(true);
	be_app->PostMessage(&message);
}


void
LoginWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgOpenApiSite:
			_OpenApiSite();
			break;
		case kMsgApiKeyEdited:
			_PasteApiKey();
			break;

		case kMsgSubmitApiKey:
		case kMsgSubmitPhone:
		case kMsgSubmitCode:
		case kMsgSubmitPassword:
		case kMsgSubmitName:
		case kMsgSubmitQr:
			_Submit(message->what);
			break;
		case kMsgUsePhone:
			// TDLib takes no phone number while it shows QR codes: the app
			// starts the login over when one is entered.
			fPreferPhone = true;
			_SetError("");
			_ShowCard(kCardPhone);
			break;

		case kMsgAuthState:
		{
			pp::AuthStep step = (pp::AuthStep)message->GetInt32("step", 0);
			_SetError("");
			switch (step) {
				case pp::AuthStep::NeedApiKey:
					_ShowCard(kCardApiKey);
					// The page where the key is made, the first time only.
					if (!fApiSiteOpened)
						_OpenApiSite();
					break;
				case pp::AuthStep::Phone:
					fPreferPhone = false;
					_ShowCard(kCardPhone);
					break;
				case pp::AuthStep::Code:
				{
					BString info("The code was sent ");
					info << message->GetString("code_info", "by Telegram") << ".";
					SetWrappedText(fCodeInfo, info, 2);
					fCode->SetText("");
					_ShowCard(kCardCode);
					break;
				}
				case pp::AuthStep::Password:
				{
					BString info("This account is protected with a password.");
					const char* hint = message->GetString("hint", "");
					if (hint[0] != '\0')
						info << "\nHint: " << hint;
					SetWrappedText(fPasswordInfo, info, 2);
					fPassword->SetText("");
					_ShowCard(kCardPassword);
					break;
				}
				case pp::AuthStep::Registration:
					_ShowCard(kCardName);
					break;
				case pp::AuthStep::Email:
					SetWrappedText(fMessageText, "Telegram wants an e-mail address "
						"for this account. Powergram cannot do that yet: "
						"log in once with another Telegram app and try again.");
					_ShowCard(kCardMessage);
					break;
				case pp::AuthStep::OtherDevice:
					// A new code every 30 s or so; the card only if it's
					// still the way chosen.
					fQr->SetLink(message->GetString("link", ""));
					if (!fPreferPhone && fCard != kCardQr)
						_ShowCard(kCardQr);
					break;
				case pp::AuthStep::Unsupported:
				{
					BString text("Telegram asked for a login step Powergram "
						"does not know: ");
					text << message->GetString("td_type", "?");
					SetWrappedText(fMessageText, text);
					_ShowCard(kCardMessage);
					break;
				}
				default:
					SetWrappedText(fMessageText, "Connecting to Telegram…");
	SetWrappedText(fCodeInfo, "", 2);
	SetWrappedText(fPasswordInfo, "", 2);
					_ShowCard(kCardMessage);
					break;
			}
			break;
		}

		case kMsgAuthError:
			_SetError(message->GetString("text", ""));
			_SetBusy(false);
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


bool
LoginWindow::QuitRequested()
{
	// Closing the login window quits; the app shuts TDLib down first.
	be_app->PostMessage(B_QUIT_REQUESTED);
	return false;
}
