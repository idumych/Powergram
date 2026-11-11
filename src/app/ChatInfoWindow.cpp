// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChatInfoWindow.h"

#include <Application.h>
#include <Bitmap.h>
#include <DataIO.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextView.h>
#include <TranslationUtils.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "BitmapUtils.h"
#include "Messages.h"

namespace {

const uint32 kMsgGrow = 'grow';

const rgb_color kAvatarColors[] = {
	{0xe1, 0x70, 0x76, 255}, {0xfa, 0xa7, 0x74, 255}, {0xa6, 0x95, 0xe7, 255},
	{0x7b, 0xc8, 0x62, 255}, {0x6e, 0xc9, 0xcb, 255}, {0x65, 0xaa, 0xdd, 255},
	{0xee, 0x7a, 0xae, 255}
};


// A downloaded image, or else the tiny inline preview; nullptr if neither.
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

} // namespace


// The chat's picture, round, or its initials on a colour.
class AvatarView : public BView {
public:
	AvatarView()
		:
		BView("avatar", B_WILL_DRAW)
	{
		float side = roundf(be_plain_font->Size() * 5);
		SetExplicitSize(BSize(side, side));
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	~AvatarView() override { delete fBitmap; }

	void Set(int64 id, const BString& path, const std::string& preview,
		const BString& initials)
	{
		if (path == fPath && !fPath.IsEmpty() && fBitmap != nullptr)
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

	void Draw(BRect) override
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

private:
	int64 fId = 0;
	BString fPath;
	BString fInitials;
	BBitmap* fBitmap = nullptr;
};


// Shared photos as square thumbnails, as many to a row as fit.
class PhotoGrid : public BView {
public:
	PhotoGrid(int64 chatId)
		:
		BView("photos", B_WILL_DRAW | B_FRAME_EVENTS),
		fChatId(chatId)
	{
		SetViewUIColor(B_LIST_BACKGROUND_COLOR);
		fCell = roundf(be_plain_font->Size() * 6.5f);
		SetExplicitMinSize(BSize(fCell * 3 + kGap * 4, fCell + kGap * 2));
	}

	~PhotoGrid() override
	{
		for (auto& [id, entry] : fBitmaps)
			delete entry.second;
	}

	struct Photo {
		int64 id;
		BString path;
		std::string preview;
	};

	void Set(const std::vector<Photo>& photos)
	{
		fPhotos = photos;
		// Keep thumbnails that haven't changed; load the rest.
		std::map<int64, std::pair<BString, BBitmap*>> kept;
		for (const Photo& p : fPhotos) {
			auto it = fBitmaps.find(p.id);
			if (it != fBitmaps.end() && (it->second.first == p.path || p.path.IsEmpty())) {
				kept[p.id] = it->second;
				fBitmaps.erase(it);
			} else
				kept[p.id] = {p.path, LoadImage(p.path, p.preview)};
		}
		for (auto& [id, entry] : fBitmaps)
			delete entry.second;
		fBitmaps.swap(kept);
		_UpdateScrollBar();
		Invalidate();
	}

	void FrameResized(float width, float height) override
	{
		BView::FrameResized(width, height);
		_UpdateScrollBar();
		Invalidate();
	}

	void Draw(BRect update) override
	{
		int columns = _Columns();
		for (size_t i = 0; i < fPhotos.size(); i++) {
			BRect cell = _Cell(i, columns);
			if (!cell.Intersects(update))
				continue;
			BBitmap* bitmap = fBitmaps[fPhotos[i].id].second;
			if (bitmap == nullptr) {
				SetHighColor(tint_color(ViewColor(), B_DARKEN_1_TINT));
				FillRect(cell);
				continue;
			}
			// Centre-cropped to the square.
			BRect b = bitmap->Bounds();
			float side = std::min(b.Width(), b.Height());
			BRect source(b.left + (b.Width() - side) / 2, b.top + (b.Height() - side) / 2, 0, 0);
			source.right = source.left + side;
			source.bottom = source.top + side;
			DrawBitmap(bitmap, source, cell, B_FILTER_BITMAP_BILINEAR);
		}
	}

	void MouseDown(BPoint where) override
	{
		int columns = _Columns();
		for (size_t i = 0; i < fPhotos.size(); i++) {
			if (!_Cell(i, columns).Contains(where))
				continue;
			BMessage open(kMsgOpenMedia);
			open.AddInt64("chat_id", fChatId);
			open.AddInt64("message_id", fPhotos[i].id);
			be_app->PostMessage(&open);
			return;
		}
	}

private:
	static constexpr float kGap = 4;

	int _Columns() const
	{
		return std::max(1, (int)((Bounds().Width() - kGap) / (fCell + kGap)));
	}

	BRect _Cell(size_t index, int columns) const
	{
		float x = kGap + (index % columns) * (fCell + kGap);
		float y = kGap + (index / columns) * (fCell + kGap);
		return BRect(x, y, x + fCell - 1, y + fCell - 1);
	}

	void _UpdateScrollBar()
	{
		BScrollBar* bar = ScrollBar(B_VERTICAL);
		if (bar == nullptr)
			return;
		int columns = _Columns();
		int rows = ((int)fPhotos.size() + columns - 1) / columns;
		float content = kGap + rows * (fCell + kGap);
		float visible = Bounds().Height();
		bar->SetRange(0, std::max(0.0f, content - visible));
		bar->SetProportion(content > 0 ? std::min(1.0f, visible / content) : 1);
		bar->SetSteps(fCell / 4, std::max(fCell, visible - fCell));
	}

	int64 fChatId;
	float fCell;
	std::vector<Photo> fPhotos;
	std::map<int64, std::pair<BString, BBitmap*>> fBitmaps;  // path loaded, bitmap
};


ChatInfoWindow::ChatInfoWindow(int64 chatId)
	:
	BWindow(BRect(0, 0, 380, 560), "Info", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fChatId(chatId)
{
	fAvatar = new AvatarView();
	fTitle = new BStringView("title", "");
	BFont big(be_bold_font);
	big.SetSize(roundf(be_bold_font->Size() * 1.3f));
	fTitle->SetFont(&big);
	fTitle->SetTruncation(B_TRUNCATE_END);
	fTitle->SetExplicitMinSize(BSize(be_plain_font->Size() * 8, B_SIZE_UNSET));
	fStatus = new BStringView("status", "");
	fStatus->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
	fStatus->SetTruncation(B_TRUNCATE_END);
	fStatus->SetExplicitMinSize(BSize(be_plain_font->Size() * 8, B_SIZE_UNSET));

	auto label = [](const char* name) {
		BStringView* view = new BStringView(name, "");
		view->SetFont(be_bold_font);
		return view;
	};
	fAboutLabel = label("about label");
	fAbout = new BTextView("about");
	fAbout->MakeEditable(false);
	fAbout->SetWordWrap(true);
	fAbout->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fAbout->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	fAbout->SetExplicitMinSize(BSize(be_plain_font->Size() * 16, B_SIZE_UNSET));

	fMembersLabel = label("members label");
	fMembers = new BListView("members");
	fMembersScroll = new BScrollView("members scroll", fMembers, 0, false, true);
	font_height fh;
	be_plain_font->GetHeight(&fh);
	fMembersScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET,
		ceilf(fh.ascent + fh.descent + fh.leading) * 5));

	fPhotosLabel = label("photos label");
	fPhotosLabel->SetText("Shared photos");
	fPhotos = new PhotoGrid(chatId);
	fPhotosScroll = new BScrollView("photos scroll", fPhotos, 0, false, true);

	// Labels and text would otherwise cap the window at their own width.
	for (BView* view : {(BView*)fTitle, (BView*)fStatus, (BView*)fAboutLabel,
			(BView*)fMembersLabel, (BView*)fPhotosLabel})
		view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fAbout->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fMembersScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	fPhotosScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL)
			.Add(fAvatar)
			.AddGroup(B_VERTICAL, 0)
				.AddGlue()
				.Add(fTitle)
				.Add(fStatus)
				.AddGlue()
			.End()
		.End()
		.Add(fAboutLabel)
		.Add(fAbout, 0)
		.Add(fMembersLabel)
		.Add(fMembersScroll, 1)
		.Add(fPhotosLabel)
		.Add(fPhotosScroll, 2);

