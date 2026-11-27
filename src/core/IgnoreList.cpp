// SPDX-License-Identifier: GPL-3.0-or-later
#include "IgnoreList.h"

#include <algorithm>
#include <sstream>

namespace pp {


void
IgnoreList::Ignore(int64_t chatId, int64_t senderKey, int64_t until, const std::string& name)
{
	auto key = std::make_pair(chatId, senderKey);
	Entry& entry = fEntries[key];
	entry.chatId = chatId;
	entry.senderKey = senderKey;
	entry.until = until;
	entry.name = name;
	if (fOrder.find(key) == fOrder.end())
		fOrder[key] = fNext++;
}


bool
IgnoreList::Unignore(int64_t chatId, int64_t senderKey)
{
	auto key = std::make_pair(chatId, senderKey);
	fOrder.erase(key);
	return fEntries.erase(key) > 0;
}


bool
IgnoreList::IsIgnored(int64_t chatId, int64_t senderKey, int64_t now) const
{
	auto it = fEntries.find(std::make_pair(chatId, senderKey));
	return it != fEntries.end() && (it->second.until == 0 || it->second.until > now);
}


std::vector<IgnoreList::Entry>
IgnoreList::In(int64_t chatId, int64_t now) const
{
	std::vector<Entry> result;
	for (Entry& entry : All(now)) {
		if (entry.chatId == chatId)
			result.push_back(std::move(entry));
	}
	return result;
}


std::vector<IgnoreList::Entry>
IgnoreList::All(int64_t now) const
{
	std::vector<std::pair<uint64_t, Entry>> found;
	for (const auto& [key, entry] : fEntries) {
		if (IsIgnored(key.first, key.second, now))
			found.emplace_back(fOrder.at(key), entry);
	}
	std::sort(found.begin(), found.end(),
		[](const auto& a, const auto& b) { return a.first < b.first; });
	std::vector<Entry> result;
	for (auto& f : found)
		result.push_back(std::move(f.second));
	return result;
}


bool
IgnoreList::Prune(int64_t now)
{
	bool changed = false;
	for (auto it = fEntries.begin(); it != fEntries.end();) {
		if (it->second.until != 0 && it->second.until <= now) {
			fOrder.erase(it->first);
			it = fEntries.erase(it);
			changed = true;
		} else
			++it;
	}
	return changed;
}


int64_t
IgnoreList::NextExpiry() const
{
	int64_t next = 0;
	for (const auto& [key, entry] : fEntries) {
		if (entry.until != 0 && (next == 0 || entry.until < next))
			next = entry.until;
	}
	return next;
}


std::string
IgnoreList::Encode() const
{
	std::vector<std::pair<uint64_t, const Entry*>> ordered;
	for (const auto& [key, entry] : fEntries)
		ordered.emplace_back(fOrder.at(key), &entry);
	std::sort(ordered.begin(), ordered.end(),
		[](const auto& a, const auto& b) { return a.first < b.first; });
	std::string text;
	for (const auto& [order, e] : ordered) {
		std::string name = e->name;
		std::replace(name.begin(), name.end(), '\t', ' ');
		std::replace(name.begin(), name.end(), '\n', ' ');
		text += std::to_string(e->chatId) + "\t" + std::to_string(e->senderKey) + "\t"
			+ std::to_string(e->until) + "\t" + name + "\n";
	}
	return text;
}


IgnoreList
IgnoreList::Decode(const std::string& text)
{
	IgnoreList list;
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line)) {
		std::istringstream fields(line);
		std::string chat, sender, until, name;
		if (!std::getline(fields, chat, '\t') || !std::getline(fields, sender, '\t')
			|| !std::getline(fields, until, '\t'))
			continue;
		std::getline(fields, name);
		try {
			list.Ignore(std::stoll(chat), std::stoll(sender), std::stoll(until), name);
		} catch (...) {
			// a damaged line: skip it
		}
	}
	return list;
}


}  // namespace pp
