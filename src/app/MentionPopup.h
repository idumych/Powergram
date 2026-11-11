// SPDX-License-Identifier: GPL-3.0-or-later
// The members matching an "@name" being typed, in a small list over the
// entry. It never takes the focus: typing goes on in the entry, whose key
// filter moves the selection (Up, Down) and picks (Enter, Tab).
#pragma once

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <vector>

class MentionPopup : public BWindow {
public:
	struct Item {
		int64 userId;
		BString name;
		BString username;   // without the @; may be empty
	};

	explicit MentionPopup(BWindow* parent);

	// Shows the items with their bottom left corner at `bottomLeft` (screen
	// coordinates), `width` wide; hides when there are none.
	void SetItems(const std::vector<Item>& items, BPoint bottomLeft, float width);
	void Close();   // hides it, keeping the window
	bool IsShowing();
	void MoveSelection(int delta);
	// The selected item; false if none.
	bool Selected(Item* item);
	bool ItemAt(int32 index, Item* item);

private:
	friend class MentionListView;
	BMessenger fTarget;
	class MentionListView* fView;
};
