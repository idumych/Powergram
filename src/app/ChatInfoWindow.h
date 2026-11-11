// SPDX-License-Identifier: GPL-3.0-or-later
// A chat's info: its picture, name and status, the bio or description,
// the members and the latest shared photos. Filled by kMsgChatInfo from
// the app; clicking a photo opens it like one in the chat.
#pragma once

#include <Window.h>

class AvatarView;
class BListView;
class BScrollView;
class BStringView;
class BTextView;
class PhotoGrid;

class ChatInfoWindow : public BWindow {
public:
	explicit ChatInfoWindow(int64 chatId);

	int64 ChatId() const { return fChatId; }
	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;

private:
	void _Update(const BMessage* info);

	int64 fChatId;
	bool fSized = false;
	AvatarView* fAvatar;
	BStringView* fTitle;
	BStringView* fStatus;
	BStringView* fAboutLabel;
	BTextView* fAbout;
	BStringView* fMembersLabel;
	BListView* fMembers;
	BScrollView* fMembersScroll;
	BStringView* fPhotosLabel;
	PhotoGrid* fPhotos;
	BScrollView* fPhotosScroll;
};
