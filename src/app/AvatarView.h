// SPDX-License-Identifier: GPL-3.0-or-later
// A round profile picture, or initials on a colour without one; for the
// chat info and Profile windows.
#pragma once

#include <String.h>
#include <View.h>

#include <string>

class BBitmap;

class AvatarView : public BView {
public:
	// `side` in multiples of the plain font's size.
	explicit AvatarView(float side = 5);
	~AvatarView() override;

	// The downloaded picture at `path`, else its tiny `preview`, else the
	// initials on a colour picked by `id`.
	void Set(int64 id, const BString& path, const std::string& preview,
		const BString& initials);
	void Draw(BRect updateRect) override;

private:
	int64 fId = 0;
	BString fPath;
	BString fInitials;
	BBitmap* fBitmap = nullptr;
};

// A downloaded image, or else a tiny inline preview; nullptr if neither.
BBitmap* LoadImage(const BString& path, const std::string& preview);
