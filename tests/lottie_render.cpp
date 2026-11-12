// SPDX-License-Identifier: GPL-3.0-or-later
// Renders frames of an animated sticker (Lottie .json or .tgs, or .webm)
// side by side into a PNG, to look at what the sticker players draw:
//   lottie_render <file> <out.png> [size] [frames]
#include <Application.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <File.h>
#include <OS.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <View.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "../src/app/LottieRenderer.h"
#include "../src/app/StickerPlayer.h"

int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s <file.json|file.tgs> <out.png> [size] [frames]\n", argv[0]);
		return 1;
	}
	BApplication app("application/x-vnd.Powergram-lottie-render");
	BString path(argv[1]);
	if (path.EndsWith(".webm")) {
		// Frames a tenth of a second apart, through the player.
		int32 size = argc > 3 ? atoi(argv[3]) : 160;
		int32 frames = argc > 4 ? atoi(argv[4]) : 6;
		StickerPlayer* player = StickerPlayer::Open(path, StickerPlayer::kWebm);
		if (player == nullptr) {
			fprintf(stderr, "can't play %s\n", argv[1]);
			return 1;
		}
		BBitmap sheet(BRect(0, 0, size * frames - 1, size - 1), B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
		BView* view = new BView(sheet.Bounds(), "sheet", B_FOLLOW_NONE, 0);
		sheet.AddChild(view);
		sheet.Lock();
		for (int32 y = 0; y < size; y += 8) {
			for (int32 x = 0; x < size * frames; x += 8) {
				view->SetHighColor(((x + y) / 8) % 2 ? make_color(230, 230, 230)
					: make_color(250, 250, 250));
				view->FillRect(BRect(x, y, x + 7, y + 7));
			}
		}
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		bigtime_t start = system_time();
		for (int32 i = 0; i < frames; i++) {
			const BBitmap* bitmap = player->Frame(size, size, start + i * 100000);
			if (bitmap != nullptr)
				view->DrawBitmap(bitmap, bitmap->Bounds(), BRect(i * size, 0, (i + 1) * size - 1,
					size - 1), B_FILTER_BITMAP_BILINEAR);
		}
		printf("%.2f ms for %d frames\n", (system_time() - start) / 1000.0, (int)frames);
		view->Sync();
		sheet.Unlock();
		delete player;
		BBitmapStream stream(&sheet);
		BFile file(argv[2], B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		status_t status = BTranslatorRoster::Default()->Translate(&stream, nullptr, nullptr,
			&file, B_PNG_FORMAT);
		BBitmap* back;
		stream.DetachBitmap(&back);
		return status == B_OK ? 0 : 1;
	}
	std::ifstream in(argv[1], std::ios::binary);
	std::stringstream data;
	data << in.rdbuf();
	std::string text = data.str();
	if (!text.empty() && text[0] == '\x1f')
		text = pp::GunzipTgs(text);
	pp::LottieAnimation animation;
	if (!animation.Load(text)) {
		fprintf(stderr, "not a Lottie animation\n");
		return 1;
	}
	int32 size = argc > 3 ? atoi(argv[3]) : 160;
	int32 frames = argc > 4 ? atoi(argv[4]) : 6;
	printf("%d x %d, %.0f fps, frames %.0f..%.0f\n", (int)animation.Width(),
		(int)animation.Height(), animation.FrameRate(), animation.FirstFrame(),
		animation.LastFrame());

	// The frames on a light grey checkerboard, so transparency shows.
	BBitmap sheet(BRect(0, 0, size * frames - 1, size - 1), B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
	BView* view = new BView(sheet.Bounds(), "sheet", B_FOLLOW_NONE, 0);
	sheet.AddChild(view);
	sheet.Lock();
	for (int32 y = 0; y < size; y += 8) {
		for (int32 x = 0; x < size * frames; x += 8) {
			view->SetHighColor(((x + y) / 8) % 2 ? make_color(230, 230, 230)
				: make_color(250, 250, 250));
			view->FillRect(BRect(x, y, x + 7, y + 7));
		}
	}
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	LottieRenderer renderer(size, size);
	bigtime_t total = 0;
	for (int32 i = 0; i < frames; i++) {
		float frame = animation.FirstFrame()
			+ (animation.LastFrame() - animation.FirstFrame() - 1) * i / std::max(frames - 1, (int32)1);
		bigtime_t start = system_time();
		pp::LottieList shapes = animation.Render(frame, size, size);
		const BBitmap* bitmap = renderer.Draw(shapes);
		total += system_time() - start;
		view->DrawBitmap(bitmap, BPoint(i * size, 0));
	}
	view->Sync();
	sheet.Unlock();
	printf("%.2f ms a frame\n", total / 1000.0 / frames);

	BBitmapStream stream(&sheet);
	BFile file(argv[2], B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = BTranslatorRoster::Default()->Translate(&stream, nullptr, nullptr, &file,
		B_PNG_FORMAT);
	BBitmap* back;
	stream.DetachBitmap(&back);
	if (status != B_OK) {
		fprintf(stderr, "could not write the PNG\n");
		return 1;
	}
	return 0;
}
