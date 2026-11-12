// SPDX-License-Identifier: GPL-3.0-or-later
// QR codes for logging in from a phone: Telegram's tg://login?token=…
// link as a grid of dark and light modules (Nayuki's QR Code generator).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pp {

struct QrMatrix {
	int size = 0;                  // modules per side; 0 if it couldn't be made
	std::vector<uint8_t> modules;  // row by row, 1 for dark

	bool Dark(int x, int y) const
	{
		return x >= 0 && y >= 0 && x < size && y < size && modules[y * size + x] != 0;
	}
};

// The code for `text`, with medium error correction (a phone still reads
// it off a dim or scaled screen). Draw it with a light margin of four
// modules around it.
QrMatrix EncodeQr(const std::string& text);

} // namespace pp
