// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProfileWindow.h"

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>

#include <cmath>
#include <string>

#include "../core/ChatList.h"
#include "../core/Profile.h"
#include "AvatarView.h"
#include "Messages.h"

namespace {

enum {
	kMsgEdited = 'pedt',
	kMsgSave = 'psav',
	kMsgRevert = 'prev',
	kMsgChangePhoto = 'pcph',
	kMsgPhotoChosen = 'pphc',
	kMsgRemovePhoto = 'prmp',
	kMsgSessionSelected = 'pssl',
	kMsgLogOutDevice = 'plgo',
	kMsgLogOutAllOthers = 'plgx',
	kMsgConfirmed = 'pcnf'
};


// A device: what it runs on the first line, where and when on the second.
class SessionItem : public BListItem {
public:
	SessionItem(int64 id, const BString& title, const BString& detail, bool current)
		: fId(id), fTitle(title), fDetail(detail), fCurrent(current) {}

	int64 Id() const { return fId; }
	bool IsCurrent() const { return fCurrent; }

	void Update(BView* owner, const BFont* font) override
	{
		BListItem::Update(owner, font);
		font_height fh;
		font->GetHeight(&fh);
		SetHeight(ceilf((fh.ascent + fh.descent + fh.leading) * 2.4f));
	}

