// SPDX-License-Identifier: GPL-3.0-or-later
#include "QrCode.h"

#include "../third_party/qrcodegen/qrcodegen.hpp"

namespace pp {

QrMatrix
EncodeQr(const std::string& text)
{
	QrMatrix matrix;
	try {
		qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(text.c_str(),
			qrcodegen::QrCode::Ecc::MEDIUM);
		matrix.size = code.getSize();
		matrix.modules.resize(matrix.size * matrix.size);
		for (int y = 0; y < matrix.size; y++)
			for (int x = 0; x < matrix.size; x++)
				matrix.modules[y * matrix.size + x] = code.getModule(x, y) ? 1 : 0;
	} catch (const std::exception&) {
		// Too long for any QR code.
		matrix = QrMatrix();
	}
	return matrix;
}

} // namespace pp
