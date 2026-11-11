// SPDX-License-Identifier: GPL-3.0-or-later
#include "BitmapUtils.h"

#include <Bitmap.h>

#include <algorithm>
#include <cmath>

BBitmap*
RoundBitmap(const BBitmap* source)
{
	if (source == nullptr || !source->IsValid())
		return nullptr;
	BRect bounds = source->Bounds();
	BBitmap* round = new BBitmap(bounds, B_RGBA32);
	if (!round->IsValid() || round->ImportBits(source) != B_OK) {
		delete round;
		return nullptr;
	}

	int32 width = bounds.IntegerWidth() + 1;
	int32 height = bounds.IntegerHeight() + 1;
	float cx = width / 2.0f, cy = height / 2.0f;
	float radius = std::min(width, height) / 2.0f;
	// Only a real alpha channel is kept; B_RGB32's fourth byte is undefined.
	bool hasAlpha = source->ColorSpace() == B_RGBA32;
	uint8* row = static_cast<uint8*>(round->Bits());
	for (int32 y = 0; y < height; y++, row += round->BytesPerRow()) {
		for (int32 x = 0; x < width; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			// Coverage of this pixel by the circle, over a one-pixel edge.
			float coverage = std::clamp(radius - sqrtf(dx * dx + dy * dy) + 0.5f, 0.0f, 1.0f);
			uint8& alpha = row[x * 4 + 3];  // B_RGBA32 is B, G, R, A in memory
			alpha = (uint8)((hasAlpha ? alpha : 255) * coverage);
		}
	}
	return round;
}
