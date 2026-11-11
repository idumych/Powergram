// SPDX-License-Identifier: GPL-3.0-or-later
#include "LoginWindow.h"

#include <Application.h>
#include <Button.h>
#include <CardLayout.h>
#include <GridView.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "../core/AuthFlow.h"
#include "Messages.h"

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

	fApiId = new BTextControl("API ID:", "", nullptr);
	fApiHash = new BTextControl("API hash:", "", nullptr);
	fPhone = new BTextControl("Phone number:", "", nullptr);
	fCode = new BTextControl("Code:", "", nullptr);
	fPassword = new BTextControl("Password:", "", nullptr);
	fPassword->TextView()->HideTyping(true);
	fFirstName = new BTextControl("First name:", "", nullptr);
	fLastName = new BTextControl("Last name:", "", nullptr);

	// Digits only where only digits make sense.
	for (BTextControl* control : {fApiId, fCode}) {
		for (uint32 c = 0; c < 256; c++)
			if (c < '0' || c > '9')
				control->TextView()->DisallowChar(c);
	}

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

	fCards->AddView(_MakeCard("Telegram API key", &apiInfo,
		fields({fApiId, fApiHash}), &fNext[kCardApiKey], kMsgSubmitApiKey));
	fCards->AddView(_MakeCard("Welcome", &phoneInfo,
		fields({fPhone}), &fNext[kCardPhone], kMsgSubmitPhone));
	fCards->AddView(_MakeCard("Enter the verification code", &fCodeInfo,
		fields({fCode}), &fNext[kCardCode], kMsgSubmitCode));
	fCards->AddView(_MakeCard("Enter your password", &fPasswordInfo,
		fields({fPassword}), &fNext[kCardPassword], kMsgSubmitPassword));
	fCards->AddView(_MakeCard("Create an account", &nameInfo,
		fields({fFirstName, fLastName}), &fNext[kCardName], kMsgSubmitName));
	fCards->AddView(_MakeCard("Connecting", &fMessageText, nullptr, nullptr, 0));

	SetWrappedText(apiInfo, "Paper Plane needs an API ID of its own to talk to Telegram. "
		"Register an app at https://my.telegram.org under \"API development "
		"tools\", then enter its ID and hash here, or put them in a text "
		"file ~/config/settings/PaperPlane/api_key (api_id=… and api_hash=… "
		"on two lines) and start Paper Plane again.");
	SetWrappedText(phoneInfo, testDc
		? "Enter the phone number of an account on Telegram's test servers. "
			"Telegram no longer accepts its 99966 test numbers: create a test "
			"account first with Telegram's mobile app (iOS Debug menu, or the "
			"Android beta)."
		: "Enter your phone number with the country code.");
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
	BView* fields, BButton** next, uint32 what)
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
		builder.AddGroup(B_HORIZONTAL)
			.AddGlue()
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
	SetDefaultButton(card == kCardMessage ? nullptr : fNext[card]);

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
LoginWindow::_SetBusy(bool busy)
{
	if (fCard != kCardMessage)
		fNext[fCard]->SetEnabled(!busy);
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
	BMessage message(what);
	switch (what) {
		case kMsgSubmitApiKey:
		{
			int32 id = atoi(fApiId->Text());
			BString hash(fApiHash->Text());
			hash.Trim();
			if (id <= 0 || hash.Length() != 32) {
				_SetError("The API ID is a number and the hash has 32 characters.");
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
			if (fCode->Text()[0] == '\0')
				return;
			message.AddString("text", fCode->Text());
			break;
		case kMsgSubmitPassword:
			message.AddString("text", fPassword->Text());
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
		case kMsgSubmitApiKey:
		case kMsgSubmitPhone:
		case kMsgSubmitCode:
		case kMsgSubmitPassword:
		case kMsgSubmitName:
			_Submit(message->what);
			break;

		case kMsgAuthState:
		{
			pp::AuthStep step = (pp::AuthStep)message->GetInt32("step", 0);
			_SetError("");
			switch (step) {
				case pp::AuthStep::NeedApiKey:
					_ShowCard(kCardApiKey);
					break;
				case pp::AuthStep::Phone:
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
						"for this account. Paper Plane cannot do that yet: "
						"log in once with another Telegram app and try again.");
					_ShowCard(kCardMessage);
					break;
				case pp::AuthStep::OtherDevice:
					SetWrappedText(fMessageText, "Log in with a QR code is not "
						"supported yet.");
					_ShowCard(kCardMessage);
					break;
				case pp::AuthStep::Unsupported:
				{
					BString text("Telegram asked for a login step Paper Plane "
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
