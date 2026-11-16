// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <String.h>

#include <string>

class BBitmap;
class BFont;

// A B_RGBA32 copy of `source` cut to the inscribed circle (soft edge), for
// avatars and round video messages: R1/beta6 has no BView::ClipToEllipse,
// so the shape goes into the alpha channel; draw it with B_OP_ALPHA.
// Returns nullptr on failure; the caller owns the result.
BBitmap* RoundBitmap(const BBitmap* source);

// An avatar: the picture at `path`, else its JPEG `preview`, decoded and
// cut round. nullptr if neither decodes; the caller owns the result.
BBitmap* RoundAvatar(const char* path, const std::string& preview);

// "340 KB", "1.2 MB": file sizes as Tracker shows them.
BString SizeText(int64 bytes);

// Whether `emoji` (its first character) draws with `font`, emoji fonts
// falling back included, rather than as an empty box.
bool CanDrawEmoji(const BFont& font, const char* emoji);
