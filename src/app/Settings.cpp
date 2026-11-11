// SPDX-License-Identifier: GPL-3.0-or-later
#include "Settings.h"

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>

#include <cstdlib>
#include <sys/stat.h>

Settings::Settings()
	:
	apiId(0)
{
}


BPath
Settings::Directory()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		path.SetTo("/tmp");
	path.Append("PaperPlane");
	create_directory(path.Path(), 0700);
	return path;
}


// A plain text file "api_key" next to the settings, so the key can be
// copied in instead of typed: "api_id=123" and "api_hash=abc" on two
// lines (or just the two values). It's read once and then deleted.
bool
Settings::_ImportApiKeyFile()
{
	BPath path = Directory();
	path.Append("api_key");
	BFile file(path.Path(), B_READ_ONLY);
	off_t size;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK || size > 4096)
		return false;

	BString text;
	char* buffer = text.LockBuffer(size + 1);
	ssize_t bytes = file.Read(buffer, size);
	text.UnlockBuffer(bytes > 0 ? bytes : 0);

	int32 id = 0;
	BString hash;
	int32 start = 0;
	while (start < text.Length()) {
		int32 end = text.FindFirst('\n', start);
		if (end < 0)
			end = text.Length();
		BString line;
		text.CopyInto(line, start, end - start);
		start = end + 1;
		line.Trim();
		int32 equals = line.FindFirst('=');
		if (equals >= 0)
			line.Remove(0, equals + 1).Trim();
		if (line.IsEmpty())
			continue;
		if (id == 0 && atoi(line.String()) > 0)
			id = atoi(line.String());
		else if (hash.IsEmpty())
			hash = line;
	}
	if (id <= 0 || hash.Length() != 32)
		return false;

	apiId = id;
	apiHash = hash;
	if (Save() == B_OK)
		BEntry(path.Path()).Remove();
	return true;
}


status_t
Settings::Load()
{
	BPath path = Directory();
	path.Append("settings");
	BFile file(path.Path(), B_READ_ONLY);
	BMessage archive;
	status_t status = file.InitCheck();
	if (status == B_OK)
		status = archive.Unflatten(&file);
	if (status == B_OK) {
		archive.FindInt32("api_id", &apiId);
		archive.FindString("api_hash", &apiHash);
		archive.FindRect("main_frame", &mainFrame);
		const char* emoji;
		for (int32 i = 0; archive.FindString("recent_emoji", i, &emoji) == B_OK; i++)
			recentEmoji.push_back(emoji);
	}
	if (_ImportApiKeyFile())
		return B_OK;
	return status;
}


status_t
Settings::Save() const
{
	BMessage archive;
	if (apiId > 0) {
		archive.AddInt32("api_id", apiId);
		archive.AddString("api_hash", apiHash);
	}
	if (mainFrame.IsValid())
		archive.AddRect("main_frame", mainFrame);
	for (const std::string& emoji : recentEmoji)
		archive.AddString("recent_emoji", emoji.c_str());

	BPath path = Directory();
	path.Append("settings");
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;
	// The API hash is a secret of its own; keep the file private.
	file.SetPermissions(S_IRUSR | S_IWUSR);
	return archive.Flatten(&file);
}
