// SPDX-License-Identifier: GPL-3.0-or-later
// Opens a file in the media viewer window, as the app does, to look at it.
//   media_viewer_demo FILE [photo|video|gif|round]
#include <Application.h>

#include <cstring>

#include "../src/app/MediaWindow.h"
#include "../src/app/Messages.h"
#include "../src/core/ChatHistory.h"

class Demo : public BApplication {
public:
	Demo(const char* path, const char* kind)
		:
		BApplication("application/x-vnd.powergram-viewer-demo"),
		fPath(path),
		fKind(kind)
	{
	}

	void ReadyToRun() override
	{
		pp::Media::Kind kind = strcmp(fKind, "video") == 0 ? pp::Media::kVideo
			: strcmp(fKind, "gif") == 0 ? pp::Media::kAnimation
			: strcmp(fKind, "round") == 0 ? pp::Media::kVideoNote : pp::Media::kPhoto;
		MediaWindow* window = new MediaWindow();
		BMessage show(kMsgShowMedia);
		show.AddString("path", fPath);
		show.AddInt32("kind", kind);
		show.AddString("title", "Video from Alice Example");
		window->PostMessage(&show);
	}

	void MessageReceived(BMessage* message) override
	{
		BApplication::MessageReceived(message);
	}

private:
	const char* fPath;
	const char* fKind;
};


int
main(int argc, char** argv)
{
	if (argc < 2)
		return 1;
	Demo demo(argv[1], argc > 2 ? argv[2] : "video");
	demo.Run();
	return 0;
}
