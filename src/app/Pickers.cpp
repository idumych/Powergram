// SPDX-License-Identifier: GPL-3.0-or-later
#include "Pickers.h"

#include <Bitmap.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MessageFilter.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <TranslationUtils.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../core/Stickers.h"
#include "Messages.h"
#include "StickerPlayer.h"

namespace {

enum {
	kMsgHidePopover = 'hpop',
	kMsgSetSelected = 'ssel'
};


// Escape closes a picker.
class EscapeHides : public BMessageFilter {
public:
	EscapeHides() : BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_KEY_DOWN) {}

	filter_result Filter(BMessage* message, BHandler**) override
	{
		const char* bytes;
		if (message->FindString("bytes", &bytes) == B_OK && bytes[0] == B_ESCAPE) {
			Looper()->PostMessage(kMsgHidePopover);
			return B_SKIP_MESSAGE;
		}
		return B_DISPATCH_MESSAGE;
	}
};


float
LineHeight()
{
	font_height fh;
	be_plain_font->GetHeight(&fh);
	return ceilf(fh.ascent + fh.descent + fh.leading);
}

} // namespace


// #pragma mark - PopoverWindow


PopoverWindow::PopoverWindow(const char* title, BMessenger target)
	:
	BWindow(BRect(0, 0, 100, 100), title, B_BORDERED_WINDOW_LOOK,
		B_FLOATING_APP_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_NOT_MINIMIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS),
	fTarget(target)
{
	AddCommonFilter(new EscapeHides());
}


void
PopoverWindow::ShowAt(BPoint corner)
{
	if (Lock()) {
		Layout(true);
		BRect frame = Frame();
		BRect screen = BScreen(this).Frame();
		float left = std::max(screen.left + 4, std::min(corner.x - frame.Width(),
			screen.right - frame.Width() - 4));
		float top = std::max(screen.top + 4, corner.y - frame.Height() - 8);
		MoveTo(left, top);
		if (IsHidden())
			Show();
		Activate();
		Unlock();
	}
}


void
PopoverWindow::WindowActivated(bool active)
{
	BWindow::WindowActivated(active);
	// Like a menu: clicking elsewhere closes it.
	if (!active)
		PostMessage(kMsgHidePopover);
}


void
PopoverWindow::MessageReceived(BMessage* message)
{
	if (message->what == kMsgHidePopover) {
		if (!IsHidden())
			Hide();
		return;
	}
	BWindow::MessageReceived(message);
}


// #pragma mark - EmojiGrid


EmojiGrid::EmojiGrid(BMessenger target)
	:
	BView("emoji", B_WILL_DRAW | B_FRAME_EVENTS | B_SCROLL_VIEW_AWARE),
	fTarget(target),
	fCellSize(roundf(LineHeight() * 2.3f)),
	fHeight(0),
	fHaveEmojiFont(true)
{
	SetViewUIColor(B_LIST_BACKGROUND_COLOR);
	// Without an emoji font (the noto_emoji package) every cell would be
	// an empty box; say so instead.
	bool has[1] = {false};
	be_plain_font->GetHasGlyphs("\xF0\x9F\x98\x80", 1, has);
	fHaveEmojiFont = has[0];
}


void
EmojiGrid::GetPreferredSize(float* width, float* height)
{
	if (width != nullptr)
		*width = fCellSize * 9 + B_V_SCROLL_BAR_WIDTH;
	if (height != nullptr)
		*height = fCellSize * 8;
}


void
EmojiGrid::SetRecent(const std::vector<std::string>& recent)
{
	fRecent = recent;
	_Layout();
	Invalidate();
}


void
EmojiGrid::AttachedToWindow()
{
	BView::AttachedToWindow();
	_Layout();
}


void
EmojiGrid::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	_Layout();
}


