// SPDX-License-Identifier: GPL-3.0-or-later
// Video stickers: VP9 (or VP8) frames from the WebM file (pp::ParseWebm)
// decoded with libvpx, the alpha picture by a second decoder, into a
// bitmap at the video's size (the chat view scales it).
//
// libvpx is loaded when the first video sticker plays, whichever version
// Haiku has (its FFmpeg media add-on brings one; HaikuPorts has several,
// each with its own library version), so the package needs none in
// particular. Without it, video stickers keep their thumbnail or emoji.
#include "StickerPlayer.h"

#include <Bitmap.h>
#include <File.h>

#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>

#include <dlfcn.h>

#include <algorithm>
#include <string>

#include "../core/Webm.h"

namespace {

// The libvpx functions used, from whichever libvpx is installed.
struct Vpx {
	vpx_codec_err_t (*initVer)(vpx_codec_ctx_t*, vpx_codec_iface_t*,
		const vpx_codec_dec_cfg_t*, vpx_codec_flags_t, int) = nullptr;
	vpx_codec_err_t (*decode)(vpx_codec_ctx_t*, const uint8_t*, unsigned int, void*,
		long) = nullptr;
	vpx_image_t* (*getFrame)(vpx_codec_ctx_t*, vpx_codec_iter_t*) = nullptr;
	vpx_codec_err_t (*destroy)(vpx_codec_ctx_t*) = nullptr;
	vpx_codec_iface_t* (*vp9)() = nullptr;
	vpx_codec_iface_t* (*vp8)() = nullptr;

	vpx_codec_err_t Init(vpx_codec_ctx_t* codec, vpx_codec_iface_t* iface,
		const vpx_codec_dec_cfg_t* config) const
	{
		return initVer(codec, iface, config, 0, VPX_DECODER_ABI_VERSION);
	}
};


// libvpx, loaded once; nullptr if there's none.
const Vpx*
LoadVpx()
{
	static Vpx vpx;
	static bool tried = false;
	static bool loaded = false;
	if (tried)
		return loaded ? &vpx : nullptr;
	tried = true;
	for (const char* name : {"libvpx.so.12", "libvpx.so.11", "libvpx.so.10", "libvpx.so.9",
			"libvpx.so.8", "libvpx.so.7", "libvpx.so.6", "libvpx.so"}) {
		void* library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
		if (library == nullptr)
			continue;
		vpx.initVer = (decltype(vpx.initVer))dlsym(library, "vpx_codec_dec_init_ver");
		vpx.decode = (decltype(vpx.decode))dlsym(library, "vpx_codec_decode");
		vpx.getFrame = (decltype(vpx.getFrame))dlsym(library, "vpx_codec_get_frame");
		vpx.destroy = (decltype(vpx.destroy))dlsym(library, "vpx_codec_destroy");
		vpx.vp9 = (decltype(vpx.vp9))dlsym(library, "vpx_codec_vp9_dx");
		vpx.vp8 = (decltype(vpx.vp8))dlsym(library, "vpx_codec_vp8_dx");
		if (vpx.initVer && vpx.decode && vpx.getFrame && vpx.destroy && vpx.vp9) {
			loaded = true;
			return &vpx;
		}
		dlclose(library);
		vpx = Vpx();
	}
	return nullptr;
}


class WebmPlayer : public StickerPlayer {
public:
	~WebmPlayer() override
	{
		if (fOpen)
			fVpx->destroy(&fCodec);
		if (fAlphaOpen)
			fVpx->destroy(&fAlphaCodec);
		delete fBitmap;
	}

	bool Load(const BString& path)
	{
		fVpx = LoadVpx();
		if (fVpx == nullptr)
			return false;
		BFile file(path.String(), B_READ_ONLY);
		off_t size = 0;
		// Telegram allows 256 KB for video stickers.
		if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK || size <= 0
			|| size > 4 * 1024 * 1024)
			return false;
		std::string data(size, '\0');
		if (file.Read(&data[0], size) != size || !pp::ParseWebm(data, &fVideo))
			return false;
		vpx_codec_iface_t* codec = fVideo.codec == "V_VP9" ? fVpx->vp9()
			: fVideo.codec == "V_VP8" && fVpx->vp8 != nullptr ? fVpx->vp8() : nullptr;
		if (codec == nullptr || fVideo.width <= 0 || fVideo.height <= 0
			|| fVideo.width > 2048 || fVideo.height > 2048)
			return false;
		vpx_codec_dec_cfg_t config = {};
		config.threads = 1;
		if (fVpx->Init(&fCodec, codec, &config) != VPX_CODEC_OK)
			return false;
		fOpen = true;
		if (fVideo.alpha)
			fAlphaOpen = fVpx->Init(&fAlphaCodec, codec, &config) == VPX_CODEC_OK;
		const pp::WebmFrame& last = fVideo.frames.back();
		fInterval = fVideo.frames.size() > 1
			? std::max<int64>(last.time / (int64)(fVideo.frames.size() - 1), 1) : 33;
		fLength = std::max<int64>((int64)fVideo.duration, last.time + fInterval);
		fBitmap = new BBitmap(BRect(0, 0, fVideo.width - 1, fVideo.height - 1), B_RGBA32);
		fStart = system_time();
		return fBitmap->IsValid();
	}

