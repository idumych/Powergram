// SPDX-License-Identifier: GPL-3.0-or-later
#include "NewChatWindow.h"

#include <Alert.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <MessageRunner.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include <cmath>
#include <string>

#include "../core/AuthFlow.h"
#include "../core/Contacts.h"
#include "Messages.h"

namespace {

enum {
	kMsgSearchChanged = 'nsch',
	kMsgSearchNow = 'nsnw',
	kMsgSelected = 'nsel',
	kMsgOpen = 'nopn',
	kMsgRemove = 'nrmv',
	kMsgRemoveConfirmed = 'nrmc',
	kMsgEdited = 'nedt',
	kMsgToggle = 'ntgl',
	kMsgCreate = 'ncre'
};

// Public chats are looked for once the typing stops, with this much text.
const bigtime_t kSearchDelay = 400000;
const int32 kSearchMinLength = 4;


// A person or chat: the name on the first line, the detail under it;
// with `checkable`, a box in front that's ticked when chosen.
class PersonItem : public BListItem {
public:
	PersonItem(const PersonRow& row, bool isChat, bool checkable = false)
		: fRow(row), fChat(isChat), fCheckable(checkable) {}

	int64 Id() const { return fRow.id; }
	bool IsChat() const { return fChat; }
	void SetChecked(bool checked) { fChecked = checked; }
	bool IsChecked() const { return fChecked; }

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
		if (fCheckable) {
			float side = ceilf(line * 0.9f);
			BRect box(x, frame.top + (frame.Height() - side) / 2, x + side - 1,
				frame.top + (frame.Height() - side) / 2 + side - 1);
			owner->SetHighUIColor(B_CONTROL_BACKGROUND_COLOR);
			owner->FillRect(box);
			owner->SetHighUIColor(B_CONTROL_BORDER_COLOR);
			owner->StrokeRect(box);
			if (fChecked) {
				owner->SetHighUIColor(B_CONTROL_MARK_COLOR);
				owner->SetPenSize(2);
				box.InsetBy(3, 3);
				owner->StrokeLine(BPoint(box.left, box.top + box.Height() * 0.55f),
					BPoint(box.left + box.Width() * 0.4f, box.bottom));
				owner->StrokeLine(BPoint(box.left + box.Width() * 0.4f, box.bottom),
					BPoint(box.right, box.top));
				owner->SetPenSize(1);
			}
			x += side + 8;
		}
		float width = frame.right - x - 6;
		float y = frame.top + (frame.Height() - 2 * line) / 2 + fh.ascent;
		BString name(fRow.name);
		be_bold_font->TruncateString(&name, B_TRUNCATE_END, width);
		owner->SetFont(be_bold_font);
		owner->SetHighUIColor(selected ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
		owner->DrawString(name.String(), BPoint(x, y));
		BString detail(fRow.detail);
		be_plain_font->TruncateString(&detail, B_TRUNCATE_END, width);
		owner->SetFont(be_plain_font);
		owner->SetHighUIColor(selected ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR,
			selected ? B_NO_TINT : B_DARKEN_2_TINT);
		owner->DrawString(detail.String(), BPoint(x, y + line));
	}

private:
	PersonRow fRow;
	bool fChat;
	bool fCheckable;
	bool fChecked = false;
};


// A heading between the contacts and the public chats; not selectable.
class HeadingItem : public BStringItem {
public:
	explicit HeadingItem(const char* text) : BStringItem(text) { SetEnabled(false); }

