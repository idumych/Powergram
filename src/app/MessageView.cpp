// SPDX-License-Identifier: GPL-3.0-or-later
#include "MessageView.h"

#include <Bitmap.h>
#include <DataIO.h>
#include <ScrollBar.h>
#include <TranslationUtils.h>
#include <Window.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "BitmapUtils.h"
#include "Messages.h"

MessageView::MessageView()
	:
	// B_SCROLL_VIEW_AWARE: we set the scroll bar ourselves. Without it,
	// BScrollView::FrameResized resets the range from our PreferredSize()
	// whenever its height changes, which scrolled the chat to the top.
	BView("messages", B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE
		| B_SCROLL_VIEW_AWARE),
	fContentHeight(0),
	fDropTarget(false),
	fAtEnd(true),
	fRelayouting(false),
	fLastHeight(0)
{
	SetViewUIColor(B_LIST_BACKGROUND_COLOR);
	SetTextScale(1.0f);
}


static const size_t kMaxBitmaps = 150;


void
MessageView::MessageReceived(BMessage* message)
{
	if (message->what == B_SIMPLE_DATA && message->HasRef("refs")) {
		// Files dropped from Tracker: the window sends them to the chat.
		fDropTarget = false;
		Invalidate();
		Window()->PostMessage(message);
		return;
	}
	BView::MessageReceived(message);
}


void
MessageView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
	// Drop feedback: an outline round the chat while files hover over it.
	bool target = drag != nullptr && drag->HasRef("refs")
		&& transit != B_EXITED_VIEW && transit != B_OUTSIDE_VIEW && !fRows.empty();
	if (target != fDropTarget) {
		fDropTarget = target;
		Invalidate();
	}
	BView::MouseMoved(where, transit, drag);
}


void
MessageView::DetachedFromWindow()
{
	_ClearBitmaps();
	BView::DetachedFromWindow();
}


void
MessageView::_ClearBitmaps()
{
	for (auto& [key, bitmap] : fBitmaps)
		delete bitmap;
	fBitmaps.clear();
	fBitmapOrder.clear();
}


BBitmap*
MessageView::_Bitmap(const MessageRow& row, bool* blurred, bool round)
{
	if (round) {
		// A round copy, cached next to the square one.
		BBitmap* square = _Bitmap(row, blurred, false);
		if (square == nullptr)
			return nullptr;
		std::string key = (!*blurred ? std::string(row.imagePath.String())
			: "mini:" + std::to_string(row.id)) + "#round";
		auto it = fBitmaps.find(key);
		if (it != fBitmaps.end())
			return it->second;
		BBitmap* bitmap = RoundBitmap(square);
		if (bitmap != nullptr) {
			fBitmaps[key] = bitmap;
			fBitmapOrder.push_front(key);
		}
		return bitmap;
	}

	// The downloaded image if there is one, else the inline preview. Decoded
	// once through the Translation Kit (JPEG, PNG, WebP...) and cached.
	*blurred = false;
	std::string key;
	if (!row.imagePath.IsEmpty())
		key = row.imagePath.String();
	else if (!row.minithumbnail.empty()) {
		key = "mini:" + std::to_string(row.id);
		*blurred = true;
	} else
		return nullptr;

	auto it = fBitmaps.find(key);
	if (it != fBitmaps.end()) {
		fBitmapOrder.remove(key);
		fBitmapOrder.push_front(key);
		return it->second;
	}

	BBitmap* bitmap;
	if (*blurred) {
		BMemoryIO io(row.minithumbnail.data(), row.minithumbnail.size());
		bitmap = BTranslationUtils::GetBitmap(&io);
	} else
		bitmap = BTranslationUtils::GetBitmap(row.imagePath.String());
	if (bitmap == nullptr) {
		if (!*blurred && !row.minithumbnail.empty()) {
			// The file didn't decode: fall back to the preview.
			MessageRow copy = row;
			copy.imagePath = "";
			return _Bitmap(copy, blurred, false);
		}
		return nullptr;
	}
	fBitmaps[key] = bitmap;
	fBitmapOrder.push_front(key);
	while (fBitmapOrder.size() > kMaxBitmaps) {
		delete fBitmaps[fBitmapOrder.back()];
		fBitmaps.erase(fBitmapOrder.back());
		fBitmapOrder.pop_back();
	}
	return bitmap;
}