	const BBitmap* Frame(int32, int32, bigtime_t now) override
	{
		int64 time = ((now - fStart) / 1000) % fLength;
		size_t target = 0;
		while (target + 1 < fVideo.frames.size() && fVideo.frames[target + 1].time <= time)
			target++;
		if (fShown >= 0 && (size_t)fShown == target)
			return fBitmap;
		// Back to the start once it loops: the first frame is a keyframe.
		if (fShown < 0 || target < (size_t)fShown)
			fNext = 0;
		vpx_image_t* image = nullptr;
		vpx_image_t* alpha = nullptr;
		for (; fNext <= target; fNext++) {
			const pp::WebmFrame& frame = fVideo.frames[fNext];
			if (fVpx->decode(&fCodec, (const uint8_t*)frame.data.data(),
					(unsigned int)frame.data.size(), nullptr, 0) != VPX_CODEC_OK)
				continue;
			vpx_codec_iter_t iter = nullptr;
			if (vpx_image_t* decoded = fVpx->getFrame(&fCodec, &iter))
				image = decoded;
			if (fAlphaOpen && !frame.alpha.empty()
				&& fVpx->decode(&fAlphaCodec, (const uint8_t*)frame.alpha.data(),
					(unsigned int)frame.alpha.size(), nullptr, 0) == VPX_CODEC_OK) {
				vpx_codec_iter_t alphaIter = nullptr;
				if (vpx_image_t* decoded = fVpx->getFrame(&fAlphaCodec, &alphaIter))
					alpha = decoded;
			}
		}
		if (image != nullptr) {
			_Convert(image, alpha);
			fShown = (int32)target;
		}
		return fShown >= 0 ? fBitmap : nullptr;
	}

	bigtime_t FrameInterval() const override { return fInterval * 1000; }

private:
	// I420 (BT.601, studio range) and the alpha picture's luma, into the
	// bitmap.
	void _Convert(const vpx_image_t* image, const vpx_image_t* alpha)
	{
		if (image->fmt != VPX_IMG_FMT_I420 && image->fmt != VPX_IMG_FMT_YV12)
			return;
		bool yv12 = image->fmt == VPX_IMG_FMT_YV12;
		int32 width = std::min<int32>(image->d_w, fVideo.width);
		int32 height = std::min<int32>(image->d_h, fVideo.height);
		uint8* bits = (uint8*)fBitmap->Bits();
		int32 rowBytes = fBitmap->BytesPerRow();
		bool useAlpha = alpha != nullptr && (int32)alpha->d_w >= width
			&& (int32)alpha->d_h >= height;
		const uint8_t* uPlane = image->planes[yv12 ? VPX_PLANE_V : VPX_PLANE_U];
		const uint8_t* vPlane = image->planes[yv12 ? VPX_PLANE_U : VPX_PLANE_V];
		int uStride = image->stride[yv12 ? VPX_PLANE_V : VPX_PLANE_U];
		int vStride = image->stride[yv12 ? VPX_PLANE_U : VPX_PLANE_V];
		for (int32 y = 0; y < height; y++) {
			const uint8_t* yRow = image->planes[VPX_PLANE_Y] + y * image->stride[VPX_PLANE_Y];
			const uint8_t* uRow = uPlane + (y >> 1) * uStride;
			const uint8_t* vRow = vPlane + (y >> 1) * vStride;
			const uint8_t* aRow = useAlpha
				? alpha->planes[VPX_PLANE_Y] + y * alpha->stride[VPX_PLANE_Y] : nullptr;
			uint8* out = bits + y * rowBytes;
			for (int32 x = 0; x < width; x++, out += 4) {
				int c = (yRow[x] - 16) * 298;
				int d = uRow[x >> 1] - 128;
				int e = vRow[x >> 1] - 128;
				out[2] = (uint8)std::clamp((c + 409 * e + 128) >> 8, 0, 255);
				out[1] = (uint8)std::clamp((c - 100 * d - 208 * e + 128) >> 8, 0, 255);
				out[0] = (uint8)std::clamp((c + 516 * d + 128) >> 8, 0, 255);
				// The alpha picture's luma is the alpha as it is (as ffmpeg
				// and Telegram's apps read it).
				out[3] = aRow != nullptr ? aRow[x] : 255;
			}
		}
	}

	const Vpx* fVpx = nullptr;
	pp::WebmVideo fVideo;
	vpx_codec_ctx_t fCodec = {};
	vpx_codec_ctx_t fAlphaCodec = {};
	bool fOpen = false;
	bool fAlphaOpen = false;
	BBitmap* fBitmap = nullptr;
	size_t fNext = 0;
	int32 fShown = -1;
	int64 fInterval = 33;   // milliseconds
	int64 fLength = 1;
	bigtime_t fStart = 0;
};

} // namespace


StickerPlayer*
OpenWebmSticker(const BString& path)
{
	WebmPlayer* player = new WebmPlayer;
	if (!player->Load(path)) {
		delete player;
		return nullptr;
	}
	return player;
}