	void DrawItem(BView* owner, BRect frame, bool) override
	{
		owner->SetHighUIColor(B_LIST_BACKGROUND_COLOR);
		owner->FillRect(frame);
		owner->SetFont(be_bold_font);
		owner->SetHighUIColor(B_LIST_ITEM_TEXT_COLOR, B_DARKEN_1_TINT);
		font_height fh;
		be_bold_font->GetHeight(&fh);
		owner->DrawString(Text(), BPoint(frame.left + 6, frame.bottom - fh.descent - 2));
		owner->SetFont(be_plain_font);
	}
};


std::vector<PersonRow>
Rows(const BMessage* message)
{
	std::vector<PersonRow> rows;
	int64 id;
	for (int32 i = 0; message->FindInt64("id", i, &id) == B_OK; i++)
		rows.push_back({id, message->GetString("name", i, ""), message->GetString("detail", i, ""),
			message->GetString("username", i, ""), message->GetString("phone", i, "")});
	return rows;
}


// The same matching as the app's contacts: any word of the name, the
// username, the phone number's digits, any case.
bool
RowMatches(const PersonRow& row, const BString& query)
{
	pp::Contact c;
	c.firstName = row.name.String();
	c.username = row.username.String();
	c.phone = row.phone.String();
	return pp::ContactMatches(c, query.String());
}


// The members list: a click on a row, or Space on the selected one, ticks
// or unticks it; arrow keys only move.
class CheckList : public BListView {
public:
	CheckList() : BListView("members") {}

	void MouseDown(BPoint where) override
	{
		BListView::MouseDown(where);
		int32 index = IndexOf(where);
		if (index >= 0)
			_Toggle(index);
	}

	void KeyDown(const char* bytes, int32 count) override
	{
		if (count == 1 && bytes[0] == B_SPACE && CurrentSelection() >= 0)
			_Toggle(CurrentSelection());
		else
			BListView::KeyDown(bytes, count);
	}

private:
	void _Toggle(int32 index)
	{
		BMessage toggle(kMsgToggle);
		toggle.AddInt32("index", index);
		Window()->PostMessage(&toggle);
	}
};


// What the server said went wrong, in an alert (it can be long).
void
ShowFailure(const BString& text)
{
	BAlert* alert = new BAlert("failed", text, "OK", nullptr, nullptr, B_WIDTH_AS_USUAL,
		B_STOP_ALERT);
	alert->Go(nullptr);
}

} // namespace


// #pragma mark - NewChatWindow


NewChatWindow::NewChatWindow(BMessenger app, BRect parentFrame)
	:
	BWindow(BRect(0, 0, 360, 460), "New chat", B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fApp(app)
{
	fSearch = new BTextControl("search", "Search:", "", nullptr);
	fSearch->SetModificationMessage(new BMessage(kMsgSearchChanged));
	fSearch->SetToolTip("A name or phone number among your contacts, or a public "
		"chat's name or @username");
	fList = new BListView("people");
	fList->SetInvocationMessage(new BMessage(kMsgOpen));
	fList->SetSelectionMessage(new BMessage(kMsgSelected));
	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, true);
	float em = be_plain_font->Size();
	scroll->SetExplicitMinSize(BSize(em * 24, em * 24));
	fStatus = new BStringView("status", "Loading contacts" B_UTF8_ELLIPSIS);
	fStatus->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
	fOpen = new BButton("open", "Open", new BMessage(kMsgOpen));
	fOpen->SetEnabled(false);
	fRemove = new BButton("remove", "Remove contact", new BMessage(kMsgRemove));
	fRemove->SetEnabled(false);

	BMessage* group = new BMessage(kMsgShowNewChat);
	group->AddString("what", "group");
	BMessage* channel = new BMessage(kMsgShowNewChat);
	channel->AddString("what", "channel");
	BMessage* contact = new BMessage(kMsgShowNewChat);
	contact->AddString("what", "contact");
	BButton* groupButton = new BButton("new group", "New group" B_UTF8_ELLIPSIS, group);
	BButton* channelButton = new BButton("new channel", "New channel" B_UTF8_ELLIPSIS, channel);
	BButton* contactButton = new BButton("add contact", "Add contact" B_UTF8_ELLIPSIS, contact);
	for (BButton* button : {groupButton, channelButton, contactButton})
		button->SetTarget(fApp);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(groupButton)
			.Add(channelButton)
			.Add(contactButton)
			.AddGlue()
		.End()
		.Add(fSearch)
		.Add(scroll, 1.0f)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.Add(fRemove)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)))
			.Add(fOpen)
		.End();
	SetDefaultButton(fOpen);
	fSearch->MakeFocus(true);
	CenterIn(parentFrame);
}


NewChatWindow::~NewChatWindow()
{
	delete fSearchRunner;
}


bool
NewChatWindow::QuitRequested()
{
	fApp.SendMessage(kMsgNewChatClosed);
	return true;
}


