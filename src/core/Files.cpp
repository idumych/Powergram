// SPDX-License-Identifier: GPL-3.0-or-later
#include "Files.h"

#include <algorithm>

namespace pp {

void
FileStore::_Record(const json& file, std::vector<int32_t>& completed)
{
	int32_t id = (int32_t)Int(file, "id");
	if (id == 0)
		return;
	const json& local = Field(file, "local");
	Entry& entry = fFiles[id];
	bool wasComplete = entry.complete;
	entry.complete = Bool(local, "is_downloading_completed")
		&& !Str(local, "path").empty();
	entry.path = entry.complete ? Str(local, "path") : std::string();

	bool active = Bool(local, "is_downloading_active");
	if (entry.complete) {
		fWanted.erase(id);
		fActive.erase(id);
		if (!wasComplete)
			completed.push_back(id);
	} else if (!active && fActive.count(id) && !Bool(local, "can_be_downloaded", true)) {
		fActive.erase(id);  // gave up: free the slot
	}
}


void
FileStore::_Scan(const json& object, std::vector<int32_t>& completed, int depth)
{
	if (depth > 12)
		return;
	if (object.is_object()) {
		if (Type(object) == "file")
			_Record(object, completed);
		for (const auto& item : object.items())
			if (item.value().is_structured())
				_Scan(item.value(), completed, depth + 1);
	} else if (object.is_array()) {
		for (const json& item : object)
			if (item.is_structured())
				_Scan(item, completed, depth + 1);
	}
}


std::vector<int32_t>
FileStore::Apply(const json& object)
{
	std::vector<int32_t> completed;
	if (Type(object) == "updateFile")
		_Record(Field(object, "file"), completed);
	else
		_Scan(object, completed, 0);
	return completed;
}


std::string
FileStore::Path(int32_t fileId) const
{
	auto it = fFiles.find(fileId);
	return it != fFiles.end() && it->second.complete ? it->second.path : std::string();
}


void
FileStore::Want(int32_t fileId, int priority)
{
	if (fileId == 0 || !Path(fileId).empty() || fActive.count(fileId))
		return;
	int& p = fWanted[fileId];
	p = std::max(p, priority);
}


std::vector<json>
FileStore::TakeDownloads()
{
	std::vector<json> out;
	while ((int)fActive.size() < fMaxActive && !fWanted.empty()) {
		auto best = fWanted.begin();
		for (auto it = fWanted.begin(); it != fWanted.end(); ++it)
			if (it->second > best->second)
				best = it;
		out.push_back(request::Download(best->first, best->second));
		fActive.insert(best->first);
		fWanted.erase(best);
	}
	return out;
}


void
FileStore::DownloadFailed(int32_t fileId)
{
	fActive.erase(fileId);
}


void
FileStore::Clear()
{
	fFiles.clear();
	fWanted.clear();
	fActive.clear();
}


namespace request {

json
Download(int32_t fileId, int priority)
{
	return {{"@type", "downloadFile"}, {"file_id", fileId},
		{"priority", std::min(32, std::max(1, priority))}, {"offset", 0},
		{"limit", 0}, {"synchronous", false},
		{"@extra", "download:" + std::to_string(fileId)}};
}

} // namespace request

} // namespace pp