void
EmojiGrid::_Layout()
{
	fCells.clear();
	fHeadings.clear();
	float line = LineHeight();
	int columns = std::max(1, (int)((Bounds().Width() + 1) / fCellSize));
	float y = line * 0.3f;

	auto category = [&](const char* name, const std::vector<std::string>& emoji) {
		if (emoji.empty())
			return;
		y += line;
		fHeadings.push_back({y - line * 0.25f, name});
		for (size_t i = 0; i < emoji.size(); i++) {
			float x = (i % columns) * fCellSize;
			float top = y + (i / columns) * fCellSize;
			fCells.push_back({BRect(x, top, x + fCellSize - 1, top + fCellSize - 1), emoji[i]});
		}
		y += ((emoji.size() + columns - 1) / columns) * fCellSize + line * 0.3f;
	};
	category("Recently used", fRecent);
	for (const pp::EmojiCategory& c : pp::EmojiCatalog())
		category(c.name, c.emoji);
	fHeight = y;

	if (BScrollBar* bar = ScrollBar(B_VERTICAL)) {
		float visible = Bounds().Height();
		bar->SetRange(0, std::max(0.0f, fHeight - visible));
		bar->SetProportion(fHeight > 0 ? std::min(1.0f, visible / fHeight) : 1);
		bar->SetSteps(fCellSize, std::max(fCellSize, visible - fCellSize));
	}
}


void
EmojiGrid::Draw(BRect updateRect)
{
	rgb_color bg = ui_color(B_LIST_BACKGROUND_COLOR);
	rgb_color fg = ui_color(B_LIST_ITEM_TEXT_COLOR);
	SetLowColor(bg);
	FillRect(updateRect, B_SOLID_LOW);

	if (!fHaveEmojiFont) {
		SetHighColor(fg);
		DrawString("Emoji need a font: install the noto_emoji package.",
			BPoint(LineHeight() * 0.5f, LineHeight() * 1.2f));
	}

	SetHighColor(mix_color(fg, bg, 110));
	SetFont(be_bold_font);
	for (const auto& [baseline, text] : fHeadings)
		if (baseline > updateRect.top - 20 && baseline < updateRect.bottom + 20)
			DrawString(text.String(), BPoint(LineHeight() * 0.4f, baseline));

	// A whole size: Renku's app_server drops some fallback emoji at
	// fractional sizes.
	BFont font(be_plain_font);
	font.SetSize(floorf(fCellSize * 0.6f));
	SetFont(&font);
	font_height fh;
	font.GetHeight(&fh);
	SetHighColor(fg);
	for (const Cell& cell : fCells) {
		if (!cell.frame.Intersects(updateRect))
			continue;
		float w = font.StringWidth(cell.emoji.c_str());
		DrawString(cell.emoji.c_str(), BPoint(cell.frame.left + (cell.frame.Width() - w) / 2,
			cell.frame.top + (cell.frame.Height() + fh.ascent - fh.descent) / 2));
	}
	SetFont(be_plain_font);
}


void
EmojiGrid::MouseDown(BPoint where)
{
	for (const Cell& cell : fCells) {
		if (cell.frame.Contains(where)) {
			BMessage insert(kMsgInsertEmoji);
			insert.AddString("emoji", cell.emoji.c_str());
			fTarget.SendMessage(&insert);
			return;
		}
	}
}


// #pragma mark - EmojiWindow


EmojiWindow::EmojiWindow(BMessenger target)
	:
	PopoverWindow("Emoji", target)
{
	fGrid = new EmojiGrid(target);
	BScrollView* scroll = new BScrollView("scroll", fGrid, 0, false, true, B_NO_BORDER);
	float width, height;
	fGrid->GetPreferredSize(&width, &height);
	scroll->SetExplicitSize(BSize(width, height));
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(scroll);
}


void
EmojiWindow::SetRecent(const std::vector<std::string>& recent)
{
	if (Lock()) {
		fGrid->SetRecent(recent);
		Unlock();
	}
}


// #pragma mark - StickerGrid


StickerGrid::StickerGrid(BMessenger target)
	:
	BView("stickers", B_WILL_DRAW | B_FRAME_EVENTS | B_SCROLL_VIEW_AWARE),
	fTarget(target),
	fHeight(0)
{
	SetViewUIColor(B_LIST_BACKGROUND_COLOR);
}


StickerGrid::~StickerGrid()
{
	for (auto& [path, bitmap] : fBitmaps)
		delete bitmap;
}


void
StickerGrid::SetMessageText(const char* text)
{
	fMessageText = text;
	Invalidate();
}


void
StickerGrid::SetStickers(const std::vector<PickerSticker>& stickers, bool keepScroll)
{
	fStickers = stickers;
	fMessageText = stickers.empty() ? "No stickers here" : "";
	_Layout();
	if (!keepScroll)
		ScrollTo(0, 0);
	Invalidate();
}


