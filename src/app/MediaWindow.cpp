// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaWindow.h"

#include <Bitmap.h>
#include <Button.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <Path.h>
#include <Roster.h>
#include <Screen.h>
#include <Slider.h>
#include <StringView.h>
#include <TranslationUtils.h>

#include <algorithm>

#include "../core/ChatHistory.h"
#include "Messages.h"
#include "VideoPlayback.h"

namespace {

enum {
	kMsgPlayPause = 'mvpp',
	kMsgScrub = 'mvsc',
	kMsgSeek = 'mvsk',
	kMsgTick = 'mvtk',
	kMsgOpenElsewhere = 'mvoe'
};

const bigtime_t kSeekStep = 5000000;

} // namespace


// The photo or the video's frame, as large as fits, on black.
class MediaView : public BView {
public:
	explicit MediaView(MediaWindow* window)
		:
		BView("media", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_NAVIGABLE),
		fWindow(window)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
		SetExplicitMinSize(BSize(160, 120));
	}

	void SetProblem(const char* text) { fProblem = text; Invalidate(); }

	void Draw(BRect) override
	{
		BRect bounds = Bounds();
		SetHighColor(0, 0, 0);
		FillRect(bounds);
		if (fWindow->fPhoto != nullptr)
			_DrawFitted(fWindow->fPhoto, false);
		else if (fWindow->fVideo != nullptr && fWindow->fVideo->LockFrame()) {
			if (fWindow->fVideo->Frame() != nullptr)
				_DrawFitted(fWindow->fVideo->Frame(), true);
			fWindow->fVideo->UnlockFrame();
			fWindow->fVideo->FrameDrawn();
		} else if (!fProblem.IsEmpty()) {
			SetHighColor(220, 220, 220);
			font_height height;
			GetFontHeight(&height);
			float width = StringWidth(fProblem);
			DrawString(fProblem, BPoint((bounds.Width() - width) / 2,
				(bounds.Height() + height.ascent) / 2));
		}
	}

	void MouseDown(BPoint) override
	{
		MakeFocus(true);
		if (fWindow->fVideo != nullptr)
			fWindow->_TogglePlay();
	}

	void KeyDown(const char* bytes, int32 count) override
	{
		switch (bytes[0]) {
			case B_SPACE:
				fWindow->_TogglePlay();
				break;
			case B_LEFT_ARROW:
				fWindow->_SeekBy(-kSeekStep);
				break;
			case B_RIGHT_ARROW:
				fWindow->_SeekBy(kSeekStep);
				break;
			default:
				BView::KeyDown(bytes, count);
		}
	}

private:
	void _DrawFitted(const BBitmap* bitmap, bool enlarge)
	{
		BRect bounds = Bounds();
		BRect source = bitmap->Bounds();
		float scale = std::min((bounds.Width() + 1) / (source.Width() + 1),
			(bounds.Height() + 1) / (source.Height() + 1));
		if (!enlarge)
			scale = std::min(scale, 1.0f);
		float width = (source.Width() + 1) * scale;
		float height = (source.Height() + 1) * scale;
		BRect target(0, 0, width - 1, height - 1);
		target.OffsetTo(floorf((bounds.Width() + 1 - width) / 2),
			floorf((bounds.Height() + 1 - height) / 2));
		DrawBitmap(bitmap, source, target, B_FILTER_BITMAP_BILINEAR);
	}

	MediaWindow* fWindow;
	BString fProblem;
};


MediaWindow::MediaWindow()
	:
	BWindow(BRect(0, 0, 640, 480), "Viewer", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE)
{
	fView = new MediaView(this);
	fPlay = new BButton("play", "Pause", new BMessage(kMsgPlayPause));
	fPosition = new BSlider("position", nullptr, new BMessage(kMsgSeek), 0, 1000,
		B_HORIZONTAL, B_TRIANGLE_THUMB);
	fPosition->SetModificationMessage(new BMessage(kMsgScrub));
	fPosition->SetExplicitMinSize(BSize(be_plain_font->StringWidth("m") * 10, B_SIZE_UNSET));
	fTime = new BStringView("time", "0:00 / 0:00");
	fTime->SetExplicitMinSize(BSize(fTime->StringWidth("0:00:00 / 0:00:00"), B_SIZE_UNSET));
	fOpenElsewhere = new BButton("elsewhere", "Open in another app",
		new BMessage(kMsgOpenElsewhere));
	// Clicks on the controls leave the keys (space, arrows) with the picture.
	for (BView* control : {(BView*)fPlay, (BView*)fPosition, (BView*)fOpenElsewhere})
		control->SetFlags(control->Flags() & ~B_NAVIGABLE);

	fControls = new BView("controls", 0);
	BLayoutBuilder::Group<>(fControls, B_HORIZONTAL)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(fPlay)
		.Add(fPosition, 1)
		.Add(fTime)
		.AddGlue(0)   // stretches when only the button is left (photos)
		.Add(fOpenElsewhere);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fView, 1)
		.Add(fControls);
	// Running but still hidden: a window's looper only starts when it's
	// first shown, and kMsgShowMedia (which shows it) waits for that.
	Hide();
	Show();
}


MediaWindow::~MediaWindow()
{
	_Close();
}


void
MediaWindow::_Close()
{
	delete fTicker;
	fTicker = nullptr;
	if (fVideo != nullptr) {
		fVideo->Close();
		delete fVideo;
		fVideo = nullptr;
	}
	delete fPhoto;
	fPhoto = nullptr;
	fView->SetProblem("");
}


