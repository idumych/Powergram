// SPDX-License-Identifier: GPL-3.0-or-later
// "Forward to": a chat list with a search field. Sends the chosen chat back
// to its target as the reply message, with "to_chat_id" added.
#pragma once

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <vector>

class BButton;
class BListView;
class BTextControl;

class ChatPicker : public BWindow {
public:
	struct Entry {
		int64 id;
		BString title;
	};

	// `title` and `button` default to forwarding's.
	ChatPicker(BWindow* parent, const std::vector<Entry>& chats, BMessage* reply,
		const char* title = "Forward to", const char* button = "Forward");
	~ChatPicker() override;

	void MessageReceived(BMessage* message) override;

private:
	void _Filter();
	void _Choose();

	BMessenger fTarget;
	BMessage* fReply;
	std::vector<Entry> fChats;
	std::vector<int64> fShown;    // ids of the list's rows
	BTextControl* fSearch;
	BListView* fList;
	BButton* fForward;
};
