// SPDX-License-Identifier: GPL-3.0-or-later
// Video (.webm) stickers: the frames of the first video track of a WebM
// file, with the alpha channel Telegram's stickers carry in each block's
// BlockAdditional (a second VP9 stream). Decoding is left to libvpx.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pp {

struct WebmFrame {
	int64_t time = 0;          // milliseconds from the start
	bool keyframe = false;
	std::string data;          // the coded picture
	std::string alpha;         // the coded alpha picture, "" if none
};

struct WebmVideo {
	std::string codec;         // "V_VP9", "V_VP8"
	int32_t width = 0;
	int32_t height = 0;
	bool alpha = false;        // AlphaMode 1: frames carry an alpha picture
	double duration = 0;       // milliseconds, 0 if not given
	std::vector<WebmFrame> frames;
};

// Reads a WebM file's video track. False if it isn't WebM or has no video.
// Laced blocks (audio only, in practice) are skipped.
bool ParseWebm(const std::string& file, WebmVideo* video);

} // namespace pp
