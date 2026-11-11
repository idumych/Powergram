// SPDX-License-Identifier: GPL-3.0-or-later
#include "MentionPopup.h"

#include <Autolock.h>
#include <View.h>

#include <cmath>

#include "Messages.h"

class MentionListView : public BView {
public:
	explicit MentionListView(BMessenger target)
		:
		BView(BRect(0, 0, 10, 10), "mentions", B_FOLLOW_ALL, B_WILL_DRAW),
		fTarget(target)
	{
		SetViewUIColor(B_LIST_BACKGROUND_COLOR);
	}

	float RowHeight() const
	{
		font_height fh;
		be_plain_font->GetHeight(&fh);
		return ceilf((fh.ascent + fh.descent + fh.leading) * 1.5f);
	}

	void Draw(BRect) override
	{
		font_height fh;
		be_plain_font->GetHeight(&fh);
		float row = RowHeight();
		BRect b = Bounds();
		for (size_t i = 0; i < fItems.size(); i++) {
			BRect r(b.left, i * row, b.right, (i + 1) * row - 1);
			bool selected = (int)i == fSelected;
			SetHighUIColor(selected ? B_LIST_SELECTED_BACKGROUND_COLOR : B_LIST_BACKGROUND_COLOR);
			FillRect(r);
			float y = r.top + (row + fh.ascent - fh.descent) / 2;
			float x = 8;
			BString name(fItems[i].name);
			be_bold_font->TruncateString(&name, B_TRUNCATE_END, b.Width() / 2);
			SetFont(be_bold_font);
			SetHighUIColor(selected ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
			DrawString(name.String(), BPoint(x, y));
			x += be_bold_font->StringWidth(name.String()) + 8;
			if (!fItems[i].username.IsEmpty()) {
				BString handle("@");
				handle << fItems[i].username;
				be_plain_font->TruncateString(&handle, B_TRUNCATE_END, b.right - x - 8);
				SetFont(be_plain_font);
				SetHighUIColor(selected ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR,
					selected ? B_NO_TINT : B_DARKEN_2_TINT);
				DrawString(handle.String(), BPoint(x, y));
			}
		}
		SetHighUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_2_TINT);
		StrokeRect(b);
	}

	void MouseDown(BPoint where) override
	{
		int32 index = (int32)(where.y / RowHeight());
		if (index < 0 || index >= (int32)fItems.size())
			return;
		BMessage chosen(kMsgMentionChosen);
		chosen.AddInt32("index", index);
		fTarget.SendMessage(&chosen);
	}

	std::vector<MentionPopup::Item> fItems;
	int fSelected = 0;
	BMessenger fTarget;
};


MentionPopup::MentionPopup(BWindow* parent)
	:
	BWindow(BRect(0, 0, 100, 100), "Mentions", B_NO_BORDER_WINDOW_LOOK,
		B_FLOATING_SUBSET_WINDOW_FEEL,
		B_AVOID_FOCUS | B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_NOT_MINIMIZABLE | B_WILL_ACCEPT_FIRST_CLICK),
	fTarget(parent)
{
	AddToSubset(parent);
	fView = new MentionListView(fTarget);
	fView->ResizeTo(Bounds().Width(), Bounds().Height());
	AddChild(fView);
	Hide();
	Show();   // runs the looper; stays hidden (shown once, hidden once)
}


void
MentionPopup::SetItems(const std::vector<Item>& items, BPoint bottomLeft, float width)
{
	BAutolock lock(this);
	if (items.empty()) {
		Close();
		return;
	}
	fView->fItems = items;
	fView->fSelected = 0;
	float height = fView->RowHeight() * items.size();
	ResizeTo(width, height - 1);
	MoveTo(bottomLeft.x, bottomLeft.y - height);
	fView->Invalidate();
	if (IsHidden())
		BWindow::Show();
}


void
MentionPopup::Close()
{
	BAutolock lock(this);
	if (!IsHidden())
		Hide();
}


bool
MentionPopup::IsShowing()
{
	BAutolock lock(this);
	return !IsHidden();
}


void
MentionPopup::MoveSelection(int delta)
{
	BAutolock lock(this);
	int count = (int)fView->fItems.size();
	if (count == 0)
		return;
	fView->fSelected = (fView->fSelected + delta + count) % count;
	fView->Invalidate();
}


bool
MentionPopup::Selected(Item* item)
{
	BAutolock lock(this);
	return ItemAt(fView->fSelected, item);
}


bool
MentionPopup::ItemAt(int32 index, Item* item)
{
	BAutolock lock(this);
	if (index < 0 || index >= (int32)fView->fItems.size())
		return false;
	*item = fView->fItems[index];
	return true;
}
