// SPDX-License-Identifier: GPL-3.0-or-later
// Text formatting from TDLib's formattedText: bold, italic, code, links…
// TDLib counts entity offsets in UTF-16 code units; spans here are UTF-8
// byte ranges of the same text.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Json.h"

namespace pp {

enum SpanStyle : uint32_t {
	kBold = 1 << 0,
	kItalic = 1 << 1,
	kUnderline = 1 << 2,
	kStrike = 1 << 3,
	kCode = 1 << 4,     // monospace (code and pre)
	kLink = 1 << 5,     // clickable; `url` says where to
	kSpoiler = 1 << 6,
	kQuote = 1 << 7
};

struct Span {
	int32_t start = 0;      // UTF-8 byte offset
	int32_t length = 0;     // UTF-8 bytes
	uint32_t style = 0;
	std::string url;        // links: the address to open
};

// Spans for a formattedText's entities (unknown types are skipped).
std::vector<Span> ParseEntities(const json& formattedText);

// Byte offset in `text` of the UTF-16 offset `units`.
int32_t Utf16ToUtf8(const std::string& text, int32_t units);

// One line per span, "start,length,style,url", for passing in a BMessage.
std::string EncodeSpans(const std::vector<Span>& spans);
std::vector<Span> DecodeSpans(const std::string& encoded);

// Links found in plain text (http/https/www.), for text without entities.
std::vector<Span> FindLinks(const std::string& text);

} // namespace pp