void
MessageView::_DrawMedia(const MessageRow& row, const Layout& l, rgb_color fg,
	float line)
{
	BRect box = l.media;
	if (row.mediaKind == 6) {
		_DrawVoice(row, box, fg, line);
		return;
	}
	bool hidden = (row.spoiler || row.secret) && !fRevealed.count(row.id);
	bool blurred;
	// A hidden spoiler shows only the blurred preview, never the photo; a
	// self-destructing photo shows nothing until it's opened.
	BBitmap* bitmap = nullptr;
	if (hidden && !row.secret) {
		MessageRow preview = row;
		preview.imagePath = "";
		bitmap = _Bitmap(preview, &blurred);
	} else if (!hidden)
		bitmap = _Bitmap(row, &blurred, row.mediaKind == 5);
	float radius = roundf(line * 0.5f);

	bool emojiDrawable = false;
	if (bitmap == nullptr && row.mediaKind == 2 && !row.emoji.IsEmpty()) {
		// Without an emoji font (noto_emoji) the glyph would be an empty box.
		bool has[8] = {};
		int32 chars = std::min<int32>(row.emoji.CountChars(), 8);
		(&fPlain)->GetHasGlyphs(row.emoji.String(), chars, has);
		emojiDrawable = chars > 0 && has[0];
	}
	if (emojiDrawable) {
		// An animated sticker Haiku can't draw: its emoji, large.
		BFont big((&fPlain));
		// A whole size: Renku's app_server drops some fallback emoji at
		// fractional sizes (🎉 at 35.7 pt), though GetHasGlyphs says yes.
		big.SetSize(floorf(box.Height() * 0.7f));
		SetFont(&big);
		font_height fh;
		big.GetHeight(&fh);
		SetHighColor(fg);
		DrawString(row.emoji.String(), BPoint(box.left
			+ (box.Width() - big.StringWidth(row.emoji.String())) / 2,
			box.top + (box.Height() + fh.ascent - fh.descent) / 2));
		SetFont((&fPlain));
		return;
	}

	bool round = row.mediaKind == 5;
	if (bitmap != nullptr) {
		SetDrawingMode(row.mediaKind == 2 || round ? B_OP_ALPHA : B_OP_COPY);
		DrawBitmapAsync(bitmap, bitmap->Bounds(), box, B_FILTER_BITMAP_BILINEAR);
		SetDrawingMode(B_OP_ALPHA);
	} else {
		SetHighColor(mix_color(LowColor(), fg, 40));
		if (round)
			FillEllipse(box);
		else
			FillRoundRect(box, radius, radius);
	}

	// Videos: "▶ 0:42" in a dark pill at the top left; GIFs: "GIF".
	if (row.mediaKind >= 3 && !hidden) {
		BString badge;
		if (row.mediaKind == 4)
			badge = "GIF";
		else {
			badge << row.duration / 60 << ":";
			if (row.duration % 60 < 10)
				badge << "0";
			badge << row.duration % 60;
		}
		BFont small((&fPlain));
		small.SetSize(std::max(9.0f, (&fPlain)->Size() * 0.85f));
		SetFont(&small);
		font_height sh;
		small.GetHeight(&sh);
		float h = ceilf(sh.ascent + sh.descent) + 4;
		bool play = row.mediaKind != 4;
		// The play triangle is drawn, not a "▶" glyph: that would come from
		// a fallback font, which Renku's app_server misses at some sizes.
		float triangle = play ? ceilf(sh.ascent * 0.8f) : 0;
		float w = small.StringWidth(badge.String()) + h + (play ? triangle + 4 : 0);
		float inset = round ? box.Width() * 0.15f : radius * 0.6f;
		BRect pill(box.left + inset, box.top + inset, box.left + inset + w, box.top + inset + h);
		SetHighColor(0, 0, 0, 140);
		FillRoundRect(pill, h / 2, h / 2);
		SetHighColor(255, 255, 255);
		float tx = pill.left + h / 2;
		if (play) {
			float top = pill.top + (h - triangle) / 2;
			FillTriangle(BPoint(tx, top), BPoint(tx, top + triangle),
				BPoint(tx + triangle * 0.85f, top + triangle / 2));
			tx += triangle + 4;
		}
		DrawString(badge.String(), BPoint(tx, pill.top + 2 + sh.ascent));
		SetFont((&fPlain));
	}

	if (hidden || bitmap == nullptr) {
		// A dimmed cover with what clicking does.
		if (hidden && bitmap != nullptr) {
			SetHighColor(0, 0, 0, 120);
			FillRect(box);
		}
		const char* label = row.secret ? "Self-destructing photo: click to open"
			: hidden ? "Spoiler: click to show"
			: row.mediaKind == 2 ? (row.imagePath.IsEmpty() && row.emoji.IsEmpty()
				? "Loading sticker…" : "Sticker")
			: row.mediaKind >= 3 ? "Video"
			: "Loading photo…";
		BString text(label);
		(&fPlain)->TruncateString(&text, B_TRUNCATE_END, box.Width() - line);
		SetHighColor(hidden && bitmap != nullptr ? make_color(255, 255, 255) : fg);
		font_height fh;
		GetFontHeight(&fh);
		DrawString(text.String(), BPoint(box.left + (box.Width() - StringWidth(text.String())) / 2,
			box.top + (box.Height() + fh.ascent - fh.descent) / 2));
	}
}


// A voice message: a round play (or stop) button, the waveform, filled
// up to where playback has got, and the length (or the time played).
void
MessageView::_DrawVoice(const MessageRow& row, BRect box, rgb_color fg, float line)
{
	rgb_color accent = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
	bool playing = row.id == fVoiceId;
	float progress = playing ? fVoiceProgress : 0;

	float d = box.Height();
	BRect button(box.left, box.top, box.left + d - 1, box.bottom);
	SetHighColor(accent);
	FillEllipse(button);
	SetHighColor(255, 255, 255);
	BPoint c((button.left + button.right) / 2, (button.top + button.bottom) / 2);
	float s = roundf(d * 0.18f);
	if (playing)
		FillRect(BRect(c.x - s, c.y - s, c.x + s, c.y + s));
	else
		FillTriangle(BPoint(c.x - s * 0.8f, c.y - s * 1.2f), BPoint(c.x - s * 0.8f, c.y + s * 1.2f),
			BPoint(c.x + s * 1.3f, c.y));

	// The length below the bars, so the bars get the full width.
	int32 seconds = playing ? (int32)(row.duration * progress) : row.duration;
	BString time;
	time << seconds / 60 << ":" << (seconds % 60 < 10 ? "0" : "") << seconds % 60;
	BFont small(&fPlain);
	small.SetSize(std::max(9.0f, floorf(fPlain.Size() * 0.85f)));
	font_height sh;
	small.GetHeight(&sh);
	float left = button.right + roundf(line * 0.5f);
	float barsBottom = box.bottom - ceilf(sh.ascent + sh.descent) - 2;
	float barsTop = box.top + 2;
	float barsHeight = barsBottom - barsTop;

	// One bar every 3 pixels, each the loudest level in its stretch.
	const std::string& w = row.waveform;
	int bars = std::max(1, (int)((box.right - left) / 3));
	rgb_color dim = mix_color(accent, LowColor(), 110);
	for (int i = 0; i < bars; i++) {
		int level = 0;
		if (!w.empty()) {
			size_t from = w.size() * i / bars;
			size_t to = std::max(from + 1, w.size() * (i + 1) / bars);
			for (size_t k = from; k < to && k < w.size(); k++)
				level = std::max(level, (int)(unsigned char)w[k]);
		}
		float h = std::max(2.0f, roundf(barsHeight * level / 31.0f));
		float x = left + i * 3;
		SetHighColor((float)i / bars < progress ? accent : dim);
		FillRect(BRect(x, barsBottom - h, x + 1, barsBottom));
	}

	SetFont(&small);
	SetHighColor(mix_color(fg, LowColor(), 160));
	DrawString(time.String(), BPoint(left, box.bottom - sh.descent));
	SetFont(&fPlain);
}


// Telegram's avatar colours, as in the chat list.
static const rgb_color kAvatarColors[] = {
	{0xe1, 0x70, 0x76, 255}, {0xfa, 0xa7, 0x74, 255}, {0xa6, 0x95, 0xe7, 255},
	{0x7b, 0xc8, 0x62, 255}, {0x6e, 0xc9, 0xcb, 255}, {0x65, 0xaa, 0xdd, 255},
	{0xee, 0x7a, 0xae, 255}
};


