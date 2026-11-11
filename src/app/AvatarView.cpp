// SPDX-License-Identifier: GPL-3.0-or-later
#include "AvatarView.h"

#include <Bitmap.h>
#include <DataIO.h>
#include <TranslationUtils.h>

#include <cmath>

#include "BitmapUtils.h"

// Telegram's avatar colours, as in the chat list.
static const rgb_color kAvatarColors[] = {
	{0xe1, 0x70, 0x76, 255}, {0xfa, 0xa7, 0x74, 255}, {0xa6, 0x95, 0xe7, 255},
	{0x7b, 0xc8, 0x62, 255}, {0x6e, 0xc9, 0xcb, 255}, {0x65, 0xaa, 0xdd, 255},
	{0xee, 0x7a, 0xae, 255}
};


BBitmap*
LoadImage(const BString& path, const std::string& preview)
{
	if (!path.IsEmpty()) {
		if (BBitmap* bitmap = BTranslationUtils::GetBitmap(path.String()))
			return bitmap;
	}
	if (preview.empty())
		return nullptr;
	BMemoryIO io(preview.data(), preview.size());
	return BTranslationUtils::GetBitmap(&io);
}


AvatarView::AvatarView(float side)
	:
	BView("avatar", B_WILL_DRAW)
{
	float pixels = roundf(be_plain_font->Size() * side);
	SetExplicitSize(BSize(pixels, pixels));
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


AvatarView::~AvatarView()
{
	delete fBitmap;
}


void
AvatarView::Set(int64 id, const BString& path, const std::string& preview,
	const BString& initials)
{
	if (path == fPath && !fPath.IsEmpty() && fBitmap != nullptr && id == fId)
		return;
	fId = id;
	fPath = path;
	fInitials = initials;
	delete fBitmap;
	fBitmap = nullptr;
	if (BBitmap* square = LoadImage(path, preview)) {
		fBitmap = RoundBitmap(square);
		delete square;
	}
	Invalidate();
}


void
AvatarView::Draw(BRect)
{
	BRect r = Bounds();
	SetDrawingMode(B_OP_ALPHA);
	if (fBitmap != nullptr) {
		DrawBitmap(fBitmap, fBitmap->Bounds(), r, B_FILTER_BITMAP_BILINEAR);
		return;
	}
	rgb_color color = kAvatarColors[(uint64)(fId < 0 ? -fId : fId) % 7];
	SetHighColor(color);
	FillEllipse(r);
	BFont font(be_bold_font);
	font.SetSize(roundf(r.Height() * 0.36f));
	SetFont(&font);
	font_height fh;
	font.GetHeight(&fh);
	SetHighColor(255, 255, 255);
	SetLowColor(color);
	DrawString(fInitials.String(), BPoint(
		r.left + (r.Width() - font.StringWidth(fInitials.String())) / 2,
		r.top + (r.Height() + fh.ascent - fh.descent) / 2));
}
