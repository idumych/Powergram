// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileSaving.h"

#include <Alert.h>
#include <File.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <String.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <thread>
#include <vector>

#include "Messages.h"

void
GiveFileType(const char* path, const std::string& mimeType)
{
	update_mime_info(path, false, true, B_UPDATE_MIME_INFO_NO_FORCE);
	BNode node(path);
	BNodeInfo info(&node);
	char type[B_MIME_TYPE_LENGTH] = "";
	if (!mimeType.empty() && (info.GetType(type) != B_OK
			|| strcmp(type, B_FILE_MIME_TYPE) == 0))
		info.SetType(mimeType.c_str());
}


// Written beside the destination, then renamed over it: a full disk or
// quitting mid-copy leaves a file the user chose to replace as it was.
static status_t
Copy(const std::string& from, const std::string& to, const std::string& mimeType)
{
	const std::string part = to + ".part";
	BFile in(from.c_str(), B_READ_ONLY);
	BFile out(part.c_str(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = in.InitCheck() != B_OK ? in.InitCheck() : out.InitCheck();
	std::vector<char> buffer(256 * 1024);
	while (status == B_OK) {
		ssize_t got = in.Read(buffer.data(), buffer.size());
		if (got < 0)
			status = (status_t)got;
		if (got <= 0)
			break;
		ssize_t put = out.Write(buffer.data(), got);
		if (put != got)
			status = put < 0 ? (status_t)put : B_IO_ERROR;
	}
	if (status == B_OK)
		status = out.Sync();
	out.Unset();
	if (status == B_OK && rename(part.c_str(), to.c_str()) != 0)
		status = errno;
	if (status != B_OK) {
		unlink(part.c_str());
		return status;
	}
	GiveFileType(to.c_str(), mimeType);
	return B_OK;
}


status_t
SaveCopy(const std::string& from, const std::string& to, const std::string& mimeType,
	bool wait)
{
	if (wait)
		return Copy(from, to, mimeType);
	std::thread([from, to, mimeType]() {
		status_t status = Copy(from, to, mimeType);
		if (status != B_OK) {
			BString text("Could not save the file: ");
			text << strerror(status);
			(new BAlert(APP_NAME, text, "OK", nullptr, nullptr, B_WIDTH_AS_USUAL,
				B_WARNING_ALERT))->Go(nullptr);
		}
	}).detach();
	return B_OK;
}
