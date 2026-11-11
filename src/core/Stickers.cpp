// SPDX-License-Identifier: GPL-3.0-or-later
#include "Stickers.h"

#include <sstream>

namespace pp {

static bool
DrawableThumbnail(const json& thumbnail)
{
	std::string format = Type(Field(thumbnail, "format"));
	return format == "thumbnailFormatWebp" || format == "thumbnailFormatJpeg"
		|| format == "thumbnailFormatPng" || format == "thumbnailFormatGif";
}


StickerItem
ParseSticker(const json& sticker)
{
	StickerItem item;
	item.fileId = (int32_t)Int(Field(sticker, "sticker"), "id");
	item.width = (int32_t)Int(sticker, "width");
	item.height = (int32_t)Int(sticker, "height");
	item.emoji = Str(sticker, "emoji");
	const json& thumbnail = Field(sticker, "thumbnail");
	if (DrawableThumbnail(thumbnail))
		item.imageFileId = (int32_t)Int(Field(thumbnail, "file"), "id");
	else if (Type(Field(sticker, "format")) == "stickerFormatWebp")
		item.imageFileId = item.fileId;
	return item;
}


std::vector<StickerItem>
ParseStickers(const json& object)
{
	std::vector<StickerItem> out;
	const json& list = Field(object, "stickers");
	if (!list.is_array())
		return out;
	for (const json& sticker : list) {
		StickerItem item = ParseSticker(sticker);
		if (item.fileId != 0)
			out.push_back(item);
	}
	return out;
}


std::vector<StickerSetInfo>
ParseStickerSets(const json& object)
{
	std::vector<StickerSetInfo> out;
	const json& sets = Field(object, "sets");
	if (!sets.is_array())
		return out;
	for (const json& set : sets) {
		StickerSetInfo info;
		info.id = Int(set, "id");
		info.title = Str(set, "title");
		const json& covers = Field(set, "covers");
		if (covers.is_array())
			for (const json& cover : covers)
				info.covers.push_back(ParseSticker(cover));
		if (info.id != 0)
			out.push_back(info);
	}
	return out;
}


namespace request {

json
InstalledStickerSets()
{
	return {{"@type", "getInstalledStickerSets"},
		{"sticker_type", {{"@type", "stickerTypeRegular"}}}, {"@extra", "sticker_sets"}};
}


json
StickerSet(int64_t setId)
{
	return {{"@type", "getStickerSet"}, {"set_id", setId},
		{"@extra", "sticker_set:" + std::to_string(setId)}};
}


json
RecentStickers()
{
	return {{"@type", "getRecentStickers"}, {"is_attached", false},
		{"@extra", "sticker_set:recent"}};
}


json
SendSticker(int64_t chatId, const StickerItem& sticker, int64_t replyTo)
{
	json request = {{"@type", "sendMessage"}, {"chat_id", chatId},
		{"input_message_content", {{"@type", "inputMessageSticker"},
			{"sticker", {{"@type", "inputSticker"},
				{"sticker", {{"@type", "inputFileId"}, {"id", sticker.fileId}}},
				{"width", sticker.width}, {"height", sticker.height}}},
			{"emoji", sticker.emoji}}}};
	if (replyTo != 0)
		request["reply_to"] = {{"@type", "inputMessageReplyToMessage"},
			{"message_id", replyTo}};
	return request;
}

} // namespace request


// Each category is one string of emoji separated by spaces (some emoji
// are several code points, so they can't be split by character).
static std::vector<std::string>
Split(const char* list)
{
	std::vector<std::string> out;
	std::istringstream in(list);
	std::string item;
	while (in >> item)
		out.push_back(item);
	return out;
}


const std::vector<EmojiCategory>&
EmojiCatalog()
{
	static const std::vector<EmojiCategory> catalog = {
		{"Smileys", Split(
			"😀 😃 😄 😁 😆 😅 🤣 😂 🙂 🙃 😉 😊 😇 🥰 😍 🤩 😘 😗 😚 😙 😋 😛 😜 🤪 "
			"😝 🤑 🤗 🤭 🤫 🤔 🤐 🤨 😐 😑 😶 😏 😒 🙄 😬 😌 😔 😪 🤤 😴 😷 🤒 🤕 🤢 "
			"🤮 🥵 🥶 🥴 😵 🤯 🤠 🥳 😎 🤓 🧐 😕 😟 🙁 😮 😯 😲 😳 🥺 😦 😧 😨 😰 😥 "
			"😢 😭 😱 😖 😣 😞 😓 😩 😫 🥱 😤 😡 😠 🤬 😈 👿 💀 💩 🤡 👻 👽 🤖")},
		{"People", Split(
			"👋 🤚 🖐 ✋ 🖖 👌 🤏 ✌ 🤞 🤟 🤘 🤙 👈 👉 👆 👇 ☝ 👍 👎 ✊ 👊 🤛 🤜 👏 "
			"🙌 👐 🤲 🤝 🙏 ✍ 💪 🦾 👀 👁 👅 👄 🧠 👶 🧒 👦 👧 🧑 👨 👩 🧓 👴 👵 🙋 "
			"🙅 🙆 🤷 🤦 🙇 💁 🧏 🙎 🙍 💃 🕺 🚶 🏃")},
		{"Hearts", Split(
			"❤ 🧡 💛 💚 💙 💜 🖤 🤍 🤎 💔 ❣ 💕 💞 💓 💗 💖 💘 💝 💟 💯 💢 💥 💫 💦 "
			"💨 🔥 ✨ ⭐ 🌟 💤")},
		{"Animals and nature", Split(
			"🐶 🐱 🐭 🐹 🐰 🦊 🐻 🐼 🐨 🐯 🦁 🐮 🐷 🐸 🐵 🙈 🙉 🙊 🐔 🐧 🐦 🐤 🦆 🦅 "
			"🦉 🐺 🐗 🐴 🦄 🐝 🐛 🦋 🐌 🐞 🐢 🐍 🦎 🐙 🦑 🦀 🐠 🐟 🐬 🐳 🐋 🦈 🐊 🐘 "
			"🌵 🎄 🌲 🌳 🌴 🌱 🌿 🍀 🍁 🍂 🍃 🌷 🌹 🥀 🌺 🌸 🌼 🌻 🌞 🌝 🌚 🌙 ☀ ⛅ "
			"☁ 🌧 ⛈ 🌩 ❄ ☃ ⛄ 🌈 ☔ 🌊")},
		{"Food and drink", Split(
			"🍏 🍎 🍐 🍊 🍋 🍌 🍉 🍇 🍓 🍈 🍒 🍑 🥭 🍍 🥥 🥝 🍅 🍆 🥑 🥦 🥒 🌶 🌽 🥕 "
			"🥔 🥐 🍞 🥖 🧀 🥚 🍳 🥞 🥓 🍗 🍖 🌭 🍔 🍟 🍕 🥪 🌮 🌯 🥗 🍝 🍜 🍲 🍣 🍱 "
			"🍤 🍙 🍚 🍰 🎂 🧁 🍮 🍭 🍬 🍫 🍿 🍩 🍪 ☕ 🍵 🥤 🍺 🍻 🥂 🍷 🥃 🍸 🍹 🍾")},
		{"Activities and travel", Split(
			"⚽ 🏀 🏈 ⚾ 🎾 🏐 🏉 🎱 🏓 🏸 🥅 🏒 🏑 ⛳ 🏹 🎣 🥊 🥋 ⛸ 🎿 🏂 🏋 🚴 🏆 "
			"🥇 🥈 🥉 🏅 🎖 🎗 🎫 🎟 🎪 🎭 🎨 🎬 🎤 🎧 🎼 🎹 🥁 🎷 🎺 🎸 🎻 🎲 🎯 🎳 "
			"🎮 🎰 🧩 🚗 🚕 🚌 🚎 🏎 🚓 🚑 🚒 🚚 🚲 🛴 🛵 🏍 ✈ 🚀 🛸 🚁 ⛵ 🚢 🏠 🏡 "
			"🏢 🏰 🗼 🗽 ⛪ 🌋 🗻 🏕 🏖 🏝")},
		{"Objects and symbols", Split(
			"⌚ 📱 💻 ⌨ 🖥 🖨 🖱 💾 💿 📷 📹 🎥 📞 ☎ 📺 📻 ⏰ ⌛ 🔋 🔌 💡 🔦 🕯 💸 "
			"💵 💰 💳 💎 🔧 🔨 🛠 🔩 ⚙ 🧲 💊 💉 🧬 🔬 🔭 📡 🎁 🎈 🎉 🎊 ✉ 📩 📦 📝 "
			"📁 📅 📌 📎 ✂ 🔒 🔓 🔑 🗝 ✅ ❌ ❓ ❗ ⚠ 🚫 ⛔ ♻ ✔ ➕ ➖ ➗ 💲 ©️ ®️ ™️ "
			"🆗 🆕 🆒 🆓 🔝 🔜 🏁 🚩 🏳")},
	};
	return catalog;
}

} // namespace pp