	// Until the app's reply: nothing but the picture and name.
	fAboutLabel->Hide();
	fAbout->Hide();
	fMembersLabel->Hide();
	fMembersScroll->Hide();
	fPhotosLabel->Hide();
	fPhotosScroll->Hide();
}


void
ChatInfoWindow::_Update(const BMessage* info)
{
	BString title = info->GetString("title", "");
	SetTitle(title.IsEmpty() ? "Info" : title.String());
	fTitle->SetText(title);
	fStatus->SetText(info->GetString("status", ""));
	fAvatar->Set(fChatId, info->GetString("avatar", ""), FindBytes(info, "avatar_mini", 0),
		info->GetString("initials", ""));

	auto show = [](BView* view, bool visible) {
		if (visible && view->IsHidden())
			view->Show();
		else if (!visible && !view->IsHidden())
			view->Hide();
	};

	// A user's bio; a group's or channel's description.
	BString about = info->GetString("about", "");
	fAboutLabel->SetText(info->GetBool("private", false) ? "Bio" : "Description");
	if (about != fAbout->Text())
		fAbout->SetText(about);
	show(fAboutLabel, !about.IsEmpty());
	show(fAbout, !about.IsEmpty());

	int32 count = info->GetInt32("member_count", 0);
	BString members;
	members << "Members (" << count << ")";
	fMembersLabel->SetText(members);
	// Rebuilt only when the names change, so the selection stays.
	std::vector<BString> names;
	const char* name;
	for (int32 i = 0; info->FindString("member", i, &name) == B_OK; i++)
		names.push_back(name);
	bool same = names.size() == (size_t)fMembers->CountItems();
	for (size_t i = 0; same && i < names.size(); i++)
		same = names[i] == static_cast<BStringItem*>(fMembers->ItemAt(i))->Text();
	if (!same) {
		for (int32 i = fMembers->CountItems(); i-- > 0;)
			delete fMembers->RemoveItem(i);
		for (const BString& n : names)
			fMembers->AddItem(new BStringItem(n));
	}
	show(fMembersLabel, !names.empty());
	show(fMembersScroll, !names.empty());

	std::vector<PhotoGrid::Photo> photos;
	int64 id;
	for (int32 i = 0; info->FindInt64("photo_id", i, &id) == B_OK; i++)
		photos.push_back({id, info->GetString("photo_path", i, ""), FindBytes(info, "photo_mini", i)});
	fPhotos->Set(photos);
	show(fPhotosLabel, !photos.empty());
	show(fPhotosScroll, !photos.empty());

	// It opens with nothing to show; once there's more, room for it (the
	// user's size after that).
	if (!fSized && (!names.empty() || !photos.empty())) {
		fSized = true;
		PostMessage(kMsgGrow);  // after the size limits take in the new parts
	}
}


void
ChatInfoWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgGrow:
		{
			float em = be_plain_font->Size();
			ResizeTo(std::max(Bounds().Width(), em * 30), std::max(Bounds().Height(), em * 46));
			break;
		}
		case kMsgChatInfo:
			if (message->GetInt64("chat_id", 0) == fChatId)
				_Update(message);
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


bool
ChatInfoWindow::QuitRequested()
{
	BMessage closed(kMsgChatInfoClosed);
	closed.AddInt64("chat_id", fChatId);
	be_app->PostMessage(&closed);
	return true;
}
