// SPDX-License-Identifier: GPL-3.0-or-later
// The emoji and sticker pickers: small windows that open above their button
// by the message entry and close when they lose focus or on Escape.
#pragma once

#include <Messenger.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include <list>
#include <map>
#include <string>
#include <vector>

class BBitmap;
class BListView;

class PopoverWindow : public BWindow {
public:
	PopoverWindow(const char* title, BMessenger target);

	// Opens with its bottom right corner near `corner` (screen), on screen.
	void ShowAt(BPoint corner);
	void WindowActivated(bool active) override;
	void MessageReceived(BMessage* message) override;

protected:
	BMessenger fTarget;   // the main window
};


// The emoji grid: categories with headings, recently used first.
class EmojiGrid : public BView {
public:
	explicit EmojiGrid(BMessenger target);

	void SetRecent(const std::vector<std::string>& recent);

	void AttachedToWindow() override;
	void Draw(BRect updateRect) override;
	void MouseDown(BPoint where) override;
	void FrameResized(float width, float height) override;
	void GetPreferredSize(float* width, float* height) override;

private:
	struct Cell {
		BRect frame;
		std::string emoji;
	};
	void _Layout();

	BMessenger fTarget;
	std::vector<std::string> fRecent;
	std::vector<Cell> fCells;
	std::vector<std::pair<float, BString>> fHeadings;  // baseline, text
	float fCellSize;
	float fHeight;
	bool fHaveEmojiFont;
};

class EmojiWindow : public PopoverWindow {
public:
	EmojiWindow(BMessenger target);

	void SetRecent(const std::vector<std::string>& recent);

private:
	EmojiGrid* fGrid;
};


// One sticker as the picker shows it.
struct PickerSticker {
	int32 fileId;
	int32 width;
	int32 height;
	BString emoji;
	BString imagePath;   // empty until downloaded (or not drawable)
};

class StickerGrid : public BView {
public:
	explicit StickerGrid(BMessenger target);
	~StickerGrid() override;

	void SetStickers(const std::vector<PickerSticker>& stickers, bool keepScroll);
	void SetMessageText(const char* text);

	void Draw(BRect updateRect) override;
	void MouseDown(BPoint where) override;
	void FrameResized(float width, float height) override;

private:
	void _Layout();
	BBitmap* _Bitmap(const BString& path);

	BMessenger fTarget;
	std::vector<PickerSticker> fStickers;
	std::vector<BRect> fFrames;
	std::map<std::string, BBitmap*> fBitmaps;
	BString fMessageText;
	float fHeight;
};

class StickerWindow : public PopoverWindow {
public:
	StickerWindow(BMessenger target, BMessenger app);

	void MessageReceived(BMessage* message) override;
	void Show() override;

private:
	void _SelectSet(int64 id);

	BMessenger fApp;
	BListView* fSets;
	StickerGrid* fGrid;
	std::vector<int64> fSetIds;   // parallel to fSets; 0: recent
	int64 fShown;
};
