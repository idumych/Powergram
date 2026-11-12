// SPDX-License-Identifier: GPL-3.0-or-later
// "Send files": what's about to be sent (a thumbnail for pictures, the
// type's icon for other files) and a caption, before anything goes. Files
// chosen in the file panel, dropped on the chat, dropped on the Deskbar
// icon or pasted (pictures and files from the clipboard) all come here.
#pragma once

#include <Messenger.h>
#include <String.h>
#include <Window.h>

class BButton;
class BListView;
class BStringView;
class BTextControl;

class SendFilesWindow : public BWindow {
public:
	// `send` (kept): a kMsgSendFiles message with "refs" (and "chat_id",
	// "to_chat_id", "reply_to"); posted to `target` with "caption" added.
	// Files with "pasted" set are the window's own and deleted if it's
	// cancelled.
	SendFilesWindow(BMessenger target, BMessage* send, const char* caption,
		BRect parentFrame);
	~SendFilesWindow() override;

	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;

private:
	void _Check();

	BMessenger fTarget;
	BMessage* fSend;
	BListView* fList;
	BTextControl* fCaption;
	BStringView* fProblem;
	BButton* fSendButton;
	bool fSent = false;
};

// Files to send from the clipboard: pictures (raw PNG, JPEG… data, or a
// bitmap as ShowImage copies it) saved into `folder`, and files copied in
// Tracker, as "refs" in `refs`, with "pasted" (bool) set when files were
// made. False if the clipboard holds none (paste its text instead).
bool ClipboardFiles(const char* folder, BMessage* refs);

// Pasted pictures older than a day, left from earlier runs, go.
void CleanPastedFiles(const char* folder);