BBitmap*
MessageView::_AvatarBitmap(const MessageRow& row)
{
	// Round copies of the downloaded picture, or of its preview; cached
	// with the other images.
	std::string key = !row.avatarPath.IsEmpty()
		? "avatar:" + std::string(row.avatarPath.String())
		: "avatarmini:" + std::to_string(row.senderKey);
	if (row.avatarPath.IsEmpty() && row.avatarPreview.empty())
		return nullptr;
	auto it = fBitmaps.find(key);
	if (it != fBitmaps.end())
		return it->second;
	BBitmap* square;
	if (!row.avatarPath.IsEmpty())
		square = BTranslationUtils::GetBitmap(row.avatarPath.String());
	else {
		BMemoryIO io(row.avatarPreview.data(), row.avatarPreview.size());
		square = BTranslationUtils::GetBitmap(&io);
	}
	BBitmap* round = RoundBitmap(square);
	delete square;
	fBitmaps[key] = round;  // nullptr too: don't decode again
	fBitmapOrder.push_front(key);
	return round;
}


void
MessageView::_DrawAvatar(const MessageRow& row, BRect circle)
{
	SetDrawingMode(B_OP_ALPHA);
	if (BBitmap* bitmap = _AvatarBitmap(row)) {
		DrawBitmapAsync(bitmap, bitmap->Bounds(), circle, B_FILTER_BITMAP_BILINEAR);
		return;
	}
	rgb_color color = kAvatarColors[(uint64)(row.senderKey < 0 ? -row.senderKey
		: row.senderKey) % 7];
	SetHighColor(color);
	FillEllipse(circle);
	BFont font((&fBold));
	font.SetSize(floorf(circle.Height() * 0.38f));
	SetFont(&font);
	font_height fh;
	font.GetHeight(&fh);
	SetHighColor(255, 255, 255);
	SetLowColor(color);
	float w = font.StringWidth(row.avatarInitials.String());
	DrawString(row.avatarInitials.String(), BPoint(circle.left + (circle.Width() - w) / 2,
		circle.top + (circle.Height() + fh.ascent - fh.descent) / 2));
	SetFont((&fPlain));
}


void
MessageView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fLastHeight = Bounds().Height();
	_Relayout();
}


void
MessageView::SetEmptyText(const char* text)
{
	fEmptyText = text;
	Invalidate();
}


void
MessageView::SetMoreBelow(bool moreBelow)
{
	if (fMoreBelow != moreBelow) {
		fMoreBelow = moreBelow;
		Invalidate(_JumpButtonFrame());
	}
}


void
MessageView::SetTextScale(float scale)
{
	// The system fonts at the chosen size; everything drawn and measured
	// here uses these copies (Settings: Text size).
	fPlain = *be_plain_font;
	fBold = *be_bold_font;
	fFixed = *be_fixed_font;
	float size = roundf(be_plain_font->Size() * scale);
	fPlain.SetSize(size);
	fBold.SetSize(size);
	fFixed.SetSize(size);
	SetFont(&fPlain);
	fMeasured.clear();
	if (Window() != nullptr) {
		Anchor anchor = _SaveAnchor(Bounds().bottom);
		_Relayout();
		_RestoreAnchor(anchor);
		Invalidate();
	}
}


void
MessageView::SetHighlight(int64 id)
{
	if (fHighlight != id) {
		fHighlight = id;
		Invalidate();
	}
}


void
MessageView::SetVoiceProgress(int64 id, float progress)
{
	int64 old = fVoiceId;
	fVoiceId = id;
	fVoiceProgress = progress;
	for (size_t i = 0; i < fRows.size() && i < fLayout.size(); i++) {
		if (fRows[i].id != 0 && (fRows[i].id == id || fRows[i].id == old))
			Invalidate(fLayout[i].media);
	}
}


void
MessageView::SetUnreadCount(int32 count)
{
	if (fUnreadCount != count) {
		fUnreadCount = count;
		Invalidate(_JumpButtonFrame());
	}
}


BRect
MessageView::_JumpButtonFrame() const
{
	// Bottom right of what's visible, in view coordinates; room for the badge.
	font_height fh;
	(&fPlain)->GetHeight(&fh);
	float size = roundf((fh.ascent + fh.descent) * 2.6f);
	BRect b = Bounds();
	return BRect(b.right - size - 14, b.bottom - size - 14 - size * 0.3f,
		b.right - 14, b.bottom - 14);
}


void
MessageView::SetMessages(const std::vector<MessageRow>& rows, bool scrollToEnd,
	int64 scrollTo)
{
	// New messages at the end keep a reader at the end; older ones loading
	// above, or an edit changing a message's height, keep what's on screen.
	Anchor anchor = _SaveAnchor(Bounds().bottom);
	if (scrollToEnd)
		anchor.atEnd = true;
	fRows = rows;
	_Relayout();
	_RestoreAnchor(anchor);
	if (scrollTo != 0) {
		// The first unread message near the top, its "Unread messages"
		// separator above it.
		for (size_t i = 0; i < fRows.size(); i++) {
			if (fRows[i].id != scrollTo)
				continue;
			float top = (i > 0 ? fLayout[i - 1].bubble.top : fLayout[i].bubble.top) - 8;
			fRelayouting = true;
			ScrollTo(BPoint(0, std::max(0.0f, std::min(top,
				fContentHeight - Bounds().Height()))));
			fRelayouting = false;
			fAtEnd = fContentHeight - Bounds().bottom < 4;
			break;
		}
	}
	Invalidate();
}