void
NewChatWindow::_Fill()
{
	// Keep what was selected, if it's still there.
	int64 selectedId = 0;
	bool selectedChat = false;
	if (PersonItem* item = dynamic_cast<PersonItem*>(fList->ItemAt(fList->CurrentSelection()))) {
		selectedId = item->Id();
		selectedChat = item->IsChat();
	}
	while (BListItem* item = fList->RemoveItem((int32)0))
		delete item;
	BString query(fSearch->Text());
	query.Trim();
	int32 contacts = 0;
	for (const PersonRow& row : fContacts) {
		if (!RowMatches(row, query))
			continue;
		fList->AddItem(new PersonItem(row, false));
		contacts++;
	}
	bool publicShown = !query.IsEmpty() && fPublicQuery == query && !fPublic.empty();
	if (publicShown) {
		fList->AddItem(new HeadingItem("Public chats and channels"));
		for (const PersonRow& row : fPublic)
			fList->AddItem(new PersonItem(row, true));
	}
	int32 select = -1;
	for (int32 i = 0; i < fList->CountItems(); i++) {
		PersonItem* item = dynamic_cast<PersonItem*>(fList->ItemAt(i));
		if (item == nullptr)
			continue;
		if (select < 0)
			select = i;
		if (item->Id() == selectedId && item->IsChat() == selectedChat) {
			select = i;
			break;
		}
	}
	if (select >= 0)
		fList->Select(select);
	if (!fLoaded)
		fStatus->SetText("Loading contacts" B_UTF8_ELLIPSIS);
	else if (fContacts.empty() && query.IsEmpty())
		fStatus->SetText("No contacts yet: add one, or search for a public chat.");
	else if (contacts == 0 && !publicShown && !query.IsEmpty()) {
		bool searching = fSearchRunner != nullptr || (fPublicQuery != query
			&& query.CountChars() >= kSearchMinLength);
		fStatus->SetText(searching ? "Searching" B_UTF8_ELLIPSIS : "Nothing found.");
	} else
		fStatus->SetText("");
	PostMessage(kMsgSelected);
}


void
NewChatWindow::_Search()
{
	delete fSearchRunner;
	fSearchRunner = nullptr;
	BString query(fSearch->Text());
	query.Trim();
	BString bare(query);
	if (bare.StartsWith("@"))
		bare.Remove(0, 1);
	if (bare.CountChars() < kSearchMinLength || bare.FindFirst(' ') >= 0
		|| query.FindFirst("+") == 0) {
		fPublic.clear();
		fPublicQuery = query;
		_Fill();
		return;
	}
	BMessage search(kMsgNewChatSearch);
	search.AddString("query", query);
	search.AddInt32("seq", ++fSearchSeq);
	fApp.SendMessage(&search, this);
}


void
NewChatWindow::_Open()
{
	PersonItem* item = dynamic_cast<PersonItem*>(fList->ItemAt(fList->CurrentSelection()));
	if (item == nullptr)
		return;
	BMessage open(kMsgNewChatOpen);
	open.AddInt64(item->IsChat() ? "chat_id" : "user_id", item->Id());
	fApp.SendMessage(&open);
	PostMessage(B_QUIT_REQUESTED);
}