	void DrawItem(BView* owner, BRect frame, bool) override
	{
		bool selected = IsSelected();
		owner->SetHighUIColor(selected ? B_LIST_SELECTED_BACKGROUND_COLOR
			: B_LIST_BACKGROUND_COLOR);
		owner->FillRect(frame);
		font_height fh;
		be_plain_font->GetHeight(&fh);
		float line = ceilf(fh.ascent + fh.descent + fh.leading);
		float x = frame.left + 6;
		float y = frame.top + (frame.Height() - 2 * line) / 2 + fh.ascent;
		BString title(fTitle);
		be_bold_font->TruncateString(&title, B_TRUNCATE_END, frame.Width() - 12);
		owner->SetFont(be_bold_font);
		owner->SetHighUIColor(selected ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
		owner->DrawString(title.String(), BPoint(x, y));
		BString detail(fDetail);
		be_plain_font->TruncateString(&detail, B_TRUNCATE_END, frame.Width() - 12);
		owner->SetFont(be_plain_font);
		owner->SetHighUIColor(selected ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR,
			selected ? B_NO_TINT : B_DARKEN_2_TINT);
		owner->DrawString(detail.String(), BPoint(x, y + line));
	}

private:
	int64 fId;
	BString fTitle;
	BString fDetail;
	bool fCurrent;
};

} // namespace


ProfileWindow::ProfileWindow(BMessenger app)
	:
	BWindow(BRect(0, 0, 400, 500), "Profile", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fApp(app)
{
	fAvatar = new AvatarView(5);
	fChangePhoto = new BButton("change photo", "Change photo" B_UTF8_ELLIPSIS,
		new BMessage(kMsgChangePhoto));
	fRemovePhoto = new BButton("remove photo", "Remove photo", new BMessage(kMsgRemovePhoto));
	fRemovePhoto->SetEnabled(false);
	fFirst = new BTextControl("first", "First name:", "", nullptr);
	fLast = new BTextControl("last", "Last name:", "", nullptr);
	fBio = new BTextControl("bio", "Bio:", "", nullptr);
	for (BTextControl* control : {fFirst, fLast, fBio})
		control->SetModificationMessage(new BMessage(kMsgEdited));
	fUsername = new BStringView("username", "");
	fUsername->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
	fBioCount = new BStringView("bio count", "");
	fBioCount->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
	fProblem = new BStringView("problem", "");
	fProblem->SetHighUIColor(B_FAILURE_COLOR);
	fSave = new BButton("save", "Save", new BMessage(kMsgSave));
	fRevert = new BButton("revert", "Revert", new BMessage(kMsgRevert));
	fSave->SetEnabled(false);
	fRevert->SetEnabled(false);

	fSessions = new BListView("sessions");
	fSessions->SetSelectionMessage(new BMessage(kMsgSessionSelected));
	BScrollView* sessionScroll = new BScrollView("session scroll", fSessions, 0, false, true);
	float em = be_plain_font->Size();
	sessionScroll->SetExplicitMinSize(BSize(em * 32, em * 12));
	fLogOut = new BButton("log out", "Log out device", new BMessage(kMsgLogOutDevice));
	fLogOutOthers = new BButton("log out others", "Log out all other devices",
		new BMessage(kMsgLogOutAllOthers));
	fLogOut->SetEnabled(false);
	fLogOutOthers->SetEnabled(false);
	BStringView* devices = new BStringView("devices", "Logged-in devices");
	devices->SetFont(be_bold_font);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL)
			.Add(fAvatar)
			.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
				.Add(fChangePhoto)
				.Add(fRemovePhoto)
				.AddGlue()
			.End()
			.AddGlue()
		.End()
		.AddGrid(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
			.AddTextControl(fFirst, 0, 0)
			.AddTextControl(fLast, 0, 1)
			.AddTextControl(fBio, 0, 2)
			.Add(fBioCount, 1, 3)
			.Add(fUsername, 1, 4)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fProblem, 1.0f)
			.Add(fRevert)
			.Add(fSave)
		.End()
		.Add(devices)
		.Add(sessionScroll, 1.0f)
		.AddGroup(B_HORIZONTAL)
			.Add(fLogOut)
			.AddGlue()
			.Add(fLogOutOthers)
		.End();
	fFirst->SetEnabled(false);
	fLast->SetEnabled(false);
	fBio->SetEnabled(false);
	fUsername->SetText("Loading" B_UTF8_ELLIPSIS);
	CenterOnScreen();
}


ProfileWindow::~ProfileWindow()
{
	delete fPanel;
}


void
ProfileWindow::_SetProfile(const BMessage* data)
{
	BString first = data->GetString("first", "");
	BString last = data->GetString("last", "");
	BString bio = data->GetString("bio", "");
	BString name(first);
	if (!last.IsEmpty())
		name << " " << last;
	std::string preview = FindBytes(data, "photo_mini", 0);
	fAvatar->Set(data->GetInt64("user_id", 0), data->GetString("photo", ""), preview,
		pp::Initials(name.String()).c_str());
	fRemovePhoto->SetEnabled(data->GetBool("has_photo", false));
	BString username = data->GetString("username", "");
	if (username.IsEmpty())
		fUsername->SetText("No username (set one in a Telegram mobile app)");
	else
		fUsername->SetText(BString("Username: @") << username);

	// Fields the user is editing stay as they are, unless just saved.
	bool edited = fLoaded && (BString(fFirst->Text()) != fSavedFirst
		|| BString(fLast->Text()) != fSavedLast || BString(fBio->Text()) != fSavedBio);
	fSavedFirst = first;
	fSavedLast = last;
	fSavedBio = bio;
	if (!edited || fSaving) {
		fFirst->SetText(first);
		fLast->SetText(last);
		fBio->SetText(bio);
	}
	if (!fLoaded) {
		fLoaded = true;
		fFirst->SetEnabled(true);
		fLast->SetEnabled(true);
		fBio->SetEnabled(true);
	}
	_Changed();
}


void
ProfileWindow::_SetSessions(const BMessage* message)
{
	int64 selected = 0;
	if (SessionItem* item = dynamic_cast<SessionItem*>(fSessions->ItemAt(
			fSessions->CurrentSelection())))
		selected = item->Id();
	while (BListItem* item = fSessions->RemoveItem((int32)0))
		delete item;
	int64 id;
	int32 others = 0;
	for (int32 i = 0; message->FindInt64("session_id", i, &id) == B_OK; i++) {
		bool current = message->GetBool("current", i, false);
		BString title = message->GetString("application", i, "");
		if (current)
			title << " (this device)";
		else if (message->GetBool("unconfirmed", i, false))
			title << " (new login)";
		BString detail = message->GetString("device", i, "");
		BString location = message->GetString("location", i, "");
		BString active = message->GetString("active", i, "");
		if (!location.IsEmpty())
			detail << " \xC2\xB7 " << location;
		if (!active.IsEmpty() && !current)
			detail << " \xC2\xB7 " << active;
		fSessions->AddItem(new SessionItem(id, title, detail, current));
		if (id == selected)
			fSessions->Select(fSessions->CountItems() - 1);
		others += current ? 0 : 1;
	}
	fLogOutOthers->SetEnabled(others > 0);
	PostMessage(kMsgSessionSelected);
}


void
ProfileWindow::_Changed()
{
	BString first(fFirst->Text()), last(fLast->Text()), bio(fBio->Text());
	size_t chars = pp::CharacterCount(bio.String());
	BString count;
	count << (chars <= 70 ? 70 - chars : 0) << " characters left";
	if (chars > 70)
		count = "Too long";
	fBioCount->SetText(count);
	bool changed = first != fSavedFirst || last != fSavedLast || bio != fSavedBio;
	std::string problem = pp::ProfileProblem(first.String(), last.String(), bio.String());
	if (!fSaving)
		fProblem->SetText(changed ? problem.c_str() : "");
	fSave->SetEnabled(fLoaded && changed && problem.empty() && !fSaving);
	fRevert->SetEnabled(changed && !fSaving);
}


void
ProfileWindow::_Save()
{
	BMessage save(kMsgProfileSave);
	save.AddString("first", fFirst->Text());
	save.AddString("last", fLast->Text());
	save.AddString("bio", fBio->Text());
	fApp.SendMessage(&save);
	fSaving = true;
	fProblem->SetText("Saving" B_UTF8_ELLIPSIS);
	_Changed();
}


void
ProfileWindow::_Revert()
{
	fFirst->SetText(fSavedFirst);
	fLast->SetText(fSavedLast);
	fBio->SetText(fSavedBio);
	_Changed();
}


void
ProfileWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgProfileData:
			_SetProfile(message);
			break;
		case kMsgProfileSessions:
			_SetSessions(message);
			break;
		case kMsgProfileResult:
		{
			fSaving = false;
			BString text = message->GetString("text", "");
			_Changed();
			fProblem->SetText(text);
			break;
		}
		case kMsgEdited:
			_Changed();
			break;
		case kMsgSave:
			_Save();
			break;
		case kMsgRevert:
			_Revert();
			break;
		case kMsgChangePhoto:
			if (fPanel == nullptr) {
				fPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), nullptr,
					B_FILE_NODE, false, new BMessage(kMsgPhotoChosen));
				fPanel->Window()->SetTitle(APP_NAME ": Choose a picture");
				fPanel->SetButtonLabel(B_DEFAULT_BUTTON, "Use");
			}
			fPanel->Show();
			break;
		case kMsgPhotoChosen:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK)
				break;
			BMessage photo(kMsgProfilePhoto);
			photo.AddRef("refs", &ref);
			fApp.SendMessage(&photo);
			fProblem->SetText("Uploading the picture" B_UTF8_ELLIPSIS);
			break;
		}
		case kMsgRemovePhoto:
		{
			BAlert* alert = new BAlert(APP_NAME, "Remove your profile picture?",
				"Cancel", "Remove", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			BMessage* confirmed = new BMessage(kMsgConfirmed);
			confirmed->AddInt32("action", kMsgProfileRemovePhoto);
			alert->Go(new BInvoker(confirmed, this));
			break;
		}
		case kMsgSessionSelected:
		{
			SessionItem* item = dynamic_cast<SessionItem*>(fSessions->ItemAt(
				fSessions->CurrentSelection()));
			fLogOut->SetEnabled(item != nullptr && !item->IsCurrent());
			break;
		}
		case kMsgLogOutDevice:
		{
			SessionItem* item = dynamic_cast<SessionItem*>(fSessions->ItemAt(
				fSessions->CurrentSelection()));
			if (item == nullptr || item->IsCurrent())
				break;
			BAlert* alert = new BAlert(APP_NAME, "Log out this device? It will have to "
				"log in again to use your account.", "Cancel", "Log out", nullptr,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			BMessage* confirmed = new BMessage(kMsgConfirmed);
			confirmed->AddInt32("action", kMsgTerminateSession);
			confirmed->AddInt64("session_id", item->Id());
			alert->Go(new BInvoker(confirmed, this));
			break;
		}
		case kMsgLogOutAllOthers:
		{
			BAlert* alert = new BAlert(APP_NAME, "Log out all other devices? Only this "
				"one will stay logged in.", "Cancel", "Log out others", nullptr,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			BMessage* confirmed = new BMessage(kMsgConfirmed);
			confirmed->AddInt32("action", kMsgTerminateOthers);
			alert->Go(new BInvoker(confirmed, this));
			break;
		}
		case kMsgConfirmed:
		{
			if (message->GetInt32("which", 0) != 1)
				break;   // Cancel
			BMessage action(message->GetInt32("action", 0));
			if (action.what == kMsgTerminateSession)
				action.AddInt64("session_id", message->GetInt64("session_id", 0));
			fApp.SendMessage(&action);
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}


bool
ProfileWindow::QuitRequested()
{
	fApp.SendMessage(kMsgProfileClosed);
	return true;
}