const MessageView::Measured&
MessageView::_Measure(const BString& text, bool bold)
{
	BFont font(bold ? (&fBold) : (&fPlain));
	if (fMeasured.size() > 20000)
		fMeasured.clear();
	// Plain and bold can differ in size: both are part of the key.
	char prefix[32];
	snprintf(prefix, sizeof(prefix), "%c%.2f|", bold ? 'b' : 'p', font.Size());
	std::string key(prefix);
	key.append(text.String(), text.Length());
	auto it = fMeasured.find(key);
	if (it != fMeasured.end())
		return it->second;

	Measured m;
	int32 chars = text.CountChars();
	m.offsets.reserve(chars + 1);
	const char* bytes = text.String();
	for (int32 i = 0; i < text.Length();) {
		m.offsets.push_back(i);
		unsigned char c = bytes[i];
		i += c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
	}
	m.offsets.push_back(text.Length());
	chars = (int32)m.offsets.size() - 1;
	m.widths.resize(chars);
	if (chars > 0) {
		font.GetEscapements(bytes, chars, m.widths.data());
		for (float& w : m.widths) {
			w *= font.Size();
			m.total += w;
		}
	}
	return fMeasured.emplace(key, std::move(m)).first->second;
}


void
MessageView::_FontFor(uint32 style, BFont* font) const
{
	// Always from (&fPlain): the view's current font is whatever the
	// last run drew with (a bold base made links bold and too wide).
	BFont plain((&fPlain));
	if (style & pp::kCode) {
		*font = *(&fFixed);
		font->SetSize(plain.Size());
		return;
	}
	*font = (style & pp::kBold) ? *(&fBold) : plain;
	font->SetSize(plain.Size());
	if (style & pp::kItalic) {
		// B_REGULAR_FACE can't be combined with italic: build the face.
		uint16 face = B_ITALIC_FACE;
		if (style & pp::kBold)
			face |= B_BOLD_FACE;
		font->SetFace(face);
	}
}


// Only these styles change a character's width.
static uint32
FontStyle(uint32 style)
{
	return style & (pp::kBold | pp::kItalic | pp::kCode);
}


const MessageView::Measured&
MessageView::_MeasureRow(const MessageRow& row)
{
	if (row.spans.empty())
		return _Measure(row.text, false);
	BFont plain((&fPlain));
	char prefix[32];
	snprintf(prefix, sizeof(prefix), "s%.2f|", plain.Size());
	std::string key = prefix + pp::EncodeSpans(row.spans) + "|";
	key.append(row.text.String(), row.text.Length());
	auto it = fMeasured.find(key);
	if (it != fMeasured.end())
		return it->second;

	Measured m;
	const char* bytes = row.text.String();
	for (int32 i = 0; i < row.text.Length();) {
		m.offsets.push_back(i);
		unsigned char c = bytes[i];
		i += c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
	}
	m.offsets.push_back(row.text.Length());
	int32 chars = (int32)m.offsets.size() - 1;
	m.widths.resize(chars);
	std::vector<uint32> styles(chars, 0);
	for (const pp::Span& span : row.spans)
		for (int32 c = 0; c < chars; c++)
			if (m.offsets[c] >= span.start && m.offsets[c] < span.start + span.length)
				styles[c] |= span.style;
	// One GetEscapements call per run of characters in the same font.
	for (int32 c = 0; c < chars;) {
		int32 end = c + 1;
		while (end < chars && FontStyle(styles[end]) == FontStyle(styles[c]))
			end++;
		BFont font;
		_FontFor(styles[c], &font);
		font.GetEscapements(bytes + m.offsets[c], end - c, m.widths.data() + c);
		for (int32 k = c; k < end; k++) {
			m.widths[k] *= font.Size();
			m.total += m.widths[k];
		}
		c = end;
	}
	return fMeasured.emplace(key, std::move(m)).first->second;
}


std::vector<BString>
MessageView::_Wrap(const BString& text, const Measured& m, float width, float* widest,
	std::vector<int32>* lineChars)
{
	// Greedy wrap at spaces over the measured widths; a word longer than a
	// line is cut between characters. No app_server calls.
	const char* bytes = text.String();
	std::vector<BString> lines;
	*widest = 0;

	int32 count = (int32)m.widths.size();
	int32 lineStart = 0;      // character index
	int32 lastBreak = -1;     // index after the last space on this line
	float lineWidth = 0;
	float widthAtBreak = 0;
	auto emit = [&](int32 from, int32 to, float w) {
		// Drop the newline itself; keep trailing spaces out of the width.
		int32 a = m.offsets[from], b = m.offsets[to];
		BString line;
		line.SetTo(bytes + a, b - a);
		lines.push_back(line);
		if (lineChars != nullptr)
			lineChars->push_back(from);
		*widest = std::max(*widest, w);
	};

	for (int32 i = 0; i < count; i++) {
		char c = bytes[m.offsets[i]];
		if (c == '\n') {
			emit(lineStart, i, lineWidth);
			lineStart = i + 1;
			lastBreak = -1;
			lineWidth = 0;
			continue;
		}
		float w = m.widths[i];
		if (lineWidth + w > width && i > lineStart) {
			if (lastBreak > lineStart) {
				emit(lineStart, lastBreak, widthAtBreak);
				lineWidth -= widthAtBreak;
				lineStart = lastBreak;
			} else {
				emit(lineStart, i, lineWidth);
				lineStart = i;
				lineWidth = 0;
			}
			lastBreak = -1;
		}
		lineWidth += w;
		if (c == ' ') {
			lastBreak = i + 1;
			widthAtBreak = lineWidth;
		}
	}
	emit(lineStart, count, lineWidth);
	return lines;
}