void
NewChatWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgNewChatContacts:
			fContacts = Rows(message);
			fLoaded = message->GetBool("loaded", true);
			_Fill();
			break;

		case kMsgNewChatFound:
		{
			if (message->GetInt32("seq", 0) != fSearchSeq)
				break;
			fPublic = Rows(message);
			fPublicQuery = message->GetString("query", "");
			_Fill();
			break;
		}

		case kMsgSearchChanged:
		{
			delete fSearchRunner;
			BMessage now(kMsgSearchNow);
			fSearchRunner = new BMessageRunner(BMessenger(this), &now, kSearchDelay, 1);
			_Fill();
			break;
		}
		case kMsgSearchNow:
			_Search();
			break;

		case kMsgSelected:
		{
			PersonItem* item = dynamic_cast<PersonItem*>(
				fList->ItemAt(fList->CurrentSelection()));
			fOpen->SetEnabled(item != nullptr);
			fRemove->SetEnabled(item != nullptr && !item->IsChat());
			break;
		}
		case kMsgOpen:
			_Open();
			break;

		case kMsgRemove:
		{
			PersonItem* item = dynamic_cast<PersonItem*>(
				fList->ItemAt(fList->CurrentSelection()));
			if (item == nullptr || item->IsChat())
				break;
			BString name;
			for (const PersonRow& row : fContacts) {
				if (row.id == item->Id())
					name = row.name;
			}
			BString text("Remove ");
			text << name << " from your contacts? Your chat with them stays.";
			BAlert* alert = new BAlert("remove", text, "Cancel", "Remove", nullptr,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			BMessage* confirmed = new BMessage(kMsgRemoveConfirmed);
			confirmed->AddInt64("user_id", item->Id());
			alert->Go(new BInvoker(confirmed, this));
			break;
		}
		case kMsgRemoveConfirmed:
		{
			if (message->GetInt32("which", 0) != 1)
				break;
			BMessage remove(kMsgRemoveContact);
			remove.AddInt64("user_id", message->GetInt64("user_id", 0));
			fApp.SendMessage(&remove);
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


// #pragma mark - NewGroupWindow


NewGroupWindow::NewGroupWindow(BMessenger app, bool channel,
	const std::vector<PersonRow>& contacts, BRect parentFrame)
	:
	BWindow(BRect(0, 0, 360, 460), channel ? "New channel" : "New group",
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fApp(app),
	fChannel(channel),
	fContacts(contacts)
{
	fTitle = new BTextControl("title", channel ? "Channel name:" : "Group name:", "", nullptr);
	fTitle->SetModificationMessage(new BMessage(kMsgEdited));
	fFilter = new BTextControl("filter", "Find:", "", nullptr);
	fFilter->SetModificationMessage(new BMessage(kMsgSearchChanged));
	fList = new CheckList();
	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, true);
	float em = be_plain_font->Size();
	scroll->SetExplicitMinSize(BSize(em * 24, em * 18));
	fCount = new BStringView("count", "");
	fCount->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
	fProblem = new BStringView("problem", "");
	fProblem->SetHighUIColor(B_FAILURE_COLOR);
	fCreate = new BButton("create", "Create", new BMessage(kMsgCreate));
	BStringView* membersLabel = new BStringView("members label",
		channel ? "Subscribers to add (you can invite more later):"
			: "Members to add (you can add more later):");

	BLayoutBuilder::Group<> layout(this, B_VERTICAL);
	layout.SetInsets(B_USE_WINDOW_INSETS).Add(fTitle);
	if (channel) {
		fDescription = new BTextView("description");
		fDescription->SetWordWrap(true);
		fDescription->SetStylable(false);
		BScrollView* descScroll = new BScrollView("description scroll", fDescription, 0,
			false, false);
		descScroll->SetExplicitMinSize(BSize(em * 24, em * 4));
		descScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, em * 5));
		layout.Add(new BStringView("description label", "Description (optional):"))
			.Add(descScroll);
	}
	layout.Add(membersLabel)
		.Add(fFilter)
		.Add(scroll, 1.0f)
		.Add(fCount)
		.Add(fProblem)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)))
			.Add(fCreate)
		.End();
	SetDefaultButton(fCreate);
	_Fill();
	_Check();
	fTitle->MakeFocus(true);
	CenterIn(parentFrame);
}


void
NewGroupWindow::_Fill()
{
	while (BListItem* item = fList->RemoveItem((int32)0))
		delete item;
	BString query(fFilter->Text());
	query.Trim();
	for (const PersonRow& row : fContacts) {
		if (!RowMatches(row, query))
			continue;
		PersonItem* item = new PersonItem(row, false, true);
		item->SetChecked(fChosen.count(row.id) > 0);
		fList->AddItem(item);
	}
	_SetCount();
}


void
NewGroupWindow::_SetCount()
{
	BString count;
	if (fContacts.empty())
		count = "You have no contacts to add yet.";
	else if (fChosen.empty())
		count = "Click people to add them.";
	else
		count << fChosen.size() << (fChosen.size() == 1 ? " person chosen" : " people chosen");
	fCount->SetText(count);
}


