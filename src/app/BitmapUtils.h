// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class BBitmap;
class BFont;

// A B_RGBA32 copy of `source` cut to the inscribed circle (soft edge), for
// avatars and round video messages: R1/beta6 has no BView::ClipToEllipse,
// so the shape goes into the alpha channel; draw it with B_OP_ALPHA.
// Returns nullptr on failure; the caller owns the result.
BBitmap* RoundBitmap(const BBitmap* source);

// Whether `emoji` (its first character) draws with `font`, emoji fonts
// falling back included, rather than as an empty box.
bool CanDrawEmoji(const BFont& font, const char* emoji);