void
StickerGrid::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	_Layout();
}


void
StickerGrid::_Layout()
{
	float cell = roundf(LineHeight() * 5);
	int columns = std::max(1, (int)((Bounds().Width() + 1) / cell));
	fFrames.clear();
	for (size_t i = 0; i < fStickers.size(); i++) {
		float x = (i % columns) * cell, y = (i / columns) * cell;
		fFrames.push_back(BRect(x, y, x + cell - 1, y + cell - 1));
	}
	fHeight = ((fStickers.size() + columns - 1) / columns) * cell;
	if (BScrollBar* bar = ScrollBar(B_VERTICAL)) {
		float visible = Bounds().Height();
		bar->SetRange(0, std::max(0.0f, fHeight - visible));
		bar->SetProportion(fHeight > 0 ? std::min(1.0f, visible / fHeight) : 1);
		bar->SetSteps(cell / 2, std::max(cell, visible - cell));
	}
}


BBitmap*
StickerGrid::_Bitmap(const BString& path)
{
	auto it = fBitmaps.find(path.String());
	if (it != fBitmaps.end())
		return it->second;
	BBitmap* bitmap = BTranslationUtils::GetBitmap(path.String());
	fBitmaps[path.String()] = bitmap;  // nullptr too: don't retry
	return bitmap;
}


// An animated sticker's first frame, `side` pixels square, kept like the
// images.
BBitmap*
StickerGrid::_FirstFrame(const PickerSticker& sticker, int32 side)
{
	std::string key = "frame:" + std::string(sticker.animationPath.String());
	auto it = fBitmaps.find(key);
	if (it != fBitmaps.end())
		return it->second;
	BBitmap* copy = nullptr;
	if (StickerPlayer* player = StickerPlayer::Open(sticker.animationPath,
			sticker.animationFormat)) {
		if (const BBitmap* frame = player->Frame(side, side, system_time())) {
			copy = new BBitmap(frame->Bounds(), B_RGBA32);
			memcpy(copy->Bits(), frame->Bits(), std::min(copy->BitsLength(), frame->BitsLength()));
		}
		delete player;
	}
	fBitmaps[key] = copy;   // nullptr too: don't retry
	return copy;
}


void
StickerGrid::Draw(BRect updateRect)
{
	rgb_color bg = ui_color(B_LIST_BACKGROUND_COLOR);
	rgb_color fg = ui_color(B_LIST_ITEM_TEXT_COLOR);
	SetLowColor(bg);
	FillRect(updateRect, B_SOLID_LOW);

	if (!fMessageText.IsEmpty()) {
		SetHighColor(mix_color(fg, bg, 110));
		float w = StringWidth(fMessageText.String());
		DrawString(fMessageText.String(), BPoint((Bounds().Width() - w) / 2,
			Bounds().top + Bounds().Height() / 2));
		return;
	}

	SetDrawingMode(B_OP_ALPHA);
	for (size_t i = 0; i < fStickers.size(); i++) {
		BRect frame = fFrames[i];
		if (!frame.Intersects(updateRect))
			continue;
		BRect box = frame.InsetByCopy(frame.Width() * 0.08f, frame.Height() * 0.08f);
		const PickerSticker& s = fStickers[i];
		BBitmap* bitmap = s.imagePath.IsEmpty() ? nullptr : _Bitmap(s.imagePath);
		if (bitmap == nullptr && !s.animationPath.IsEmpty())
			bitmap = _FirstFrame(s, box.IntegerWidth() + 1);
		if (bitmap != nullptr) {
			BRect b = bitmap->Bounds();
			float scale = std::min(box.Width() / b.Width(), box.Height() / b.Height());
			float w = b.Width() * scale, h = b.Height() * scale;
			BRect dest(box.left + (box.Width() - w) / 2, box.top + (box.Height() - h) / 2, 0, 0);
			dest.right = dest.left + w;
			dest.bottom = dest.top + h;
			DrawBitmapAsync(bitmap, b, dest, B_FILTER_BITMAP_BILINEAR);
		} else {
			// Not downloaded yet, or animated: its emoji (whole size, see
			// EmojiGrid::Draw), or a soft placeholder.
			BFont font(be_plain_font);
			font.SetSize(floorf(box.Height() * 0.45f));
			SetFont(&font);
			font_height fh;
			font.GetHeight(&fh);
			SetHighColor(fg);
			float w = font.StringWidth(s.emoji.String());
			DrawString(s.emoji.String(), BPoint(box.left + (box.Width() - w) / 2,
				box.top + (box.Height() + fh.ascent - fh.descent) / 2));
			SetFont(be_plain_font);
		}
	}
	Sync();
}


