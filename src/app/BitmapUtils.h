// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class BBitmap;

// A B_RGBA32 copy of `source` cut to the inscribed circle (soft edge), for
// avatars and round video messages: R1/beta6 has no BView::ClipToEllipse,
// so the shape goes into the alpha channel; draw it with B_OP_ALPHA.
// Returns nullptr on failure; the caller owns the result.
BBitmap* RoundBitmap(const BBitmap* source);
