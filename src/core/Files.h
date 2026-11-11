// SPDX-License-Identifier: GPL-3.0-or-later
// TDLib files: where each one is on disk once downloaded, and a download
// queue that fetches only what's wanted, a few at a time, most wanted first.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Json.h"

namespace pp {

class FileStore {
public:
	explicit FileStore(int maxActive = 3) : fMaxActive(maxActive) {}

	// Takes updateFile, and file objects found anywhere in `object` (a
	// message already says where its photo is if TDLib has it). Returns
	// the ids of files that just finished downloading.
	std::vector<int32_t> Apply(const json& object);

	// The local path of a downloaded file, or "".
	std::string Path(int32_t fileId) const;

	// Asks for a file; higher priority first (1-32, as TDLib's).
	void Want(int32_t fileId, int priority);
	// downloadFile requests to send now; at most maxActive at a time.
	std::vector<json> TakeDownloads();

	// A downloadFile request failed ("download:<id>" error): free its slot.
	void DownloadFailed(int32_t fileId);

	// Drops requests that haven't started (another chat was opened).
	void ForgetWanted() { fWanted.clear(); }

	void Clear();
	int Active() const { return (int)fActive.size(); }

private:
	void _Record(const json& file, std::vector<int32_t>& completed);
	void _Scan(const json& object, std::vector<int32_t>& completed, int depth);

	struct Entry {
		std::string path;
		bool complete = false;
	};
	std::map<int32_t, Entry> fFiles;
	std::map<int32_t, int> fWanted;   // waiting: id → priority
	std::set<int32_t> fActive;        // requested, not finished
	int fMaxActive;
};

namespace request {

json Download(int32_t fileId, int priority);

} // namespace request

} // namespace pp
