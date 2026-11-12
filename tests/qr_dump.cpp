// SPDX-License-Identifier: GPL-3.0-or-later
// Writes the login QR code for a text as a PBM picture (4 pixels a module,
// a four-module margin), for a QR reader to check:
//   qr_dump TEXT > code.pbm && zbarimg --raw code.pbm
#include <cstdio>

#include "../src/core/QrCode.h"

int
main(int argc, char** argv)
{
	if (argc < 2)
		return 1;
	pp::QrMatrix qr = pp::EncodeQr(argv[1]);
	if (qr.size == 0)
		return 1;
	const int scale = 4, margin = 4;
	int side = (qr.size + 2 * margin) * scale;
	std::printf("P1\n%d %d\n", side, side);
	for (int py = 0; py < side; py++) {
		for (int px = 0; px < side; px++)
			std::printf("%d ", qr.Dark(px / scale - margin, py / scale - margin) ? 1 : 0);
		std::printf("\n");
	}
	return 0;
}