void
NewGroupWindow::_Check()
{
	std::string problem = pp::ChatTitleProblem(fTitle->Text());
	if (problem.empty() && fDescription != nullptr)
		problem = pp::ChatDescriptionProblem(fDescription->Text());
	// No complaint about an empty name before anything was typed.
	bool empty = BString(fTitle->Text()).Trim().IsEmpty();
	fProblem->SetText(empty || fBusy ? "" : problem.c_str());
	fCreate->SetEnabled(problem.empty() && !fBusy);
}


void
NewGroupWindow::_Create()
{
	_Check();
	if (!fCreate->IsEnabled())
		return;
	BMessage create(kMsgCreateGroup);
	create.AddString("title", fTitle->Text());
	create.AddString("description", fDescription != nullptr ? fDescription->Text() : "");
	create.AddBool("channel", fChannel);
	for (int64 id : fChosen)
		create.AddInt64("user_id", id);
	fBusy = true;
	fCreate->SetLabel("Creating" B_UTF8_ELLIPSIS);
	_Check();
	fApp.SendMessage(&create, this);
}


void
NewGroupWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgEdited:
			_Check();
			break;
		case kMsgSearchChanged:
			_Fill();
			break;
		case kMsgToggle:
		{
			int32 index = message->GetInt32("index", -1);
			PersonItem* item = dynamic_cast<PersonItem*>(fList->ItemAt(index));
			if (item == nullptr)
				break;
			if (!fChosen.erase(item->Id()))
				fChosen.insert(item->Id());
			item->SetChecked(fChosen.count(item->Id()) > 0);
			fList->InvalidateItem(index);
			_SetCount();
			break;
		}
		case kMsgCreate:
			_Create();
			break;
		case kMsgCreateResult:
		{
			BString text = message->GetString("text", "");
			if (text.IsEmpty()) {
				Quit();
				return;
			}
			fBusy = false;
			fCreate->SetLabel("Create");
			_Check();
			ShowFailure(text);
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}


// #pragma mark - AddContactWindow


AddContactWindow::AddContactWindow(BMessenger app, BRect parentFrame)
	:
	BWindow(BRect(0, 0, 320, 160), "Add contact", B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fApp(app)
{
	fPhone = new BTextControl("phone", "Phone number:", "", nullptr);
	fPhone->SetToolTip("With the country code, e.g. +44 20 7946 0000");
	fFirst = new BTextControl("first", "First name:", "", nullptr);
	fLast = new BTextControl("last", "Last name:", "", nullptr);
	for (BTextControl* control : {fPhone, fFirst, fLast})
		control->SetModificationMessage(new BMessage(kMsgEdited));
	fAdd = new BButton("add", "Add", new BMessage(kMsgCreate));
	float em = be_plain_font->Size();
	fPhone->TextView()->SetExplicitMinSize(BSize(em * 14, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGrid(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
			.AddTextControl(fPhone, 0, 0)
			.AddTextControl(fFirst, 0, 1)
			.AddTextControl(fLast, 0, 2)
		.End()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)))
			.Add(fAdd)
		.End();
	SetDefaultButton(fAdd);
	_Check();
	fPhone->MakeFocus(true);
	CenterIn(parentFrame);
}


void
AddContactWindow::_Check()
{
	std::string digits = pp::NormalizePhone(fPhone->Text());
	bool ok = digits.size() >= 6 && !BString(fFirst->Text()).Trim().IsEmpty();
	fAdd->SetEnabled(ok && !fBusy);
}


void
AddContactWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgEdited:
			_Check();
			break;
		case kMsgCreate:
		{
			_Check();
			if (!fAdd->IsEnabled())
				break;
			BMessage add(kMsgAddContact);
			add.AddString("phone", fPhone->Text());
			add.AddString("first", fFirst->Text());
			add.AddString("last", fLast->Text());
			fBusy = true;
			_Check();
			fApp.SendMessage(&add, this);
			break;
		}
		case kMsgCreateResult:
		{
			BString text = message->GetString("text", "");
			if (text.IsEmpty()) {
				Quit();
				return;
			}
			fBusy = false;
			_Check();
			ShowFailure(text);
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}
