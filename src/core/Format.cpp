// SPDX-License-Identifier: GPL-3.0-or-later
#include "Format.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace pp {

int32_t
Utf16ToUtf8(const std::string& text, int32_t units)
{
	int32_t count = 0;
	size_t i = 0;
	while (i < text.size() && count < units) {
		unsigned char c = text[i];
		size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
		count += len == 4 ? 2 : 1;  // outside the BMP: a surrogate pair
		i += len;
	}
	return (int32_t)std::min(i, text.size());
}


std::vector<Span>
ParseEntities(const json& formattedText)
{
	std::vector<Span> spans;
	std::string text = Str(formattedText, "text");
	const json& entities = Field(formattedText, "entities");
	if (!entities.is_array())
		return spans;
	for (const json& e : entities) {
		int32_t offset = (int32_t)Int(e, "offset");
		int32_t length = (int32_t)Int(e, "length");
		const json& type = Field(e, "type");
		std::string t = Type(type);
		Span s;
		s.start = Utf16ToUtf8(text, offset);
		s.length = Utf16ToUtf8(text, offset + length) - s.start;
		if (s.length <= 0)
			continue;
		if (t == "textEntityTypeBold")
			s.style = kBold;
		else if (t == "textEntityTypeItalic")
			s.style = kItalic;
		else if (t == "textEntityTypeUnderline")
			s.style = kUnderline;
		else if (t == "textEntityTypeStrikethrough")
			s.style = kStrike;
		else if (t == "textEntityTypeCode" || t == "textEntityTypePre"
			|| t == "textEntityTypePreCode")
			s.style = kCode;
		else if (t == "textEntityTypeSpoiler")
			s.style = kSpoiler;
		else if (t == "textEntityTypeBlockQuote" || t == "textEntityTypeExpandableBlockQuote")
			s.style = kQuote;
		else if (t == "textEntityTypeUrl") {
			s.style = kLink;
			s.url = text.substr(s.start, s.length);
			if (s.url.find("://") == std::string::npos)
				s.url = "https://" + s.url;
		} else if (t == "textEntityTypeTextUrl") {
			s.style = kLink;
			s.url = Str(type, "url");
		} else if (t == "textEntityTypeEmailAddress") {
			s.style = kLink;
			s.url = "mailto:" + text.substr(s.start, s.length);
		} else
			continue;
		spans.push_back(s);
	}
	return spans;
}


std::string
EncodeSpans(const std::vector<Span>& spans)
{
	std::string out;
	for (const Span& s : spans) {
		out += std::to_string(s.start) + "," + std::to_string(s.length) + ","
			+ std::to_string(s.style) + ",";
		for (char c : s.url)
			if (c != '\n')
				out += c;
		out += "\n";
	}
	return out;
}


std::vector<Span>
DecodeSpans(const std::string& encoded)
{
	std::vector<Span> spans;
	std::istringstream in(encoded);
	std::string line;
	while (std::getline(in, line)) {
		Span s;
		size_t a = line.find(','), b = line.find(',', a + 1), c = line.find(',', b + 1);
		if (a == std::string::npos || b == std::string::npos || c == std::string::npos)
			continue;
		s.start = std::atoi(line.c_str());
		s.length = std::atoi(line.c_str() + a + 1);
		s.style = (uint32_t)std::strtoul(line.c_str() + b + 1, nullptr, 10);
		s.url = line.substr(c + 1);
		spans.push_back(s);
	}
	return spans;
}


std::vector<Span>
FindLinks(const std::string& text)
{
	std::vector<Span> spans;
	size_t i = 0;
	while (i < text.size()) {
		size_t http = text.find("http", i), www = text.find("www.", i);
		size_t at = std::min(http, www);
		if (at == std::string::npos)
			break;
		bool isHttp = at == http && (text.compare(at, 7, "http://") == 0
			|| text.compare(at, 8, "https://") == 0);
		bool isWww = at == www;
		bool boundary = at == 0 || text[at - 1] == ' ' || text[at - 1] == '\n'
			|| text[at - 1] == '(';
		if (!(isHttp || isWww) || !boundary) {
			i = at + 1;
			continue;
		}
		size_t end = at;
		while (end < text.size() && text[end] != ' ' && text[end] != '\n'
			&& text[end] != '\t')
			end++;
		// Trailing punctuation belongs to the sentence, not the address.
		while (end > at && std::string(".,;:!?)\"'").find(text[end - 1]) != std::string::npos)
			end--;
		Span s;
		s.start = (int32_t)at;
		s.length = (int32_t)(end - at);
		s.style = kLink;
		s.url = text.substr(at, end - at);
		if (isWww)
			s.url = "https://" + s.url;
		spans.push_back(s);
		i = end;
	}
	return spans;
}

} // namespace pp
