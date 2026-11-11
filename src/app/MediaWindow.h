// SPDX-License-Identifier: GPL-3.0-or-later
// Powergram's own viewer: shows a photo, or plays a video, GIF or round
// video message, with a button to open it in the system's app for it
// instead (ShowImage, MediaPlayer). One window, reused for what's opened
// next.
#pragma once

#include <String.h>
#include <Window.h>

class BBitmap;
class BButton;
class BMessageRunner;
class BSlider;
class BStringView;
class MediaView;
class VideoPlayback;

class MediaWindow : public BWindow {
public:
	MediaWindow();
	~MediaWindow() override;

	void MessageReceived(BMessage* message) override;

private:
	// kMsgShowMedia: "path", "kind" (int32 pp::Media::Kind), "title"
	void _Open(const BMessage* message);
	void _Close();
	void _FitTo(BSize media);
	void _TogglePlay();
	void _SeekBy(bigtime_t delta);
	void _UpdateControls();
	void _OpenElsewhere();

	MediaView* fView;
	BView* fControls;
	BButton* fPlay;
	BSlider* fPosition;
	BStringView* fTime;
	BButton* fOpenElsewhere;
	BMessageRunner* fTicker = nullptr;

	BString fPath;
	BBitmap* fPhoto = nullptr;
	VideoPlayback* fVideo = nullptr;
	bool fScrubbing = false;
	bool fPlaced = false;   // sized and centred once; later media keep its place

	friend class MediaView;
};