void
MessageView::_Relayout()
{
	BFont font;
	GetFont(&font);
	font_height fh;
	font.GetHeight(&fh);
	float line = ceilf(fh.ascent + fh.descent + fh.leading);
	float pad = roundf(line * 0.5f);
	float width = Bounds().Width();
	// Paper Plane's bubbles stop at about two thirds of a wide window.
	float maxBubble = std::max(line * 6, std::min(width * 0.75f, line * 32));

	// Groups leave a column for the senders' pictures.
	float avatar = roundf(line * 2.4f);
	bool avatars = false;
	for (const MessageRow& row : fRows)
		avatars |= row.showAvatar;
	float avatarColumn = avatars ? avatar + pad * 0.6f : 0;

	fLayout.clear();
	float y = pad;
	for (const MessageRow& row : fRows) {
		Layout l;
		float textWidth = maxBubble - 2 * pad;
		float widest = 0;
		const Measured& measured = _MeasureRow(row);
		l.lines = _Wrap(row.text, measured, textWidth, &widest, &l.lineChars);
		float meta = _Width(row.time) + (row.edited ? _Width("edited ") : 0) + line * 2;
		float nameWidth = row.sender.IsEmpty() ? 0 : _Width(row.sender, true);
		float bubbleWidth = std::min(maxBubble,
			std::max({widest, nameWidth, meta}) + 2 * pad);
		const BString& forwardLabel = row.forward;
		float forwardWidth = forwardLabel.IsEmpty() ? 0 : _Width(forwardLabel);
		bool hasReply = !row.replyText.IsEmpty() || !row.replySender.IsEmpty();
		float replyWidth = hasReply ? std::max(_Width(row.replySender, true),
			std::min(_Width(row.replyText), textWidth)) + pad : 0;
		bubbleWidth = std::min(maxBubble, std::max({bubbleWidth,
			forwardWidth + 2 * pad, replyWidth + 2 * pad}));

		// A photo fills the bubble's width (up to a height limit); a
		// sticker is a fixed box with no bubble, like Paper Plane's.
		float mediaW = 0, mediaH = 0;
		// Stickers and round video messages go without a bubble.
		bool sticker = row.mediaKind == 2 || row.mediaKind == 5;
		if (row.mediaKind == 6) {
			mediaW = std::min(maxBubble - 2 * pad, line * 14);
			mediaH = roundf(line * 2.4f);
			bubbleWidth = std::max(bubbleWidth, std::min(maxBubble, mediaW + 2 * pad));
		} else if (row.mediaKind != 0) {
			float w = row.mediaWidth > 0 ? row.mediaWidth : 4;
			float h = row.mediaHeight > 0 ? row.mediaHeight : 3;
			float boxW = row.mediaKind == 5 ? line * 13 : sticker ? line * 9 : maxBubble - pad;
			float boxH = row.mediaKind == 5 ? line * 13 : sticker ? line * 9 : line * 20;
			if (sticker && row.imagePath.IsEmpty() && row.minithumbnail.empty()
				&& !row.emoji.IsEmpty())
				boxW = boxH = line * 4;  // just the emoji
			float scale = std::min(boxW / w, boxH / h);
			if (!sticker)
				scale = std::min(scale, 2.0f);  // don't blow small photos up much
			mediaW = std::max(line * 4, roundf(w * scale));
			mediaH = std::max(line * 3, roundf(h * scale));
			bubbleWidth = sticker ? std::max(mediaW, bubbleWidth)
				: std::max(bubbleWidth, std::min(maxBubble, mediaW + pad));
		}

		float y0 = y + pad;
		if (!row.sender.IsEmpty())
			y0 += line;
		l.forwardTop = y0;
		if (!forwardLabel.IsEmpty())
			y0 += line;
		l.replyTop = y0;
		if (hasReply)
			y0 += line * 2 + pad * 0.5f;
		if (row.mediaKind != 0) {
			if (sticker && row.sender.IsEmpty() && forwardLabel.IsEmpty() && !hasReply)
				y0 = y;  // no bubble around a bare sticker
			l.media.Set(0, y0, mediaW - 1, y0 + mediaH - 1);
			y0 += mediaH + pad * 0.5f;
		}
		l.textTop = y0;
		// Reaction pills: one row below the text (wrapping if needed).
		BFont pillFont((&fPlain));
		pillFont.SetSize(std::max(11.0f, floorf((&fPlain)->Size())));
		float pillH = roundf(line * 1.25f);
		std::vector<float> pillW;
		for (const MessageRow::Reaction& r : row.reactions) {
			BString label(r.emoji);
			label << " " << r.count;
			pillW.push_back(pillFont.StringWidth(label.String()) + pillH);
		}
		if (l.lines.size() == 1 && l.lines[0].IsEmpty()) {
			l.lines.clear();  // no caption
			l.lineChars.clear();
		}
		float pillRows = 0;
		if (!pillW.empty()) {
			float rowX = 0, maxRow = std::max(bubbleWidth - 2 * pad, maxBubble - 2 * pad);
			pillRows = 1;
			for (float w : pillW) {
				if (rowX > 0 && rowX + w > maxRow) {
					pillRows++;
					rowX = 0;
				}
				rowX += w + 4;
			}
			bubbleWidth = std::max(bubbleWidth, std::min(maxBubble,
				(pillRows == 1 ? rowX : maxRow) + 2 * pad));
		}
		float pillsTop = y0 + line * l.lines.size() + (pillRows > 0 ? pad * 0.4f : 0);
		float height = (y0 - y) + line * l.lines.size()
			+ pillRows * (pillH + 4) + (pillRows > 0 ? pad * 0.4f : 0) + line + pad * 0.5f;
		if (row.service) {
			bubbleWidth = std::min(width - 2 * pad, widest + 2 * pad);
			height = line * l.lines.size() + pad;
		}

		float left = row.service ? (width - bubbleWidth) / 2
			: row.outgoing ? width - pad * 2 - bubbleWidth : pad * 2 + avatarColumn;
		l.bubble.Set(left, y, left + bubbleWidth, y + height);
		if (row.service)
			l.textTop = y + pad * 0.5f;
		if (row.mediaKind != 0)
			l.media.OffsetBy(sticker ? left : row.mediaKind == 6 ? left + pad
				: left + (bubbleWidth - mediaW) / 2, 0);

		// Where the reaction pills go.
		{
			float px = left + pad, py = pillsTop, maxRight = left + bubbleWidth - pad;
			for (float w : pillW) {
				if (px > left + pad && px + w > maxRight) {
					px = left + pad;
					py += pillH + 4;
				}
				l.reactions.push_back(BRect(px, py, px + w - 1, py + pillH - 1));
				px += w + 4;
			}
		}

		// Where the links are, for clicks.
		for (const pp::Span& span : row.spans) {
			if ((span.style & pp::kLink) == 0)
				continue;
			for (size_t li = 0; li < l.lineChars.size(); li++) {
				int32 c0 = l.lineChars[li];
				int32 c1 = li + 1 < l.lineChars.size() ? l.lineChars[li + 1]
					: (int32)measured.widths.size();
				float x = left + pad, x0 = -1, x1 = -1;
				for (int32 c = c0; c < c1; c++) {
					int32 b = measured.offsets[c];
					if (b >= span.start && b < span.start + span.length) {
						if (x0 < 0)
							x0 = x;
						x1 = x + measured.widths[c];
					}
					x += measured.widths[c];
				}
				if (x0 >= 0) {
					float top = l.textTop + line * li;
					l.links.push_back({BRect(x0, top, x1, top + line - 1), span.url});
				}
			}
		}
		fLayout.push_back(l);
		y += height + pad;
	}
	fContentHeight = y;
	bool relayouting = fRelayouting;
	fRelayouting = true;
	_UpdateScrollBar();
	fRelayouting = relayouting;
}