void
StickerGrid::MouseDown(BPoint where)
{
	for (size_t i = 0; i < fFrames.size(); i++) {
		if (fFrames[i].Contains(where)) {
			const PickerSticker& s = fStickers[i];
			BMessage send(kMsgSendSticker);
			send.AddInt32("file_id", s.fileId);
			send.AddInt32("width", s.width);
			send.AddInt32("height", s.height);
			send.AddString("emoji", s.emoji);
			fTarget.SendMessage(&send);
			Window()->PostMessage(kMsgHidePopover);
			return;
		}
	}
}


// #pragma mark - StickerWindow


StickerWindow::StickerWindow(BMessenger target, BMessenger app)
	:
	PopoverWindow("Stickers", target),
	fApp(app),
	fShown(-1)
{
	fSets = new BListView("sets");
	fSets->SetSelectionMessage(new BMessage(kMsgSetSelected));
	BScrollView* setScroll = new BScrollView("set scroll", fSets, 0, false, true);
	fGrid = new StickerGrid(target);
	fGrid->SetMessageText("Loading stickers…");
	BScrollView* gridScroll = new BScrollView("grid scroll", fGrid, 0, false, true, B_NO_BORDER);

	float line = LineHeight();
	setScroll->SetExplicitSize(BSize(roundf(line * 9), roundf(line * 20)));
	gridScroll->SetExplicitSize(BSize(roundf(line * 5) * 4 + B_V_SCROLL_BAR_WIDTH,
		roundf(line * 20)));
	BLayoutBuilder::Group<>(this, B_HORIZONTAL, 0)
		.Add(setScroll)
		.Add(gridScroll);
}


void
StickerWindow::Show()
{
	PopoverWindow::Show();
	// Ask for the set list each time (it changes when sets are added).
	fApp.SendMessage(kMsgStickerSetsWanted);
}


void
StickerWindow::_SelectSet(int64 id)
{
	if (id == fShown)
		return;
	fShown = id;
	fGrid->SetStickers({}, false);
	fGrid->SetMessageText("Loading stickers…");
	BMessage want(kMsgStickerSetWanted);
	want.AddInt64("set_id", id);
	fApp.SendMessage(&want);
}


void
StickerWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgStickerSets:
		{
			// "id" (int64) and "title" arrays; the recent stickers first.
			while (BListItem* item = fSets->RemoveItem((int32)0))
				delete item;
			fSetIds.clear();
			fSets->AddItem(new BStringItem("Recently used"));
			fSetIds.push_back(0);
			int64 id;
			for (int32 i = 0; message->FindInt64("id", i, &id) == B_OK; i++) {
				fSets->AddItem(new BStringItem(message->GetString("title", i, "")));
				fSetIds.push_back(id);
			}
			int32 select = 0;
			for (size_t i = 0; i < fSetIds.size(); i++)
				if (fSetIds[i] == fShown)
					select = (int32)i;
			fShown = -1;
			fSets->Select(select);
			break;
		}
		case kMsgSetSelected:
		{
			int32 index = fSets->CurrentSelection();
			if (index >= 0 && index < (int32)fSetIds.size())
				_SelectSet(fSetIds[index]);
			break;
		}
		case kMsgStickers:
		{
			if (message->GetInt64("set_id", -1) != fShown)
				break;
			std::vector<PickerSticker> stickers;
			int32 fileId;
			for (int32 i = 0; message->FindInt32("file_id", i, &fileId) == B_OK; i++) {
				PickerSticker s;
				s.fileId = fileId;
				s.width = message->GetInt32("width", i, 0);
				s.height = message->GetInt32("height", i, 0);
				s.emoji = message->GetString("emoji", i, "");
				s.imagePath = message->GetString("image", i, "");
				s.animationPath = message->GetString("animation", i, "");
				s.animationFormat = message->GetInt32("animation_format", i, 0);
				stickers.push_back(s);
			}
			fGrid->SetStickers(stickers, message->GetBool("update", false));
			break;
		}
		default:
			PopoverWindow::MessageReceived(message);
	}
}
