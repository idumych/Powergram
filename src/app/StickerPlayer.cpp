// SPDX-License-Identifier: GPL-3.0-or-later
#include "StickerPlayer.h"

#include <Bitmap.h>
#include <File.h>

#include <cmath>
#include <memory>
#include <string>

#include "../core/Lottie.h"
#include "LottieRenderer.h"

namespace {

class TgsPlayer : public StickerPlayer {
public:
	bool Load(const BString& path)
	{
		BFile file(path.String(), B_READ_ONLY);
		off_t size = 0;
		// Telegram's limit is 64 KB; anything much bigger isn't a sticker.
		if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK || size <= 0
			|| size > 1024 * 1024)
			return false;
		std::string packed(size, '\0');
		if (file.Read(&packed[0], size) != size)
			return false;
		std::string text = packed[0] == '\x1f' ? pp::GunzipTgs(packed) : packed;
		if (text.empty() || !fAnimation.Load(text))
			return false;
		fStart = system_time();
		return true;
	}

	const BBitmap* Frame(int32 width, int32 height, bigtime_t now) override
	{
		if (width <= 0 || height <= 0)
			return nullptr;
		int32 count = std::max<int32>(fAnimation.FrameCount(), 1);
		int32 index = (int32)((now - fStart) * fAnimation.FrameRate() / 1000000) % count;
		if (fRenderer == nullptr || fRenderer->Width() != width
			|| fRenderer->Height() != height) {
			fRenderer.reset(new LottieRenderer(width, height));
			fShown = -1;
		}
		if (index != fShown) {
			fBitmap = fRenderer->Draw(fAnimation.Render(fAnimation.FirstFrame() + index,
				width, height));
			fShown = index;
		}
		return fBitmap;
	}

	bigtime_t FrameInterval() const override
	{
		return (bigtime_t)(1000000 / std::max(fAnimation.FrameRate(), 1.0f));
	}

private:
	pp::LottieAnimation fAnimation;
	std::unique_ptr<LottieRenderer> fRenderer;
	const BBitmap* fBitmap = nullptr;
	int32 fShown = -1;
	bigtime_t fStart = 0;
};

} // namespace


StickerPlayer*
StickerPlayer::Open(const BString& path, int32 format)
{
	if (path.IsEmpty())
		return nullptr;
	if (format == kWebm)
		return OpenWebmSticker(path);
	if (format != kTgs)
		return nullptr;
	TgsPlayer* player = new TgsPlayer;
	if (!player->Load(path)) {
		delete player;
		return nullptr;
	}
	return player;
}
