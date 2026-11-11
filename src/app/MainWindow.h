// SPDX-License-Identifier: GPL-3.0-or-later
// The main window, laid out like Paper Plane's: the chat list on the left,
// the open chat on the right (header, messages, entry).
#pragma once

#include <Window.h>

#include <map>
#include <string>
#include <vector>

class BBitmap;
class BButton;
class BFilePanel;
class EmojiWindow;
class StickerWindow;
class BListView;
class BStringView;
class BTextControl;
class MessageView;

class MainWindow : public BWindow {
public:
	MainWindow(BRect frame, bool testDc, const std::vector<std::string>& recentEmoji);

	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;
	void WindowActivated(bool active) override;

private:
	void _UpdateChats(const BMessage* list);
	void _UpdateHistory(const BMessage* history);
	void _SelectionChanged();
	void _Send();
	void _ShowMessageMenu(const BMessage* properties);
	void _StartReply(int64 messageId);
	void _StartEdit(int64 messageId);
	void _CancelCompose();
	void _AskDelete(int64 messageId, bool canSelf, bool canAll);
	void _SelectChat(int64 chatId);
	void _SendFiles(const BMessage* message);
	BBitmap* _Avatar(const BMessage* list, int32 index);
	BButton* _IconButton(const char* name, int kind, uint32 what, const char* tip);
	void _InsertEmoji(const char* emoji);

	BListView* fList;
	BStringView* fTitle;
	MessageView* fMessages;
	BTextControl* fEntry;
	BButton* fSendButton;
	BButton* fAttachButton;
	BButton* fEmojiButton;
	BButton* fStickerButton;
	EmojiWindow* fEmojiWindow;
	StickerWindow* fStickerWindow;
	std::vector<std::string> fRecentEmoji;
	BFilePanel* fFilePanel;
	std::map<std::string, BBitmap*> fAvatars;   // round chat photos by path
	BView* fComposeBar;         // "Reply to …" / "Edit message", over the entry
	BStringView* fComposeLabel;
	int64 fOpenChat;
	int64 fReplyTo;
	int64 fEditing;
	BPoint fMenuWhere;
};