MessageView::Anchor
MessageView::_SaveAnchor(float viewBottom) const
{
	Anchor anchor;
	anchor.atEnd = fAtEnd || fLayout.empty();
	if (anchor.atEnd)
		return anchor;
	// The last real message (not a date) that reaches into the view.
	for (size_t i = fLayout.size(); i-- > 0;) {
		if (fRows[i].id == 0 || fLayout[i].bubble.top >= viewBottom)
			continue;
		anchor.id = fRows[i].id;
		anchor.fromBottom = viewBottom - fLayout[i].bubble.bottom;
		return anchor;
	}
	anchor.atEnd = true;
	return anchor;
}


void
MessageView::_RestoreAnchor(const Anchor& anchor)
{
	float height = Bounds().Height();
	float maxTop = std::max(0.0f, fContentHeight - height);
	float top = maxTop;
	if (!anchor.atEnd) {
		for (size_t i = 0; i < fRows.size(); i++) {
			if (fRows[i].id == anchor.id) {
				top = fLayout[i].bubble.bottom + anchor.fromBottom - height;
				break;
			}
		}
	}
	top = std::min(std::max(0.0f, roundf(top)), maxTop);
	fRelayouting = true;
	ScrollTo(BPoint(0, top));
	fRelayouting = false;
	fAtEnd = anchor.atEnd || fContentHeight - Bounds().bottom < 4;
	fLastHeight = height;
}


void
MessageView::_UpdateScrollBar()
{
	BScrollBar* bar = ScrollBar(B_VERTICAL);
	if (bar == nullptr)
		return;
	float visible = Bounds().Height();
	float range = std::max(0.0f, fContentHeight - visible);
	bar->SetRange(0, range);
	bar->SetProportion(fContentHeight > 0 ? std::min(1.0f, visible / fContentHeight) : 1);
	font_height fh;
	GetFontHeight(&fh);
	bar->SetSteps(ceilf(fh.ascent + fh.descent) * 3, std::max(visible - 20, 20.0f));
}


void
MessageView::ScrollTo(BPoint where)
{
	// The jump button stays at the bottom right of what's visible: redraw
	// where it was and where it goes (scrolling moves its pixels too).
	BRect oldButton = _JumpButtonFrame();
	BView::ScrollTo(where);
	Invalidate(oldButton);
	Invalidate(_JumpButtonFrame());
	// The scroll bar calls this while we re-layout (SetRange clamps its
	// value): that's not the reader moving.
	if (fRelayouting)
		return;
	fAtEnd = fContentHeight - Bounds().bottom < 4;
	if (fAtEnd && fMoreBelow && Window() != nullptr)
		Window()->PostMessage(kMsgLoadNewer);
	// At the top of a history taller than the view: ask for older messages.
	if (Window() != nullptr && where.y <= 0 && !fRows.empty()
		&& fContentHeight > Bounds().Height())
		Window()->PostMessage(kMsgLoadOlder);
}


int64
MessageView::BottomMessage(float* fromBottom, bool* atEnd) const
{
	*atEnd = fAtEnd;
	bool saved = fAtEnd;
	const_cast<MessageView*>(this)->fAtEnd = false;
	Anchor anchor = _SaveAnchor(Bounds().bottom);
	const_cast<MessageView*>(this)->fAtEnd = saved;
	*fromBottom = anchor.fromBottom;
	return anchor.id;
}


void
MessageView::ScrollToMessage(int64 id)
{
	for (size_t i = 0; i < fRows.size(); i++) {
		if (fRows[i].id == id) {
			ScrollTo(BPoint(0, std::max(0.0f, std::min(fLayout[i].bubble.top - 8,
				fContentHeight - Bounds().Height()))));
			return;
		}
	}
}


const MessageRow*
MessageView::Row(int64 id) const
{
	for (const MessageRow& row : fRows)
		if (row.id == id && id != 0)
			return &row;
	return nullptr;
}


void
MessageView::MouseDown(BPoint where)
{
	MakeFocus(false);
	uint32 buttons = 0;
	if (BMessage* current = Window()->CurrentMessage())
		current->FindInt32("buttons", (int32*)&buttons);
	if ((buttons & B_PRIMARY_MOUSE_BUTTON) != 0) {
		if ((!fAtEnd || fMoreBelow) && _JumpButtonFrame().Contains(where)) {
			Window()->PostMessage(kMsgJumpToLatest);
			return;
		}
		for (size_t i = 0; i < fLayout.size(); i++) {
			if (!fLayout[i].bubble.Contains(where))
				continue;
			for (size_t k = 0; k < fLayout[i].reactions.size()
					&& k < fRows[i].reactions.size(); k++) {
				if (fLayout[i].reactions[k].Contains(where)) {
					// Our reaction comes off; anyone else's is added for us.
					BMessage react(kMsgReact);
					react.AddInt64("message_id", fRows[i].id);
					react.AddString("emoji", fRows[i].reactions[k].emoji);
					react.AddBool("add", !fRows[i].reactions[k].chosen);
					Window()->PostMessage(&react);
					return;
				}
			}
			for (const Layout::Link& link : fLayout[i].links) {
				if (link.frame.Contains(where)) {
					BMessage open(kMsgOpenUrl);
					open.AddString("url", link.url.c_str());
					Window()->PostMessage(&open);
					return;
				}
			}
			// A spoiler in the text: the first click shows it.
			bool spoiler = false;
			for (const pp::Span& span : fRows[i].spans)
				spoiler |= (span.style & pp::kSpoiler) != 0;
			if (spoiler && !fRevealed.count(fRows[i].id)
				&& fRows[i].mediaKind == 0) {
				fRevealed.insert(fRows[i].id);
				Invalidate(fLayout[i].bubble);
				return;
			}
		}
		for (size_t i = 0; i < fRows.size(); i++) {
			const MessageRow& row = fRows[i];
			if (row.mediaKind == 0 || row.mediaKind == 2
				|| !fLayout[i].media.Contains(where))
				continue;
			if (row.spoiler && !row.secret && !fRevealed.count(row.id)) {
				fRevealed.insert(row.id);  // show it in place
				Invalidate(fLayout[i].media);
				return;
			}
			BMessage open(kMsgOpenMedia);
			open.AddInt64("message_id", row.id);
			Window()->PostMessage(&open);
			return;
		}
		return;
	}
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0)
		return;
	for (size_t i = 0; i < fRows.size(); i++) {
		if (fRows[i].id == 0 || fRows[i].service || !fLayout[i].bubble.Contains(where))
			continue;
		BMessage menu(kMsgMessageMenu);
		menu.AddInt64("message_id", fRows[i].id);
		menu.AddPoint("where", ConvertToScreen(where));
		Window()->PostMessage(&menu);
		return;
	}
}


