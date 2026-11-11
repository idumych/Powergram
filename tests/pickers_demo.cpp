// SPDX-License-Identifier: GPL-3.0-or-later
// Shows the entry's icon buttons and one picker with made-up content, for
// checking how they look (Haiku only):
//   make pickers-demo && build/pickers_demo emoji|stickers [sticker.webp]
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <TextControl.h>
#include <Window.h>

#include <cstdio>
#include <cstring>

#include "../src/app/Icons.h"
#include "../src/app/Messages.h"
#include "../src/app/Pickers.h"

static BButton*
IconButton(IconKind kind, const char* tip)
{
	BButton* button = new BButton(tip, nullptr, nullptr);
	BBitmap* icon = MakeIcon(kind, 20, ui_color(B_CONTROL_TEXT_COLOR));
	button->SetIcon(icon);
	delete icon;
	button->SetToolTip(tip);
	return button;
}


int
main(int argc, char** argv)
{
	bool stickers = argc > 1 && strcmp(argv[1], "stickers") == 0;
	const char* stickerPath = argc > 2 ? argv[2] : "";

	BApplication app("application/x-vnd.paper-plane-pickers-demo");
	BWindow* window = new BWindow(BRect(40, 420, 680, 470), "entry",
		B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE | B_AUTO_UPDATE_SIZE_LIMITS);
	BButton* emojiButton = IconButton(kIconEmoji, "Emoji");
	BButton* stickerButton = IconButton(kIconSticker, "Stickers");
	BLayoutBuilder::Group<>(window, B_HORIZONTAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_SMALL_SPACING)
		.Add(IconButton(kIconAttach, "Attach files"))
		.Add(new BTextControl("entry", nullptr, "Hello", nullptr), 1.0f)
		.Add(emojiButton)
		.Add(stickerButton)
		.Add(new BButton("send", "Send", nullptr));
	window->Show();

	BPoint corner(560, 410);
	if (stickers) {
		// Messages go to the app, which isn't listening: post the data
		// straight to the picker instead.
		StickerWindow* picker = new StickerWindow(BMessenger(window), BMessenger());
		picker->ShowAt(corner);
		BMessage sets(kMsgStickerSets);
		sets.AddInt64("id", 1);
		sets.AddString("title", "Smiles");
		sets.AddInt64("id", 2);
		sets.AddString("title", "Animated (emoji only)");
		picker->PostMessage(&sets);
		snooze(200000);
		BMessage list(kMsgStickers);
		list.AddInt64("set_id", 0);
		for (int i = 0; i < 10; i++) {
			list.AddInt32("file_id", i + 1);
			list.AddInt32("width", 512);
			list.AddInt32("height", 512);
			list.AddString("emoji", "\xF0\x9F\x98\x80");
			list.AddString("image", i % 3 == 2 ? "" : stickerPath);
		}
		picker->PostMessage(&list);
	} else {
		EmojiWindow* picker = new EmojiWindow(BMessenger(window));
		picker->SetRecent({"\xF0\x9F\x91\x8D", "\xE2\x9D\xA4", "\xF0\x9F\x98\x82"});
		picker->ShowAt(corner);
	}
	// Content frames, for cropping screenshots to just these windows.
	snooze(500000);
	auto print = [](BWindow* w) {
		if (w->Lock()) {
			BRect f = w->Frame();  // content only: nothing behind can show
			printf("frame %.0f %.0f %.0f %.0f\n", f.left, f.top, f.right, f.bottom);
			fflush(stdout);
			w->Unlock();
		}
	};
	print(window);
	for (int32 i = 0; BWindow* w = be_app->WindowAt(i); i++)
		if (w != window && !w->IsHidden())
			print(w);
	app.Run();
	return 0;
}
