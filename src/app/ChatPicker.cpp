// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChatPicker.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <ScrollView.h>
#include <TextControl.h>

namespace {

enum {
	kMsgSearchChanged = 'srch',
	kMsgChoose = 'chse',
	kMsgSelected = 'csel'
};

} // namespace


ChatPicker::ChatPicker(BWindow* parent, const std::vector<Entry>& chats, BMessage* reply,
	const char* title, const char* button)
	:
	// Modeless, as the HIG asks: skipping it loses nothing.
	BWindow(BRect(0, 0, 320, 400), title, B_TITLED_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL, B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fTarget(parent),
	fReply(reply),
	fChats(chats)
{
	fSearch = new BTextControl("search", "Search:", "", nullptr);
	fSearch->SetModificationMessage(new BMessage(kMsgSearchChanged));
	fList = new BListView("chats");
	fList->SetInvocationMessage(new BMessage(kMsgChoose));
	fList->SetSelectionMessage(new BMessage(kMsgSelected));
	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, true);
	float em = be_plain_font->Size();
	scroll->SetExplicitMinSize(BSize(em * 22, em * 22));
	fForward = new BButton("forward", button, new BMessage(kMsgChoose));
	fForward->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fSearch)
		.Add(scroll, 1.0f)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)))
			.Add(fForward)
		.End();
	SetDefaultButton(fForward);
	_Filter();
	fSearch->MakeFocus(true);
	CenterIn(parent->Frame());
}


ChatPicker::~ChatPicker()
{
	delete fReply;
}


void
ChatPicker::_Filter()
{
	while (BListItem* item = fList->RemoveItem((int32)0))
		delete item;
	fShown.clear();
	BString query(fSearch->Text());
	for (const Entry& chat : fChats) {
		if (!query.IsEmpty() && chat.title.IFindFirst(query) < 0)
			continue;
		fList->AddItem(new BStringItem(chat.title));
		fShown.push_back(chat.id);
	}
	if (!fShown.empty())
		fList->Select(0);
	fForward->SetEnabled(fList->CurrentSelection() >= 0);
}


void
ChatPicker::_Choose()
{
	int32 index = fList->CurrentSelection();
	if (index < 0 || index >= (int32)fShown.size())
		return;
	fReply->AddInt64("to_chat_id", fShown[index]);
	fTarget.SendMessage(fReply);
	Quit();
}


void
ChatPicker::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSearchChanged:
			_Filter();
			break;
		case kMsgSelected:
			fForward->SetEnabled(fList->CurrentSelection() >= 0);
			break;
		case kMsgChoose:
			_Choose();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}