void
MessageView::_DrawText(const MessageRow& row, const Layout& l, float x, float line,
	rgb_color fg, rgb_color fill)
{
	// Each line drawn in runs of the same style: fonts, link colour,
	// underline and strike-through, code on a tint, spoilers as a bar.
	const Measured& m = _MeasureRow(row);
	int32 chars = (int32)m.widths.size();
	std::vector<uint32> styles(chars, 0);
	for (const pp::Span& span : row.spans)
		for (int32 c = 0; c < chars; c++)
			if (m.offsets[c] >= span.start && m.offsets[c] < span.start + span.length)
				styles[c] |= span.style;
	bool revealed = fRevealed.count(row.id) > 0;
	rgb_color accent = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
	font_height fh;
	GetFontHeight(&fh);
	const char* bytes = row.text.String();

	for (size_t li = 0; li < l.lineChars.size(); li++) {
		int32 c0 = l.lineChars[li];
		int32 c1 = li + 1 < l.lineChars.size() ? l.lineChars[li + 1] : chars;
		float baseline = l.textTop + line * li + fh.ascent;
		float cx = x;
		for (int32 c = c0; c < c1;) {
			int32 end = c + 1;
			while (end < c1 && styles[end] == styles[c])
				end++;
			// A line's own newline isn't drawn.
			int32 drawEnd = end;
			while (drawEnd > c && bytes[m.offsets[drawEnd - 1]] == '\n')
				drawEnd--;
			float w = 0;
			for (int32 k = c; k < end; k++)
				w += m.widths[k];
			uint32 st = styles[c];
			BFont font;
			_FontFor(st, &font);
			SetFont(&font);
			BRect box(cx, baseline - fh.ascent, cx + w - 1, baseline + fh.descent);
			if (st & pp::kCode) {
				SetHighColor(mix_color(fill, fg, 25));
				FillRect(box);
			}
			if ((st & pp::kSpoiler) && !revealed) {
				SetHighColor(mix_color(fill, fg, 140));
				FillRoundRect(box, 3, 3);
			} else {
				rgb_color color = (st & pp::kLink) ? accent
					: (st & pp::kQuote) ? mix_color(fg, fill, 60) : fg;
				SetHighColor(color);
				SetLowColor(fill);
				if (drawEnd > c)
					DrawString(bytes + m.offsets[c], m.offsets[drawEnd] - m.offsets[c],
						BPoint(cx, baseline));
				if (st & (pp::kUnderline | pp::kLink))
					StrokeLine(BPoint(cx, baseline + 1), BPoint(cx + w - 1, baseline + 1));
				if (st & pp::kStrike) {
					float mid = baseline - fh.ascent * 0.32f;
					StrokeLine(BPoint(cx, mid), BPoint(cx + w - 1, mid));
				}
			}
			cx += w;
			c = end;
		}
	}
	SetFont((&fPlain));
}


void
MessageView::FrameResized(float width, float height)
{
	// The layout still has the old width; the anchor is taken from where
	// the old bottom edge was (the top doesn't move when a view resizes).
	float oldBottom = Bounds().top + fLastHeight;
	Anchor anchor = _SaveAnchor(oldBottom);
	BView::FrameResized(width, height);
	_Relayout();
	_RestoreAnchor(anchor);
}


