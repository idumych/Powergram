// SPDX-License-Identifier: GPL-3.0-or-later
#include "Presence.h"

namespace pp {

// In the group: an owner who hasn't left, an admin, a member, or a
// restricted member who still is one.
static bool
IsMemberStatus(const json& status)
{
	std::string type = Type(status);
	if (type == "chatMemberStatusCreator" || type == "chatMemberStatusRestricted")
		return Bool(status, "is_member", true);
	return type != "chatMemberStatusLeft" && type != "chatMemberStatusBanned";
}


static UserStatus
ParseStatus(const json& status)
{
	UserStatus s;
	std::string t = Type(status);
	if (t == "userStatusOnline") {
		s.kind = UserStatus::kOnline;
		s.time = Int(status, "expires");
	} else if (t == "userStatusOffline") {
		s.kind = UserStatus::kOffline;
		s.time = Int(status, "was_online");
	} else if (t == "userStatusRecently")
		s.kind = UserStatus::kRecently;
	else if (t == "userStatusLastWeek")
		s.kind = UserStatus::kLastWeek;
	else if (t == "userStatusLastMonth")
		s.kind = UserStatus::kLastMonth;
	return s;
}


static std::string
ActionKind(const json& action)
{
	std::string t = Type(action);
	if (t == "chatActionTyping")
		return "typing";
	if (t == "chatActionRecordingVoiceNote" || t == "chatActionUploadingVoiceNote")
		return "voice";
	if (t == "chatActionUploadingPhoto")
		return "photo";
	if (t == "chatActionRecordingVideo" || t == "chatActionUploadingVideo"
		|| t == "chatActionRecordingVideoNote" || t == "chatActionUploadingVideoNote")
		return "video";
	if (t == "chatActionUploadingDocument")
		return "file";
	if (t == "chatActionChoosingSticker")
		return "sticker";
	if (t == "chatActionCancel")
		return std::string();
	return "typing";  // games, locations, contacts: shown as typing
}


bool
Presence::Apply(const json& object, int64_t now)
{
	std::string type = Type(object);
	if (type == "updateUser") {
		const json& user = Field(object, "user");
		fStatuses[Int(user, "id")] = ParseStatus(Field(user, "status"));
		return true;
	}
	if (type == "updateUserStatus") {
		fStatuses[Int(object, "user_id")] = ParseStatus(Field(object, "status"));
		return true;
	}
	if (type == "updateNewChat") {
		const json& chat = Field(object, "chat");
		int64_t id = Int(chat, "id");
		const json& t = Field(chat, "type");
		std::string kind = Type(t);
		if (kind == "chatTypePrivate" || kind == "chatTypeSecret")
			fPrivateUser[id] = Int(t, "user_id");
		else if (kind == "chatTypeBasicGroup")
			fBasicGroup[id] = Int(t, "basic_group_id");
		else if (kind == "chatTypeSupergroup") {
			fSupergroup[id] = Int(t, "supergroup_id");
			fChannel[id] = Bool(t, "is_channel");
		}
		return false;  // the chat list wants it too
	}
	if (type == "updateBasicGroup") {
		const json& g = Field(object, "basic_group");
		fBasicCounts[Int(g, "id")] = (int32_t)Int(g, "member_count");
		fBasicMember[Int(g, "id")] = IsMemberStatus(Field(g, "status"));
		return true;
	}
	if (type == "updateSupergroup") {
		const json& g = Field(object, "supergroup");
		fSuperCounts[Int(g, "id")] = (int32_t)Int(g, "member_count");
		fSuperMember[Int(g, "id")] = IsMemberStatus(Field(g, "status"));
		return true;
	}
	if (type == "updateChatAction") {
		int64_t chat = Int(object, "chat_id");
		const json& sender = Field(object, "sender_id");
		int64_t senderId = Type(sender) == "messageSenderChat"
			? -Int(sender, "chat_id") : Int(sender, "user_id");
		std::string kind = ActionKind(Field(object, "action"));
		if (kind.empty())
			fActions[chat].erase(senderId);
		else
			fActions[chat][senderId] = {senderId, kind, now};
		return true;
	}
	return false;
}


UserStatus
Presence::Status(int64_t userId) const
{
	auto it = fStatuses.find(userId);
	return it == fStatuses.end() ? UserStatus() : it->second;
}


int64_t
Presence::PrivateUser(int64_t chatId) const
{
	auto it = fPrivateUser.find(chatId);
	return it == fPrivateUser.end() ? 0 : it->second;
}


int64_t
Presence::BasicGroup(int64_t chatId) const
{
	auto it = fBasicGroup.find(chatId);
	return it != fBasicGroup.end() ? it->second : 0;
}


int64_t
Presence::Supergroup(int64_t chatId) const
{
	auto it = fSupergroup.find(chatId);
	return it != fSupergroup.end() ? it->second : 0;
}


bool
Presence::CanJoin(int64_t chatId) const
{
	auto basic = fBasicGroup.find(chatId);
	if (basic != fBasicGroup.end()) {
		auto member = fBasicMember.find(basic->second);
		return member != fBasicMember.end() && !member->second;
	}
	auto super = fSupergroup.find(chatId);
	if (super != fSupergroup.end()) {
		auto member = fSuperMember.find(super->second);
		return member != fSuperMember.end() && !member->second;
	}
	return false;
}


int32_t
Presence::MemberCount(int64_t chatId) const
{
	auto b = fBasicGroup.find(chatId);
	if (b != fBasicGroup.end()) {
		auto c = fBasicCounts.find(b->second);
		return c == fBasicCounts.end() ? 0 : c->second;
	}
	auto s = fSupergroup.find(chatId);
	if (s != fSupergroup.end()) {
		auto c = fSuperCounts.find(s->second);
		return c == fSuperCounts.end() ? 0 : c->second;
	}
	return 0;
}


bool
Presence::IsChannel(int64_t chatId) const
{
	auto it = fChannel.find(chatId);
	return it != fChannel.end() && it->second;
}


std::vector<Presence::Action>
Presence::Actions(int64_t chatId, int64_t now) const
{
	std::vector<Action> out;
	auto it = fActions.find(chatId);
	if (it == fActions.end())
		return out;
	for (const auto& [sender, action] : it->second)
		if (now - action.since <= 6)
			out.push_back(action);
	return out;
}


std::string
ActionText(const std::vector<Presence::Action>& actions,
	const std::map<int64_t, std::string>& names, bool privateChat)
{
	if (actions.empty())
		return std::string();
	auto verb = [](const std::string& kind) {
		if (kind == "voice")
			return std::string("recording a voice message");
		if (kind == "photo")
			return std::string("sending a photo");
		if (kind == "video")
			return std::string("sending a video");
		if (kind == "file")
			return std::string("sending a file");
		if (kind == "sticker")
			return std::string("choosing a sticker");
		return std::string("typing");
	};
	if (privateChat)
		return verb(actions[0].kind) + "…";
	auto name = [&](int64_t id) {
		auto it = names.find(id);
		std::string n = it == names.end() || it->second.empty() ? std::string("Someone") : it->second;
		return n.substr(0, n.find(' '));  // first name only
	};
	if (actions.size() == 1)
		return name(actions[0].senderId) + " is " + verb(actions[0].kind) + "…";
	if (actions.size() == 2)
		return name(actions[0].senderId) + " and " + name(actions[1].senderId)
			+ " are " + verb(actions[0].kind) + "…";
	return std::to_string(actions.size()) + " people are typing…";
}

} // namespace pp
