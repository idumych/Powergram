// SPDX-License-Identifier: GPL-3.0-or-later
// Sticker sets for the sticker picker: the installed sets, their stickers,
// the recently used ones, and the requests to fetch and send them.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Json.h"

namespace pp {

struct StickerItem {
	int32_t fileId = 0;      // the sticker itself, for sending
	int32_t imageFileId = 0; // something Haiku can draw (WebP sticker or an
	                         // image thumbnail); 0: show the emoji
	int32_t width = 0;
	int32_t height = 0;
	std::string emoji;
	// Animated stickers (Media::kTgs, kWebm; 0 for still ones): without an
	// image thumbnail, the picker shows the first frame of the file itself.
	int32_t animationFormat = 0;
};

struct StickerSetInfo {
	int64_t id = 0;
	std::string title;
	std::vector<StickerItem> covers;  // a few stickers, for the set's icon
};

// One sticker; prefers the small thumbnail for showing it in the picker.
StickerItem ParseSticker(const json& sticker);
// "stickers" (recent) or "stickerSet" (one set): its stickers.
std::vector<StickerItem> ParseStickers(const json& object);
// "stickerSets": the installed sets, in the user's order.
std::vector<StickerSetInfo> ParseStickerSets(const json& object);

namespace request {

json InstalledStickerSets();
json StickerSet(int64_t setId);
json RecentStickers();
json SendSticker(int64_t chatId, const StickerItem& sticker, int64_t replyTo = 0);

} // namespace request

// The emoji offered by the emoji picker, by category (no network needed).
struct EmojiCategory {
	const char* name;
	std::vector<std::string> emoji;
};
const std::vector<EmojiCategory>& EmojiCatalog();

} // namespace pp