void
MessageView::Draw(BRect updateRect)
{
	rgb_color bg = ui_color(B_LIST_BACKGROUND_COLOR);
	rgb_color fg = ui_color(B_LIST_ITEM_TEXT_COLOR);
	rgb_color dim = mix_color(fg, bg, 110);
	rgb_color incoming = mix_color(bg, fg, 22);
	rgb_color outgoing = mix_color(bg, ui_color(B_CONTROL_HIGHLIGHT_COLOR), 70);

	SetLowColor(bg);
	FillRect(updateRect, B_SOLID_LOW);

	if (fRows.empty()) {
		SetHighColor(dim);
		BRect b = Bounds();
		float w = StringWidth(fEmptyText.String());
		DrawString(fEmptyText.String(), BPoint(b.left + (b.Width() - w) / 2,
			b.top + b.Height() / 2));
		return;
	}

	font_height fh;
	GetFontHeight(&fh);
	float line = ceilf(fh.ascent + fh.descent + fh.leading);
	float pad = roundf(line * 0.5f);

	SetDrawingMode(B_OP_ALPHA);
	for (size_t i = 0; i < fRows.size(); i++) {
		const MessageRow& row = fRows[i];
		const Layout& l = fLayout[i];
		if (!l.bubble.Intersects(updateRect))
			continue;

		bool bareSticker = (row.mediaKind == 2 || row.mediaKind == 5) && row.sender.IsEmpty()
			&& row.forward.IsEmpty() && row.replyText.IsEmpty()
			&& row.replySender.IsEmpty() && row.text.IsEmpty();
		rgb_color fill = row.service || bareSticker ? bg
			: row.outgoing ? outgoing : incoming;
		SetHighColor(fill);
		if (!bareSticker)
			FillRoundRect(l.bubble, pad * 1.4f, pad * 1.4f);
		if (row.id != 0 && row.id == fHighlight) {
			SetHighColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR));
			SetPenSize(2);
			StrokeRoundRect(l.bubble.InsetByCopy(1, 1), pad * 1.4f, pad * 1.4f);
			SetPenSize(1);
		}
		SetLowColor(fill);

		if (row.mediaKind != 0)
			_DrawMedia(row, l, fg, line);

		// The sender's picture at the bottom of their run of messages.
		if (row.showAvatar) {
			bool lastOfRun = i + 1 >= fRows.size() || !fRows[i + 1].showAvatar
				|| fRows[i + 1].senderKey != row.senderKey || fRows[i + 1].id == 0;
			if (lastOfRun) {
				float size = roundf(line * 2.4f);
				BRect circle(pad * 2, l.bubble.bottom - size, pad * 2 + size - 1, l.bubble.bottom - 1);
				_DrawAvatar(row, circle);
				SetLowColor(fill);
			}
		}

		float x = l.bubble.left + pad;
		float inner = l.bubble.Width() - 2 * pad;
		rgb_color accent = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
		if (!row.sender.IsEmpty()) {
			SetFont((&fBold));
			SetHighColor(accent);
			DrawString(row.sender.String(), BPoint(x, l.bubble.top + pad + fh.ascent));
			SetFont((&fPlain));
		}
		if (!row.forward.IsEmpty() && !row.service) {
			BString label(row.forward);
			(&fPlain)->TruncateString(&label, B_TRUNCATE_END, inner);
			SetHighColor(accent);
			DrawString(label.String(), BPoint(x, l.forwardTop + fh.ascent));
		}
		if ((!row.replyText.IsEmpty() || !row.replySender.IsEmpty()) && !row.service) {
			// A bar and two lines, like Paper Plane's reply block.
			float barWidth = std::max(2.0f, roundf(pad / 3));
			SetHighColor(accent);
			FillRect(BRect(x, l.replyTop, x + barWidth - 1, l.replyTop + line * 2 - 1));
			float rx = x + barWidth + pad * 0.6f;
			BString who(row.replySender);
			(&fBold)->TruncateString(&who, B_TRUNCATE_END, inner - (rx - x));
			SetFont((&fBold));
			DrawString(who.String(), BPoint(rx, l.replyTop + fh.ascent));
			SetFont((&fPlain));
			BString what(row.replyText);
			what.ReplaceAll('\n', ' ');
			(&fPlain)->TruncateString(&what, B_TRUNCATE_END, inner - (rx - x));
			SetHighColor(fg);
			DrawString(what.String(), BPoint(rx, l.replyTop + line + fh.ascent));
		}

		if (row.service || row.spans.empty()) {
			SetHighColor(row.service ? dim : fg);
			float y = l.textTop + fh.ascent;
			for (const BString& s : l.lines) {
				float lx = row.service
					? l.bubble.left + (l.bubble.Width() - StringWidth(s.String())) / 2 : x;
				DrawString(s.String(), BPoint(lx, y));
				y += line;
			}
		} else
			_DrawText(row, l, x, line, fg, fill);
		if (row.service)
			continue;

		// Reactions: "👍 3" pills, ours highlighted. A whole font size of at
		// least 11 pt: Renku's app_server misses some fallback emoji at others.
		if (!row.reactions.empty()) {
			BFont pillFont((&fPlain));
			pillFont.SetSize(std::max(11.0f, floorf((&fPlain)->Size())));
			font_height pfh;
			pillFont.GetHeight(&pfh);
			for (size_t k = 0; k < row.reactions.size() && k < l.reactions.size(); k++) {
				const MessageRow::Reaction& r = row.reactions[k];
				BRect pill = l.reactions[k];
				rgb_color pillFill = r.chosen ? accent : mix_color(fill, fg, 30);
				SetHighColor(pillFill);
				FillRoundRect(pill, pill.Height() / 2, pill.Height() / 2);
				BString label(r.emoji);
				label << " " << r.count;
				SetFont(&pillFont);
				SetLowColor(pillFill);
				SetHighColor(r.chosen ? make_color(255, 255, 255) : fg);
				DrawString(label.String(), BPoint(pill.left + pill.Height() / 2,
					pill.top + (pill.Height() + pfh.ascent - pfh.descent) / 2));
				SetFont((&fPlain));
				SetLowColor(fill);
			}
		}

		// Time, "edited", and the sending state, bottom right.
		BString meta;
		if (row.edited)
			meta << "edited ";
		meta << row.time;
		if (row.outgoing)
			meta << (row.failed ? "  !" : row.pending ? "  …" : row.read ? "  ✓✓" : "  ✓");
		SetHighColor(row.failed ? ui_color(B_FAILURE_COLOR) : dim);
		DrawString(meta.String(), BPoint(l.bubble.right - pad - StringWidth(meta.String()),
			l.bubble.bottom - pad * 0.5f - fh.descent));
		SetLowColor(bg);
	}

	if (!fAtEnd || fMoreBelow) {
		// A round "down" button, with the unread count above it.
		BRect frame = _JumpButtonFrame();
		float size = frame.Width();
		BRect circle(frame.left, frame.bottom - size, frame.right, frame.bottom);
		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		FillEllipse(circle);
		SetHighColor(mix_color(fg, bg, 160));
		StrokeEllipse(circle);
		SetHighColor(fg);
		BPoint c(circle.left + size / 2, circle.top + size / 2);
		float a = size * 0.18f;
		SetPenSize(2);
		StrokeLine(BPoint(c.x - a, c.y - a * 0.5f), BPoint(c.x, c.y + a * 0.5f));
		StrokeLine(BPoint(c.x, c.y + a * 0.5f), BPoint(c.x + a, c.y - a * 0.5f));
		SetPenSize(1);
		if (fUnreadCount > 0) {
			BString badge;
			badge << fUnreadCount;
			font_height bfh;
			(&fPlain)->GetHeight(&bfh);
			float h = ceilf(bfh.ascent + bfh.descent) + 2;
			float w = std::max(h, (&fPlain)->StringWidth(badge.String()) + h * 0.7f);
			BRect pill(c.x - w / 2, circle.top - h / 2, c.x + w / 2, circle.top + h / 2);
			SetHighColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR));
			FillRoundRect(pill, h / 2, h / 2);
			SetHighColor(255, 255, 255);
			SetLowColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR));
			SetFont((&fPlain));
			DrawString(badge.String(), BPoint(c.x - (&fPlain)->StringWidth(badge.String()) / 2,
				pill.top + 1 + bfh.ascent));
		}
	}

	if (fDropTarget) {
		BRect b = Bounds().InsetByCopy(2, 2);
		SetHighColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR));
		SetPenSize(3);
		StrokeRoundRect(b, pad, pad);
		SetPenSize(1);
	}
}
