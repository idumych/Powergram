// SPDX-License-Identifier: GPL-3.0-or-later
#include "SendFilesWindow.h"

#include <Bitmap.h>
#include <BitmapStream.h>
#include <Button.h>
#include <Clipboard.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <NodeInfo.h>
#include <Path.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>

#include <cmath>
#include <ctime>
#include <string>

#include "../core/Compose.h"
#include "Messages.h"

namespace {

enum {
	kMsgEdited = 'sfed',
	kMsgSend = 'sfsn'
};


// A file: its picture (or icon), name, and size.
class FileItem : public BListItem {
public:
	FileItem(const entry_ref& ref, float side)
		: fName(ref.name), fSide(side)
	{
		BEntry entry(&ref, true);
		off_t size = 0;
		entry.GetSize(&size);
		char text[32];
		if (size < 1024)
			snprintf(text, sizeof(text), "%lld bytes", (long long)size);
		else if (size < 1024 * 1024)
			snprintf(text, sizeof(text), "%.0f KB", size / 1024.0);
		else
			snprintf(text, sizeof(text), "%.1f MB", size / 1048576.0);
		fDetail = text;
		// Pictures as themselves, scaled down once; anything else by its icon.
		if (BBitmap* picture = BTranslationUtils::GetBitmap(&ref)) {
			BRect b = picture->Bounds();
			float scale = std::min(side / (b.Width() + 1), side / (b.Height() + 1));
			fDetail << ", " << b.IntegerWidth() + 1 << " × " << b.IntegerHeight() + 1;
			BRect thumb(0, 0, std::max(1.0f, roundf((b.Width() + 1) * scale)) - 1,
				std::max(1.0f, roundf((b.Height() + 1) * scale)) - 1);
			fThumb = new BBitmap(thumb, B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
			BView* view = new BView(thumb, "thumb", B_FOLLOW_NONE, 0);
			fThumb->AddChild(view);
			fThumb->Lock();
			view->DrawBitmap(picture, b, thumb, B_FILTER_BITMAP_BILINEAR);
			view->Sync();
			fThumb->RemoveChild(view);
			fThumb->Unlock();
			delete view;
			delete picture;
		} else {
			fThumb = new BBitmap(BRect(0, 0, 31, 31), B_RGBA32);
			BNode node(&entry);
			BNodeInfo info(&node);
			if (info.InitCheck() != B_OK || info.GetTrackerIcon(fThumb, B_LARGE_ICON) != B_OK) {
				delete fThumb;
				fThumb = nullptr;
			}
		}
	}

	~FileItem() override { delete fThumb; }

	void Update(BView* owner, const BFont* font) override
	{
		BListItem::Update(owner, font);
		SetHeight(fSide + 8);
	}

	void DrawItem(BView* owner, BRect frame, bool) override
	{
		owner->SetHighUIColor(B_LIST_BACKGROUND_COLOR);
		owner->FillRect(frame);
		if (fThumb != nullptr) {
			BRect b = fThumb->Bounds();
			BPoint at(frame.left + 4 + (fSide - b.Width() - 1) / 2,
				frame.top + 4 + (fSide - b.Height() - 1) / 2);
			owner->SetDrawingMode(B_OP_ALPHA);
			owner->DrawBitmap(fThumb, at);
			owner->SetDrawingMode(B_OP_COPY);
		}
		font_height fh;
		be_plain_font->GetHeight(&fh);
		float line = ceilf(fh.ascent + fh.descent + fh.leading);
		float x = frame.left + fSide + 14;
		float y = frame.top + (frame.Height() - 2 * line) / 2 + fh.ascent;
		BString name(fName);
		be_bold_font->TruncateString(&name, B_TRUNCATE_MIDDLE, frame.right - x - 6);
		owner->SetFont(be_bold_font);
		owner->SetHighUIColor(B_LIST_ITEM_TEXT_COLOR);
		owner->DrawString(name.String(), BPoint(x, y));
		owner->SetFont(be_plain_font);
		owner->SetHighUIColor(B_LIST_ITEM_TEXT_COLOR, B_DARKEN_2_TINT);
		owner->DrawString(fDetail.String(), BPoint(x, y + line));
	}

private:
	BString fName;
	BString fDetail;
	BBitmap* fThumb = nullptr;
	float fSide;
};


// Writes `bitmap` as a PNG file; false where there's no PNG translator.
bool
SavePng(BBitmap* bitmap, const char* path)
{
	BBitmapStream stream(bitmap);
	BFile file(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck() == B_OK
		? BTranslatorRoster::Default()->Translate(&stream, nullptr, nullptr, &file, B_PNG_FORMAT)
		: B_ERROR;
	BBitmap* back = nullptr;
	stream.DetachBitmap(&back);
	if (status != B_OK)
		BEntry(path).Remove();
	else
		BNodeInfo(&file).SetType("image/png");
	return status == B_OK;
}


BString
PastedPath(const char* folder, const std::string& extension, int32 index)
{
	time_t now = time(nullptr);
	struct tm t;
	localtime_r(&now, &t);
	std::string name = pp::PastedImageName(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour,
		t.tm_min, t.tm_sec, extension);
	if (index > 0)
		name.insert(name.rfind('.'), " " + std::to_string(index + 1));
	BPath path(folder, name.c_str());
	return path.Path();
}

} // namespace


bool
ClipboardFiles(const char* folder, BMessage* refs)
{
	if (!be_clipboard->Lock())
		return false;
	BMessage* clip = be_clipboard->Data();
	bool found = false;
	bool made = false;   // files written here (not Tracker's)
	create_directory(folder, 0700);
	// Files copied in Tracker.
	entry_ref ref;
	for (int32 i = 0; clip != nullptr && clip->FindRef("refs", i, &ref) == B_OK; i++) {
		refs->AddRef("refs", &ref);
		found = true;
	}
	// A bitmap, as ShowImage copies a picture (or its selection).
	BMessage archive;
	if (!found && clip != nullptr && clip->FindMessage("image/bitmap", &archive) == B_OK) {
		BBitmap* bitmap = dynamic_cast<BBitmap*>(BBitmap::Instantiate(&archive));
		BString path = PastedPath(folder, "png", 0);
		if (bitmap != nullptr && SavePng(bitmap, path.String())
			&& get_ref_for_path(path.String(), &ref) == B_OK) {
			refs->AddRef("refs", &ref);
			found = made = true;
		}
		delete bitmap;
	}
	// Pictures as data of their type (image/png, image/jpeg…).
	char* name;
	type_code type;
	int32 count;
	for (int32 i = 0; !found && clip != nullptr
			&& clip->GetInfo(B_MIME_TYPE, i, &name, &type, &count) == B_OK; i++) {
		std::string extension = pp::PastedImageExtension(name);
		const void* data;
		ssize_t size;
		if (extension.empty() || clip->FindData(name, B_MIME_TYPE, &data, &size) != B_OK
			|| size <= 0)
			continue;
		BString path = PastedPath(folder, extension, 0);
		BFile file(path.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		if (file.Write(data, size) != size) {
			BEntry(path.String()).Remove();
			continue;
		}
		BNodeInfo(&file).SetType(name);
		if (get_ref_for_path(path.String(), &ref) == B_OK) {
			refs->AddRef("refs", &ref);
			found = made = true;
		}
	}
	be_clipboard->Unlock();
	if (found)
		refs->AddBool("pasted", made);
	return found;
}


void
CleanPastedFiles(const char* folder)
{
	BDirectory directory(folder);
	BEntry entry;
	time_t now = time(nullptr);
	while (directory.GetNextEntry(&entry) == B_OK) {
		time_t modified = 0;
		if (entry.GetModificationTime(&modified) == B_OK && now - modified > 86400)
			entry.Remove();
	}
}


SendFilesWindow::SendFilesWindow(BMessenger target, BMessage* send, const char* caption,
	BRect parentFrame)
	:
	BWindow(BRect(0, 0, 380, 300), "Send", B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fSend(send)
{
	float em = be_plain_font->Size();
	fList = new BListView("files");
	entry_ref ref;
	int32 count = 0, pictures = 0;
	for (; send->FindRef("refs", count, &ref) == B_OK; count++) {
		BEntry entry(&ref, true);
		BNode node(&entry);
		char type[B_MIME_TYPE_LENGTH] = "";
		BNodeInfo(&node).GetType(type);
		if (BString(type).StartsWith("image/"))
			pictures++;
		fList->AddItem(new FileItem(ref, roundf(em * 3.5f)));
	}
	BString title;
	if (pictures == count)
		title << "Send " << count << (count == 1 ? " picture" : " pictures");
	else
		title << "Send " << count << (count == 1 ? " file" : " files");
	SetTitle(title);
	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, count > 4);
	float rows = std::min<int32>(std::max<int32>(count, 1), 4);
	scroll->SetExplicitMinSize(BSize(em * 26, rows * (roundf(em * 3.5f) + 8) + 4));
	fCaption = new BTextControl("caption", "Caption:", caption, nullptr);
	fCaption->SetModificationMessage(new BMessage(kMsgEdited));
	fCaption->SetToolTip(count > 1 ? "Goes with the first file" : nullptr);
	fProblem = new BStringView("problem", "");
	fProblem->SetHighUIColor(B_FAILURE_COLOR);
	fSendButton = new BButton("send", "Send", new BMessage(kMsgSend));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(scroll, 1.0f)
		.Add(fCaption)
		.Add(fProblem)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)))
			.Add(fSendButton)
		.End();
	SetDefaultButton(fSendButton);
	_Check();
	fCaption->MakeFocus(true);
	fCaption->TextView()->SelectAll();
	CenterIn(parentFrame);
}


SendFilesWindow::~SendFilesWindow()
{
	delete fSend;
}


bool
SendFilesWindow::QuitRequested()
{
	// Pictures pasted for this and not sent: gone.
	if (!fSent && fSend->GetBool("pasted", false)) {
		entry_ref ref;
		for (int32 i = 0; fSend->FindRef("refs", i, &ref) == B_OK; i++)
			BEntry(&ref).Remove();
	}
	return true;
}


void
SendFilesWindow::_Check()
{
	std::string problem = pp::CaptionProblem(fCaption->Text());
	fProblem->SetText(problem.c_str());
	fSendButton->SetEnabled(problem.empty());
}


void
SendFilesWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgEdited:
			_Check();
			break;
		case kMsgSend:
			_Check();
			if (!fSendButton->IsEnabled())
				break;
			fSend->RemoveName("caption");
			fSend->AddString("caption", fCaption->Text());
			fTarget.SendMessage(fSend);
			fSent = true;
			PostMessage(B_QUIT_REQUESTED);
			break;
		default:
			BWindow::MessageReceived(message);
	}
}
