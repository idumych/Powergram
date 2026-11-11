// SPDX-License-Identifier: GPL-3.0-or-later
// Received files on disk: giving TDLib's downloads a file type so the
// right application opens them, and copying them where the user saves them.
#pragma once

#include <SupportDefs.h>

#include <string>

// Sniffs the file's type; if that finds nothing better than a plain file,
// uses `mimeType` (what the sender's app said), when there is one.
void GiveFileType(const char* path, const std::string& mimeType);

// Copies `from` to `to` (replacing it) and gives the copy its type. With
// `wait` false it runs in a thread of its own, as documents can be big, and
// tells the user if it fails; with `wait` true it returns the result.
status_t SaveCopy(const std::string& from, const std::string& to,
	const std::string& mimeType, bool wait = false);