void
MediaWindow::_Open(const BMessage* message)
{
	_Close();
	fPath = message->GetString("path", "");
	int32 kind = message->GetInt32("kind", pp::Media::kPhoto);
	SetTitle(message->GetString("title", "Viewer"));

	// The app the system would open it with, named on the button.
	BString elsewhere("Open in another app");
	entry_ref ref, app;
	if (get_ref_for_path(fPath, &ref) == B_OK && be_roster->FindApp(&ref, &app) == B_OK)
		elsewhere.SetToFormat("Open in %s", app.name);
	fOpenElsewhere->SetLabel(elsewhere);

	BSize size(480, 360);
	bool video = kind != pp::Media::kPhoto;
	if (!video) {
		fPhoto = BTranslationUtils::GetBitmapFile(fPath);
		if (fPhoto != nullptr) {
			BRect bounds = fPhoto->Bounds();
			size = BSize(bounds.Width() + 1, bounds.Height() + 1);
		} else
			fView->SetProblem("Powergram can't show this picture.");
	} else {
		fVideo = new VideoPlayback(BMessenger(this));
		pp::Media::Kind mediaKind = (pp::Media::Kind)kind;
		// GIFs are silent and loop, as in Telegram's own apps.
		if (fVideo->Open(fPath, pp::LoopsInViewer(mediaKind),
				mediaKind == pp::Media::kAnimation) == B_OK) {
			size = fVideo->VideoSize();
			fVideo->Play();
			BMessage tick(kMsgTick);
			fTicker = new BMessageRunner(BMessenger(this), &tick, 200000);
		} else {
			delete fVideo;
			fVideo = nullptr;
			video = false;
			fView->SetProblem("Powergram can't play this video.");
		}
	}
	// Photos have only the button to open them elsewhere.
	// (Asked of each control itself: while the window is hidden, every
	// view in it counts as hidden.)
	for (BView* control : {(BView*)fPlay, (BView*)fPosition, (BView*)fTime}) {
		if (video && control->IsHidden(control))
			control->Show();
		else if (!video && !control->IsHidden(control))
			control->Hide();
	}
	_UpdateControls();
	_FitTo(size);
	fView->Invalidate();
	if (IsHidden())
		Show();
	Activate();
	fView->MakeFocus(true);
}


// Big enough for the media, but not beyond most of the screen.
void
MediaWindow::_FitTo(BSize media)
{
	BRect screen = BScreen(this).Frame();
	float controls = fControls->PreferredSize().height + 1;
	float maxWidth = screen.Width() * 0.85f;
	float maxHeight = screen.Height() * 0.85f - controls;
	float scale = std::min({1.0f, maxWidth / std::max(1.0f, media.width),
		maxHeight / std::max(1.0f, media.height)});
	float width = std::max(media.width * scale, fControls->MinSize().width);
	width = std::max(width, 360.0f);
	float height = std::max(media.height * scale, 120.0f);
	ResizeTo(ceilf(width) - 1, ceilf(height + controls) - 1);
	if (!fPlaced) {
		CenterOnScreen();
		fPlaced = true;
	}
}


void
MediaWindow::_TogglePlay()
{
	if (fVideo == nullptr)
		return;
	if (fVideo->IsPlaying())
		fVideo->Pause();
	else
		fVideo->Play();
	_UpdateControls();
}


void
MediaWindow::_SeekBy(bigtime_t delta)
{
	if (fVideo == nullptr)
		return;
	fVideo->Seek(fVideo->Position() + delta);
	_UpdateControls();
}


void
MediaWindow::_UpdateControls()
{
	if (fVideo == nullptr)
		return;
	fPlay->SetLabel(fVideo->IsPlaying() ? "Pause" : "Play");
	bigtime_t position = fVideo->Position();
	bigtime_t duration = fVideo->Duration();
	if (!fScrubbing && duration > 0)
		fPosition->SetValue((int32)(position * 1000 / duration));
	BString time;
	time << pp::PlayerTime(position / 1e6).c_str() << " / "
		<< pp::PlayerTime(duration / 1e6).c_str();
	fTime->SetText(time);
}


void
MediaWindow::_OpenElsewhere()
{
	entry_ref ref;
	if (get_ref_for_path(fPath, &ref) != B_OK)
		return;
	if (fVideo != nullptr && fVideo->IsPlaying()) {
		fVideo->Pause();
		_UpdateControls();
	}
	be_roster->Launch(&ref);
}


void
MediaWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgShowMedia:
			_Open(message);
			break;
		case kMsgPlaybackFrame:
			fView->Invalidate();
			break;
		case kMsgPlaybackEnded:
		case kMsgTick:
			_UpdateControls();
			break;
		case kMsgPlayPause:
			_TogglePlay();
			break;
		case kMsgScrub:
			if (fVideo != nullptr) {
				fScrubbing = true;
				fVideo->Seek(fVideo->Duration() * fPosition->Value() / 1000);
				_UpdateControls();
			}
			break;
		case kMsgSeek:
			if (fVideo != nullptr) {
				fScrubbing = false;
				fVideo->Seek(fVideo->Duration() * fPosition->Value() / 1000);
				_UpdateControls();
			}
			break;
		case kMsgOpenElsewhere:
			_OpenElsewhere();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}
