// SPDX-License-Identifier: GPL-3.0-or-later
// Starting chats: "New chat" lists your contacts and finds public chats
// and channels as you type; from it, "New group", "New channel" and "Add
// contact" open windows of their own. All of them only talk to the app by
// messages (Messages.h).
#pragma once

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <set>
#include <vector>

class BButton;
class BListView;
class BMessageRunner;
class BStringView;
class BTextControl;
class BTextView;

// A person or chat as the app sends it: an id, a name, a second line, and
// for people the username and phone number to search by.
struct PersonRow {
	int64 id;
	BString name;
	BString detail;
	BString username;
	BString phone;
};

class NewChatWindow : public BWindow {
public:
	NewChatWindow(BMessenger app, BRect parentFrame);
	~NewChatWindow() override;

	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;

private:
	void _Fill();
	void _Open();
	void _Search();

	BMessenger fApp;
	BTextControl* fSearch;
	BListView* fList;
	BButton* fOpen;
	BButton* fRemove;
	BStringView* fStatus;
	BMessageRunner* fSearchRunner = nullptr;
	std::vector<PersonRow> fContacts;
	std::vector<PersonRow> fPublic;      // found for fPublicQuery
	BString fPublicQuery;
	int32 fSearchSeq = 0;
	bool fLoaded = false;
};

// "New group" and "New channel": a name (and a channel's description), and
// the contacts to add, ticked in a list.
class NewGroupWindow : public BWindow {
public:
	NewGroupWindow(BMessenger app, bool channel, const std::vector<PersonRow>& contacts,
		BRect parentFrame);

	void MessageReceived(BMessage* message) override;

private:
	void _Fill();
	void _SetCount();
	void _Check();
	void _Create();

	BMessenger fApp;
	bool fChannel;
	std::vector<PersonRow> fContacts;
	std::set<int64> fChosen;
	BTextControl* fTitle;
	BTextView* fDescription = nullptr;
	BTextControl* fFilter;
	BListView* fList;
	BStringView* fCount;
	BStringView* fProblem;
	BButton* fCreate;
	bool fBusy = false;
};

// "Add contact": a phone number and a name.
class AddContactWindow : public BWindow {
public:
	AddContactWindow(BMessenger app, BRect parentFrame);

	void MessageReceived(BMessage* message) override;

private:
	void _Check();

	BMessenger fApp;
	BTextControl* fPhone;
	BTextControl* fFirst;
	BTextControl* fLast;
	BButton* fAdd;
	bool fBusy = false;
};
