// SPDX-License-Identifier: GPL-3.0-or-later
// Host tests for the portable core: `make test`.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../src/core/AuthFlow.h"
#include "../src/core/ChatHistory.h"
#include "../src/core/ChatInfo.h"
#include "../src/core/ChatList.h"
#include "../src/core/Files.h"
#include "../src/core/Format.h"
#include "../src/core/Links.h"
#include "../src/core/Pinned.h"
#include "../src/core/VoiceNote.h"
#include "../src/core/Mentions.h"
#include "../src/core/Profile.h"
#include "../src/core/Presence.h"
#include "../src/core/SendQueue.h"
#include "../src/core/Stickers.h"

using namespace pp;

static int sFailures = 0;

#define CHECK(cond) do { if (!(cond)) { \
	std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
	sFailures++; } } while (0)

static json
AuthUpdate(const json& state)
{
	return {{"@type", "updateAuthorizationState"}, {"authorization_state", state}};
}


static void
TestAuthFlow()
{
	TdParams params;
	params.databaseDir = "/tmp/db";
	params.testDc = true;
	AuthFlow flow(params);
	std::vector<json> out;

	// Unrelated objects are ignored.
	CHECK(!flow.Handle({{"@type", "updateOption"}}, out));

	// No API key yet: ask for it instead of sending parameters.
	CHECK(flow.Handle(AuthUpdate({{"@type", "authorizationStateWaitTdlibParameters"}}), out));
	CHECK(flow.State().step == AuthStep::NeedApiKey);
	CHECK(out.empty());

	json req = flow.SetApiKey(12345, "abcdef");
	CHECK(req["@type"] == "setTdlibParameters");
	CHECK(req["api_id"] == 12345);
	CHECK(req["use_test_dc"] == true);
	CHECK(req["database_directory"] == "/tmp/db");
	CHECK(AuthFlow::IsAuthReply(req));

	// With a key, parameters go out at once.
	AuthFlow keyed(flow.Params());
	keyed.Handle(AuthUpdate({{"@type", "authorizationStateWaitTdlibParameters"}}), out);
	CHECK(out.size() == 1 && out[0]["@type"] == "setTdlibParameters");
	CHECK(keyed.State().step == AuthStep::Starting);

	flow.Handle(AuthUpdate({{"@type", "authorizationStateWaitPhoneNumber"}}), out);
	CHECK(flow.State().step == AuthStep::Phone);
	CHECK(flow.PhoneRequest(" +99 966 2 1234 ")["phone_number"] == "+9996621234");

	flow.Handle(AuthUpdate({{"@type", "authorizationStateWaitCode"},
		{"code_info", {{"type", {{"@type", "authenticationCodeTypeSms"}, {"length", 5}}}}}}), out);
	CHECK(flow.State().step == AuthStep::Code);
	CHECK(flow.State().codeInfo == "by SMS");
	CHECK(flow.State().codeLength == 5);
	CHECK(flow.CodeRequest("22 222")["code"] == "22222");

	flow.Handle(AuthUpdate({{"@type", "authorizationStateWaitPassword"},
		{"password_hint", "cat"}}), out);
	CHECK(flow.State().step == AuthStep::Password);
	CHECK(flow.State().passwordHint == "cat");

	flow.Handle(AuthUpdate({{"@type", "authorizationStateWaitRegistration"}}), out);
	CHECK(flow.State().step == AuthStep::Registration);
	CHECK(flow.RegisterRequest("A", "B")["@type"] == "registerUser");

	flow.Handle(AuthUpdate({{"@type", "authorizationStateWaitOtherDeviceConfirmation"},
		{"link", "tg://login?token=x"}}), out);
	CHECK(flow.State().step == AuthStep::OtherDevice);
	CHECK(flow.State().link == "tg://login?token=x");

	flow.Handle(AuthUpdate({{"@type", "authorizationStateReady"}}), out);
	CHECK(flow.State().step == AuthStep::Ready);

	flow.Handle(AuthUpdate({{"@type", "authorizationStateSomethingNew"}}), out);
	CHECK(flow.State().step == AuthStep::Unsupported);
	CHECK(flow.State().tdType == "authorizationStateSomethingNew");

	CHECK(AuthFlow::ErrorText({{"@type", "error"}, {"code", 400},
		{"message", "PHONE_CODE_INVALID"}}).find("wrong") != std::string::npos);
	CHECK(AuthFlow::ErrorText({{"@type", "error"}, {"message", "WEIRD"}})
		== "Telegram reported an error: WEIRD");
	CHECK(AuthFlow::IsApiKeyError({{"@type", "error"}, {"message", "API_ID_INVALID"}}));
	CHECK(!AuthFlow::IsApiKeyError({{"@type", "error"}, {"message", "PHONE_CODE_INVALID"}}));
	CHECK(!AuthFlow::IsAuthReply({{"@type", "ok"}, {"@extra", "loadChats"}}));
	CHECK(NormalizePhone("+1 (555) 12-34") == "+15551234");
	CHECK(NormalizePhone("00+44") == "0044");
}


static json
NewChat(int64_t id, const char* title, const char* order, const json& lastMessage = nullptr)
{
	json chat = {{"id", id}, {"title", title}, {"unread_count", 0},
		{"positions", json::array()}, {"last_message", lastMessage}};
	if (order)
		chat["positions"].push_back({{"@type", "chatPosition"},
			{"list", {{"@type", "chatListMain"}}}, {"order", order}, {"is_pinned", false}});
	return {{"@type", "updateNewChat"}, {"chat", chat}};
}


static void
TestChatList()
{
	ChatList list;
	CHECK(list.Apply(NewChat(1, "Kevin", "100")));
	CHECK(list.Apply(NewChat(2, "Harold", "300", {{"id", 7}, {"date", 1700000000},
		{"is_outgoing", true}, {"content", {{"@type", "messageText"},
		{"text", {{"text", "this is the way!\nsecond line"}}}}}})));
	CHECK(!list.Apply(NewChat(3, "Archived", nullptr)));  // not in the main list
	CHECK(list.Apply(NewChat(4, "Quentin", "9223372036854775000")));  // > 2^53

	auto sorted = list.Sorted();
	CHECK(sorted.size() == 3);
	CHECK(sorted[0].id == 4 && sorted[1].id == 2 && sorted[2].id == 1);
	CHECK(sorted[1].lastText == "this is the way!");
	CHECK(sorted[1].lastSender == "You");
	CHECK(sorted[1].lastDate == 1700000000);

	// A new message moves Kevin to the top.
	CHECK(list.Apply({{"@type", "updateChatLastMessage"}, {"chat_id", 1},
		{"last_message", {{"id", 9}, {"date", 1700000500}, {"is_outgoing", false},
			{"content", {{"@type", "messagePhoto"}, {"caption", {{"text", "Look"}}}}}}},
		{"positions", json::array({{{"@type", "chatPosition"}, {"list", {{"@type", "chatListMain"}}},
			{"order", "9223372036854775800"}, {"is_pinned", false}}})}}));
	sorted = list.Sorted();
	CHECK(sorted[0].id == 1);
	CHECK(sorted[0].lastText == "Photo, Look");

	// Order 0 removes a chat; other lists are ignored.
	// Other lists are tracked separately; the main list is unchanged.
	CHECK(list.Apply({{"@type", "updateChatPosition"}, {"chat_id", 2},
		{"position", {{"list", {{"@type", "chatListArchive"}}}, {"order", "0"}}}}));
	CHECK(list.Sorted().size() == 3);
	CHECK(list.Apply({{"@type", "updateChatPosition"}, {"chat_id", 2},
		{"position", {{"list", {{"@type", "chatListMain"}}}, {"order", "0"}, {"is_pinned", false}}}}));
	CHECK(list.Sorted().size() == 2);

	CHECK(list.Apply({{"@type", "updateChatReadInbox"}, {"chat_id", 1}, {"unread_count", 4}}));
	CHECK(list.Find(1)->unread == 4);
	CHECK(list.Apply({{"@type", "updateChatTitle"}, {"chat_id", 4}, {"title", "Q"}}));
	CHECK(list.Find(4)->title == "Q");
	CHECK(!list.Apply({{"@type", "updateChatTitle"}, {"chat_id", 99}, {"title", "?"}}));

	CHECK(ChatList::LoadRequest(50)["chat_list"]["@type"] == "chatListMain");

	CHECK(MessagePreview({{"@type", "messageSticker"}, {"sticker", {{"emoji", "😀"}}}}) == "😀 Sticker");
	CHECK(MessagePreview({{"@type", "messageUnheardOf"}}) == "Message");
	CHECK(Initials("Tyrone Ford") == "TF");
	CHECK(Initials("harold") == "H");
	CHECK(Initials("  Mr. Pengu the third") == "MP");
	CHECK(Initials("Бумажный самолётик") == "Бс");
	CHECK(Initials("") == "");
	CHECK(ToInt64(json("123")) == 123 && ToInt64(json(5)) == 5 && ToInt64(json(nullptr)) == 0);
}


// TDLib leaves out null fields; the real chat list crashed on an
// updateChatLastMessage without "last_message".
static void
TestMissingFields()
{
	ChatList list;
	CHECK(list.Apply(NewChat(5, "Mike", "500", {{"id", 1}, {"date", 1},
		{"is_outgoing", false}, {"content", {{"@type", "messageText"},
		{"text", {{"text", "hi"}}}}}})));
	CHECK(list.Apply({{"@type", "updateChatLastMessage"}, {"chat_id", 5},
		{"positions", json::array()}}));
	CHECK(list.Find(5)->lastText.empty());
	CHECK(list.Find(5)->lastDate == 0);

	// A chat with neither positions nor last message.
	CHECK(!list.Apply({{"@type", "updateNewChat"}, {"chat", {{"id", 6}, {"title", "Bare"}}}}));
	CHECK(list.Find(6) != nullptr);
	// No chat at all, or no id.
	CHECK(!list.Apply({{"@type", "updateNewChat"}}));
	CHECK(!list.Apply({{"@type", "updateNewChat"}, {"chat", {{"title", "x"}}}}));

	// Every update type, with nothing in it, with wrong types, and for a
	// known chat: must not crash or throw.
	const char* types[] = {"updateNewChat", "updateChatTitle", "updateChatPosition",
		"updateChatLastMessage", "updateChatReadInbox", "updateChatReadOutbox",
		"updateChatUnreadMentionCount", "updateAuthorizationState"};
	for (const char* type : types) {
		json bodies[] = {
			{{"@type", type}},
			{{"@type", type}, {"chat_id", 5}},
			{{"@type", type}, {"chat_id", 5}, {"position", nullptr},
				{"positions", "x"}, {"last_message", 3}, {"title", 7},
				{"unread_count", "lots"}, {"chat", json::array()},
				{"authorization_state", nullptr}},
			{{"@type", type}, {"chat_id", "5"}, {"position", {{"list", 1}}},
				{"last_message", {{"content", nullptr}, {"date", "soon"}}}},
		};
		for (const json& body : bodies) {
			try {
				list.Apply(body);
				AuthFlow flow(TdParams{});
				std::vector<json> out;
				flow.Handle(body, out);
			} catch (const std::exception& e) {
				std::fprintf(stderr, "%s threw: %s\n", body.dump().c_str(), e.what());
				sFailures++;
			}
		}
	}

	AuthFlow flow(TdParams{});
	std::vector<json> out;
	flow.Handle(AuthUpdate({{"@type", "authorizationStateWaitCode"}}), out);
	CHECK(flow.State().step == AuthStep::Code);
	CHECK(flow.State().codeLength == 0);
	CHECK(AuthFlow::ErrorText(json()) == "Telegram reported an error.");
	CHECK(MessagePreview(json()) == "Message");
	CHECK(MessagePreview({{"@type", "messageText"}}) == "");
	CHECK(MessagePreview({{"@type", "messagePhoto"}, {"caption", nullptr}}) == "Photo");
}


static void
TestSendQueue()
{
	SendQueue q(2, 0.1);
	q.Push({{"@type", "getChatHistory"}});
	q.Push({{"@type", "getChatHistory"}});
	q.Push({{"@type", "getChatHistory"}});

	// One paced request per gap, and never more than two in flight.
	auto out = q.Take(0.0);
	CHECK(out.size() == 1);
	CHECK(Str(out[0], "@extra") == "q1");
	CHECK(q.Take(0.05).empty());
	CHECK(q.Take(0.1).size() == 1);
	CHECK(q.InFlight() == 2);
	CHECK(q.Take(0.5).empty());  // full
	q.OnObject({{"@type", "messages"}, {"@extra", "q1"}}, 0.5);
	CHECK(q.InFlight() == 1);
	CHECK(q.Take(0.5).size() == 1);

	// Urgent requests jump the queue; close passes even a full queue.
	q.Push({{"@type", "getChatHistory"}});
	q.Push({{"@type", "close"}}, true);
	out = q.Take(0.6);
	CHECK(out.size() == 1 && Type(out[0]) == "close");

	// A flood error stops everything but close/logOut for that long.
	q.OnObject({{"@type", "error"}, {"code", 429},
		{"message", "Too Many Requests: retry after 9"}, {"@extra", "q2"}}, 1.0);
	CHECK(q.PausedUntil() == 10.0);
	q.OnObject({{"@type", "ok"}, {"@extra", "q3"}}, 1.0);
	CHECK(q.InFlight() == 0);
	CHECK(q.Take(5.0).empty());
	q.Push({{"@type", "logOut"}}, true);
	out = q.Take(5.0);
	CHECK(out.size() == 1 && Type(out[0]) == "logOut");
	CHECK(q.Take(10.0).size() == 1);  // the waiting history request

	CHECK(SendQueue::RetryAfter({{"@type", "error"}, {"message", "FLOOD_WAIT_30"}}) == 30);
	CHECK(SendQueue::RetryAfter({{"@type", "error"}, {"code", 429}, {"message", "?"}}) == 5);
	CHECK(SendQueue::RetryAfter({{"@type", "error"}, {"code", 400}, {"message", "X"}}) == -1);
	CHECK(SendQueue::RetryAfter({{"@type", "ok"}}) == -1);

	// Existing @extra is kept (the login flow routes replies by it).
	SendQueue keep(4, 0);
	keep.Push({{"@type", "checkAuthenticationCode"}, {"@extra", "auth"}});
	CHECK(Str(keep.Take(0)[0], "@extra") == "auth");
}


static json
Msg(int64_t chat, int64_t id, const char* text, bool outgoing = false, int64_t user = 7)
{
	return {{"@type", "message"}, {"id", id}, {"chat_id", chat},
		{"sender_id", {{"@type", "messageSenderUser"}, {"user_id", user}}},
		{"is_outgoing", outgoing}, {"date", 100 + id},
		{"content", {{"@type", "messageText"}, {"text", {{"text", text}}}}}};
}


static void
TestChatHistory()
{
	ChatHistory h(42);
	// History replies may hold nulls and other chats' messages.
	CHECK(h.AddHistory({{"@type", "messages"}, {"messages",
		json::array({Msg(42, 3, "three"), nullptr, Msg(42, 1, "one\ntwo lines"),
			Msg(99, 2, "other chat")})}}) == 2);
	auto sorted = h.Sorted();
	CHECK(sorted.size() == 2 && sorted[0].id == 1 && sorted[1].id == 3);
	CHECK(sorted[0].text == "one\ntwo lines");
	CHECK(h.OldestId() == 1);

	CHECK(h.Apply({{"@type", "updateNewMessage"}, {"message", Msg(42, 5, "mine", true)}}));
	CHECK(!h.Apply({{"@type", "updateNewMessage"}, {"message", Msg(43, 6, "elsewhere")}}));

	// viewMessages only for incoming messages, each once.
	auto unviewed = h.TakeUnviewed();
	CHECK(unviewed.size() == 2 && unviewed[0] == 1 && unviewed[1] == 3);
	CHECK(h.TakeUnviewed().empty());
	h.Apply({{"@type", "updateNewMessage"}, {"message", Msg(42, 8, "new")}});
	CHECK(h.TakeUnviewed() == std::vector<int64_t>{8});

	// Self-destruct and deletions remove; cache eviction doesn't.
	CHECK(!h.Apply({{"@type", "updateDeleteMessages"}, {"chat_id", 42},
		{"message_ids", {3}}, {"is_permanent", false}, {"from_cache", true}}));
	CHECK(h.Apply({{"@type", "updateDeleteMessages"}, {"chat_id", 42},
		{"message_ids", {3}}, {"is_permanent", true}, {"from_cache", false}}));
	CHECK(h.Size() == 3);

	// A sent message swaps its temporary id for the real one.
	json pending = Msg(42, 1000, "sending", true);
	pending["sending_state"] = {{"@type", "messageSendingStatePending"}};
	h.Apply({{"@type", "updateNewMessage"}, {"message", pending}});
	CHECK(h.Sorted().back().pending);
	CHECK(h.Apply({{"@type", "updateMessageSendSucceeded"}, {"old_message_id", 1000},
		{"message", Msg(42, 9, "sending", true)}}));
	CHECK(h.Sorted().back().id == 9 && !h.Sorted().back().pending);

	CHECK(h.Apply({{"@type", "updateMessageContent"}, {"chat_id", 42}, {"message_id", 9},
		{"new_content", {{"@type", "messageText"}, {"text", {{"text", "edited"}}}}}}));
	CHECK(h.Apply({{"@type", "updateMessageEdited"}, {"chat_id", 42}, {"message_id", 9}}));
	CHECK(h.Sorted().back().text == "edited" && h.Sorted().back().edited);
	CHECK(h.Apply({{"@type", "updateChatReadOutbox"}, {"chat_id", 42},
		{"last_read_outbox_message_id", 9}}));
	CHECK(h.LastReadOutbox() == 9);

	Names names;
	CHECK(names.Apply({{"@type", "updateUser"}, {"user", {{"id", 7},
		{"first_name", "Tyrone"}, {"last_name", "Ford"}}}}));
	names.Apply({{"@type", "updateNewChat"}, {"chat", {{"id", -100}, {"title", "News"}}}});
	Message fromChat;
	fromChat.senderIsChat = true;
	fromChat.senderId = -100;
	CHECK(names.Sender(h.Sorted()[0]) == "Tyrone Ford");
	CHECK(names.Sender(fromChat) == "News");
	CHECK(names.User(12345).empty());

	CHECK(MessageText({{"@type", "messagePhoto"}, {"caption", {{"text", "Look"}}}}) == "Look");
	CHECK(request::SendText(42, "hi")["input_message_content"]["text"]["text"] == "hi");
	CHECK(request::Typing(42, false)["action"]["@type"] == "chatActionCancel");
	CHECK(request::Online(true)["value"]["value"] == true);
	CHECK(request::ViewMessages(42, {1, 2})["force_read"] == false);
	CHECK(request::History(42, 0, 30)["limit"] == 30);

	TypingThrottle typing;
	CHECK(typing.ShouldSend(0));
	CHECK(!typing.ShouldSend(4.9));
	CHECK(typing.ShouldSend(5.0));
}


static void
TestRepliesAndForwards()
{
	ChatHistory h(42);
	json reply = Msg(42, 10, "agreed");
	reply["reply_to"] = {{"@type", "messageReplyToMessage"}, {"chat_id", 42},
		{"message_id", 4}, {"quote", {{"text", {{"text", "the plan"}}}}}};
	json crossChat = Msg(42, 11, "see this");
	crossChat["reply_to"] = {{"@type", "messageReplyToMessage"}, {"chat_id", 77},
		{"message_id", 3}, {"content", {{"@type", "messageText"}, {"text", {{"text", "elsewhere"}}}}}};
	json forwarded = Msg(42, 12, "fwd");
	forwarded["forward_info"] = {{"origin", {{"@type", "messageOriginHiddenUser"},
		{"sender_name", "Anon"}}}};
	json fromChannel = Msg(42, 13, "news");
	fromChannel["forward_info"] = {{"origin", {{"@type", "messageOriginChannel"},
		{"chat_id", -100}, {"message_id", 5}}}};
	h.AddHistory({{"messages", json::array({Msg(42, 2, "old"), reply, crossChat,
		forwarded, fromChannel})}});

	const Message* r = h.Find(10);
	CHECK(r && r->replyToId == 4 && r->replyQuote == "the plan");
	CHECK(h.ReplyTarget(*r) == nullptr);
	CHECK(h.Find(11)->replyText == "elsewhere");

	// Only the same-chat target that isn't loaded is fetched, once.
	CHECK(h.TakeMissingReplies() == std::vector<int64_t>{4});
	CHECK(h.TakeMissingReplies().empty());
	h.AddReplyTargets({{"@type", "messages"}, {"messages",
		json::array({nullptr, Msg(42, 4, "the plan is good"), Msg(99, 4, "wrong chat")})}});
	h.AddReplyTargets(json());
	CHECK(h.ReplyTarget(*r) && h.ReplyTarget(*r)->text == "the plan is good");
	CHECK(h.Size() == 5);  // targets aren't shown as history

	Names names;
	names.Apply({{"@type", "updateNewChat"}, {"chat", {{"id", -100}, {"title", "News"}}}});
	CHECK(h.Find(12)->forwarded && names.ForwardOrigin(*h.Find(12)) == "Anon");
	CHECK(names.ForwardOrigin(*h.Find(13)) == "News");
	CHECK(!h.Find(2)->forwarded);

	json send = request::SendText(42, "hi", 10);
	CHECK(send["reply_to"]["message_id"] == 10);
	CHECK(!request::SendText(42, "hi").contains("reply_to"));
	CHECK(request::Delete(42, {1}, true)["revoke"] == true);
	CHECK(request::GetMessages(42, {4, 5}, "reply:42")["message_ids"].size() == 2);
	CHECK(request::EditText(42, 9, "x")["input_message_content"]["text"]["text"] == "x");
}


static void
TestNotifications()
{
	ChatList list;
	json chat = NewChat(1, "Friend", "100");
	chat["chat"]["type"] = {{"@type", "chatTypePrivate"}, {"user_id", 7}};
	list.Apply(chat);
	json group = NewChat(-2, "Group", "200");
	group["chat"]["type"] = {{"@type", "chatTypeSupergroup"}, {"is_channel", false}};
	group["chat"]["notification_settings"] = {{"use_default_mute_for", false}, {"mute_for", 3600}};
	list.Apply(group);
	json channel = NewChat(-3, "Channel", "300");
	channel["chat"]["type"] = {{"@type", "chatTypeSupergroup"}, {"is_channel", true}};
	list.Apply(channel);

	auto message = [](int64_t chat, int64_t date, bool out = false) {
		return json{{"chat_id", chat}, {"date", date}, {"is_outgoing", out}};
	};
	CHECK(list.ShouldNotify(message(1, 1000), 0, 1000));
	CHECK(!list.ShouldNotify(message(1, 1001), 0, 1001));   // within 3 s
	CHECK(list.ShouldNotify(message(1, 1004), 0, 1004));
	CHECK(!list.ShouldNotify(message(1, 1010, true), 0, 1010));  // ours
	CHECK(!list.ShouldNotify(message(1, 1020), 1, 1020));  // on screen
	CHECK(!list.ShouldNotify(message(1, 100), 0, 1030));   // replayed, old
	CHECK(!list.ShouldNotify(message(-2, 1040), 0, 1040)); // muted chat
	CHECK(!list.ShouldNotify(message(55, 1040), 0, 1040)); // unknown chat

	// Channels muted by their scope, until a chat overrides it.
	CHECK(list.ShouldNotify(message(-3, 1050), 0, 1050));
	list.Apply({{"@type", "updateScopeNotificationSettings"},
		{"scope", {{"@type", "notificationSettingsScopeChannelChats"}}},
		{"notification_settings", {{"mute_for", 99999}, {"show_preview", false}}}});
	CHECK(list.IsMuted(-3) && !list.ShowsPreview(-3));
	CHECK(!list.IsMuted(1) && list.ShowsPreview(1));
	list.Apply({{"@type", "updateChatNotificationSettings"}, {"chat_id", -3},
		{"notification_settings", {{"use_default_mute_for", false}, {"mute_for", 0}}}});
	CHECK(!list.IsMuted(-3));
}


static json
File(int32_t id, const char* path = "", bool done = false, bool active = false)
{
	return {{"@type", "file"}, {"id", id}, {"local", {{"path", path},
		{"is_downloading_completed", done}, {"is_downloading_active", active},
		{"can_be_downloaded", true}}}};
}


static void
TestMedia()
{
	// A photo: blurred inline preview, the size nearest 480 px, the largest
	// for the viewer. "AAEC" is base64 for bytes 0 1 2.
	json photo = {{"minithumbnail", {{"width", 40}, {"height", 30}, {"data", "AAEC"}}},
		{"sizes", json::array({
			{{"type", "m"}, {"width", 320}, {"height", 240}, {"photo", File(1)}},
			{{"type", "x"}, {"width", 800}, {"height", 600}, {"photo", File(2)}},
			{{"type", "y"}, {"width", 1280}, {"height", 960}, {"photo", File(3)}}})}};
	Media m = PhotoMedia(photo);
	CHECK(m.kind == Media::kPhoto && m.fileId == 2 && m.fullFileId == 3);
	CHECK(m.width == 800 && m.height == 600);
	CHECK(m.minithumbnail == std::string("\x00\x01\x02", 3));
	CHECK(PhotoMedia(photo, 2000).fileId == 3);  // none big enough: largest
	CHECK(PhotoMedia(json()).fileId == 0);
	CHECK(DecodeBase64("aGVsbG8=") == "hello");

	json message = Msg(42, 1, "");
	message["content"] = {{"@type", "messagePhoto"}, {"photo", photo},
		{"caption", {{"text", "Look"}}}, {"has_spoiler", true}, {"is_secret", false}};
	Message parsed = ParseMessage(message);
	CHECK(parsed.text == "Look" && parsed.media.spoiler && parsed.media.fileId == 2);

	// Stickers: WebP shown as is; animated ones by an image thumbnail, or
	// by their emoji when the thumbnail is animated too.
	json webp = {{"emoji", "😀"}, {"width", 512}, {"height", 512},
		{"format", {{"@type", "stickerFormatWebp"}}}, {"sticker", File(10)},
		{"thumbnail", {{"format", {{"@type", "thumbnailFormatWebp"}}}, {"file", File(11)}}}};
	CHECK(StickerMedia(webp).fileId == 10);
	json tgs = webp;
	tgs["format"] = {{"@type", "stickerFormatTgs"}};
	CHECK(StickerMedia(tgs).fileId == 11);
	tgs["thumbnail"]["format"] = {{"@type", "thumbnailFormatTgs"}};
	CHECK(StickerMedia(tgs).fileId == 0 && StickerMedia(tgs).emoji == "😀");
	message["content"] = {{"@type", "messageSticker"}, {"sticker", webp}};
	CHECK(ParseMessage(message).media.kind == Media::kSticker);
	CHECK(ParseMessage(message).text.empty());

	// Files: paths from messages and updates, a capped queue, most wanted first.
	FileStore files(2);
	auto done = files.Apply({{"@type", "updateNewMessage"}, {"message",
		{{"content", {{"photo", {{"sizes", json::array({{{"photo",
			File(5, "/tmp/5.jpg", true)}}})}}}}}}}});
	CHECK(done == std::vector<int32_t>{5} && files.Path(5) == "/tmp/5.jpg");
	files.Want(5, 10);   // already here: nothing to do
	files.Want(6, 1);
	files.Want(7, 20);
	files.Want(8, 5);
	auto requests = files.TakeDownloads();
	CHECK(requests.size() == 2);
	CHECK(requests[0]["file_id"] == 7 && requests[1]["file_id"] == 8);
	CHECK(files.TakeDownloads().empty());
	CHECK(files.Apply({{"@type", "updateFile"}, {"file", File(7, "", false, true)}}).empty());
	CHECK(files.Apply({{"@type", "updateFile"}, {"file", File(7, "/tmp/7", true)}})
		== std::vector<int32_t>{7});
	CHECK(files.Active() == 1);
	files.DownloadFailed(8);
	requests = files.TakeDownloads();
	CHECK(requests.size() == 1 && requests[0]["file_id"] == 6);
	CHECK(request::Download(1, 99)["priority"] == 32);
	CHECK(request::OpenContent(42, 1)["@type"] == "openMessageContent");
	json info = {{"reactions", {{"reactions", json::array({
		{{"type", {{"@type", "reactionTypeEmoji"}, {"emoji", "👍"}}}, {"total_count", 3}, {"is_chosen", true}},
		{{"type", {{"@type", "reactionTypeCustomEmoji"}, {"custom_emoji_id", 5}}}, {"total_count", 1}},
		{{"type", {{"@type", "reactionTypePaid"}}}, {"total_count", 9}}})}}}};
	auto reactions = ParseReactions(info);
	CHECK(reactions.size() == 1 && reactions[0].emoji == "👍" && reactions[0].count == 3 && reactions[0].chosen);
	CHECK(ParseReactions(json()).empty());
	ChatHistory rh(42);
	rh.AddHistory({{"messages", json::array({Msg(42, 1, "hi")})}});
	CHECK(rh.Apply({{"@type", "updateMessageInteractionInfo"}, {"chat_id", 42}, {"message_id", 1},
		{"interaction_info", info}}));
	CHECK(rh.Find(1)->reactions.size() == 1);
	CHECK(request::React(42, 1, "👍", true)["@type"] == "addMessageReaction");
	CHECK(request::React(42, 1, "👍", false)["@type"] == "removeMessageReaction");
	CHECK(!request::React(42, 1, "👍", false).contains("is_big"));
	json search = request::SearchChat(42, "Suspend", 50);
	CHECK(search["query"] == "Suspend" && search["@extra"] == "search:42");
	CHECK(search["filter"]["@type"] == "searchMessagesFilterEmpty");
	CHECK((FoundIds({{"@type", "foundChatMessages"}, {"messages",
		json::array({Msg(42, 9, "a"), nullptr, Msg(42, 3, "b")})}}) == std::vector<int64_t>{9, 3}));
	CHECK(FoundIds(json()).empty());
	json fwd = request::Forward(7, 42, {1, 2});
	CHECK(fwd["chat_id"] == 7 && fwd["from_chat_id"] == 42 && fwd["message_ids"].size() == 2);
}


static void
TestVideosAvatarsSending()
{
	// Video: its JPEG thumbnail in the bubble, the video file to open.
	json video = {{"duration", 42}, {"width", 1280}, {"height", 720},
		{"minithumbnail", {{"width", 40}, {"height", 22}, {"data", "AAEC"}}},
		{"thumbnail", {{"format", {{"@type", "thumbnailFormatJpeg"}}},
			{"width", 320}, {"height", 180}, {"file", File(20)}}},
		{"video", File(21)}};
	json message = Msg(42, 1, "");
	message["content"] = {{"@type", "messageVideo"}, {"video", video},
		{"caption", {{"text", "Clip"}}}};
	Message m = ParseMessage(message);
	CHECK(m.media.kind == Media::kVideo && m.media.fileId == 20 && m.media.fullFileId == 21);
	CHECK(m.media.duration == 42 && m.media.width == 1280 && m.text == "Clip");

	// An MPEG-4 thumbnail can't be drawn: the inline preview stays.
	json gif = video;
	gif["thumbnail"]["format"] = {{"@type", "thumbnailFormatMpeg4"}};
	gif["animation"] = File(22);
	message["content"] = {{"@type", "messageAnimation"}, {"animation", gif}};
	m = ParseMessage(message);
	CHECK(m.media.kind == Media::kAnimation && m.media.fileId == 0);
	CHECK(m.media.fullFileId == 22 && !m.media.minithumbnail.empty());

	json note = {{"duration", 9}, {"length", 384}, {"thumbnail",
		{{"format", {{"@type", "thumbnailFormatJpeg"}}}, {"file", File(23)}}}, {"video", File(24)}};
	message["content"] = {{"@type", "messageVideoNote"}, {"video_note", note}};
	m = ParseMessage(message);
	CHECK(m.media.kind == Media::kVideoNote && m.media.width == 384 && m.media.height == 384);

	// Documents and music: a card with the name and size, the caption as
	// text, and the file fetched only when asked for.
	json pdf = File(25);
	pdf["size"] = 1000;
	message["content"] = {{"@type", "messageDocument"}, {"document", {{"file_name", "notes.pdf"},
		{"mime_type", "application/pdf"}, {"document", pdf}}},
		{"caption", {{"text", "Read this"}}}};
	m = ParseMessage(message);
	CHECK(m.media.kind == Media::kDocument && m.text == "Read this");
	CHECK(m.media.fileId == 0 && m.media.fullFileId == 25 && m.media.fileName == "notes.pdf");
	CHECK(m.media.mimeType == "application/pdf" && m.media.fileSize == 1000);
	message["content"] = {{"@type", "messageAudio"}, {"audio", {{"file_name", "track01.mp3"},
		{"title", "Blue"}, {"performer", "Band"}, {"duration", 185}, {"audio", File(26)}}},
		{"caption", {{"text", ""}}}};
	m = ParseMessage(message);
	CHECK(m.media.kind == Media::kDocument && m.text.empty() && m.media.fullFileId == 26);
	CHECK(m.media.title == "Band \xE2\x80\x93 Blue" && m.media.fileName == "track01.mp3");
	CHECK(m.media.duration == 185);

	// Download progress, for the card.
	FileStore progress;
	progress.Want(25, 32);
	CHECK(progress.Downloading(25) && progress.TakeDownloads().size() == 1);
	json part = File(25, "", false, true);
	part["local"]["downloaded_size"] = 400;
	progress.Apply({{"@type", "updateFile"}, {"file", part}});
	CHECK(progress.Downloaded(25) == 400 && progress.Downloading(25));
	part = File(25, "/f/notes.pdf", true);
	part["local"]["downloaded_size"] = 1000;
	CHECK(progress.Apply({{"@type", "updateFile"}, {"file", part}}).size() == 1);
	CHECK(!progress.Downloading(25) && progress.Path(25) == "/f/notes.pdf");

	// Chat photos: the small file, its preview, and updates to both.
	ChatList list;
	json chat = NewChat(1, "Friend", "100");
	chat["chat"]["photo"] = {{"small", File(30)}, {"big", File(31)},
		{"minithumbnail", {{"data", "AAEC"}}}};
	list.Apply(chat);
	CHECK(list.Find(1)->photoFileId == 30 && list.Find(1)->photoPreview.size() == 3);
	CHECK(list.Apply({{"@type", "updateChatPhoto"}, {"chat_id", 1}, {"photo", nullptr}}));
	CHECK(list.Find(1)->photoFileId == 0);

	json photo = request::SendPhoto(42, "/boot/home/a.jpg", 800, 600, "Hi", 7);
	CHECK(photo["input_message_content"]["photo"]["photo"]["path"] == "/boot/home/a.jpg");
	CHECK(photo["input_message_content"]["photo"]["width"] == 800);
	CHECK(photo["input_message_content"]["caption"]["text"] == "Hi");
	CHECK(photo["reply_to"]["message_id"] == 7);
	json doc = request::SendDocument(42, "/boot/home/b.pdf", "");
	CHECK(doc["input_message_content"]["document"]["document"]["@type"] == "inputFileLocal");
	CHECK(!doc.contains("reply_to"));
}


static void
TestPhotosAndStickers()
{
	Names names;
	names.Apply({{"@type", "updateUser"}, {"user", {{"id", 7}, {"first_name", "Ann"},
		{"profile_photo", {{"small", File(40)}, {"minithumbnail", {{"data", "AAEC"}}}}}}}});
	names.Apply({{"@type", "updateUser"}, {"user", {{"id", 8}, {"first_name", "Bo"}}}});
	names.Apply({{"@type", "updateNewChat"}, {"chat", {{"id", -100}, {"title", "News"},
		{"photo", {{"small", File(41)}}}}}});
	Message fromUser;
	fromUser.senderId = 7;
	CHECK(names.SenderPhoto(fromUser).fileId == 40);
	CHECK(names.SenderPhoto(fromUser).preview.size() == 3);
	fromUser.senderId = 8;
	CHECK(names.SenderPhoto(fromUser).fileId == 0);  // no photo: initials
	Message fromChat;
	fromChat.senderIsChat = true;
	fromChat.senderId = -100;
	CHECK(names.SenderPhoto(fromChat).fileId == 41);
	names.Apply({{"@type", "updateChatPhoto"}, {"chat_id", -100}});
	CHECK(names.SenderPhoto(fromChat).fileId == 0);

	// Stickers: the thumbnail when drawable, the WebP itself, or nothing.
	json webp = {{"emoji", "😀"}, {"width", 512}, {"height", 512},
		{"format", {{"@type", "stickerFormatWebp"}}}, {"sticker", File(50)},
		{"thumbnail", {{"format", {{"@type", "thumbnailFormatWebp"}}}, {"file", File(51)}}}};
	json tgs = {{"emoji", "🎉"}, {"width", 512}, {"height", 512},
		{"format", {{"@type", "stickerFormatTgs"}}}, {"sticker", File(52)},
		{"thumbnail", {{"format", {{"@type", "thumbnailFormatTgs"}}}, {"file", File(53)}}}};
	json bare = webp;
	bare.erase("thumbnail");
	auto items = ParseStickers({{"@type", "stickers"}, {"stickers", json::array({webp, tgs, bare, json()})}});
	CHECK(items.size() == 3);
	CHECK(items[0].fileId == 50 && items[0].imageFileId == 51);
	CHECK(items[1].fileId == 52 && items[1].imageFileId == 0 && items[1].emoji == "🎉");
	CHECK(items[2].imageFileId == 50);

	auto sets = ParseStickerSets({{"@type", "stickerSets"}, {"sets", json::array({
		{{"id", "123456789012"}, {"title", "Cats"}, {"covers", json::array({webp})}},
		{{"id", 0}, {"title", "broken"}}})}});
	CHECK(sets.size() == 1 && sets[0].id == 123456789012LL && sets[0].title == "Cats");
	CHECK(sets[0].covers.size() == 1);
	CHECK(ParseStickerSets(json()).empty());

	json send = request::SendSticker(42, items[0], 9);
	CHECK(send["input_message_content"]["sticker"]["sticker"]["id"] == 50);
	CHECK(send["input_message_content"]["emoji"] == "😀");
	CHECK(send["reply_to"]["message_id"] == 9);
	CHECK(request::StickerSet(5)["@extra"] == "sticker_set:5");
	CHECK(request::InstalledStickerSets()["sticker_type"]["@type"] == "stickerTypeRegular");

	const auto& catalog = EmojiCatalog();
	CHECK(catalog.size() >= 6);
	size_t total = 0;
	for (const auto& category : catalog) {
		total += category.emoji.size();
		for (const std::string& e : category.emoji)
			CHECK((unsigned char)e[0] >= 0xC0);  // every entry is a non-ASCII character
	}
	CHECK(total > 400);
	CHECK(catalog[0].emoji[0] == "😀");
}


static void
TestFormatting()
{
	// "Hi 😀 bold link": the emoji is two UTF-16 units and four UTF-8 bytes.
	std::string text = "Hi \xF0\x9F\x98\x80 bold link";
	CHECK(Utf16ToUtf8(text, 3) == 3);
	CHECK(Utf16ToUtf8(text, 5) == 7);   // after the emoji
	CHECK(Utf16ToUtf8(text, 100) == (int32_t)text.size());
	json formatted = {{"text", text}, {"entities", json::array({
		{{"offset", 6}, {"length", 4}, {"type", {{"@type", "textEntityTypeBold"}}}},
		{{"offset", 11}, {"length", 4}, {"type", {{"@type", "textEntityTypeTextUrl"},
			{"url", "https://haiku-os.org"}}}},
		{{"offset", 0}, {"length", 2}, {"type", {{"@type", "textEntityTypeHashtag"}}}}})}};
	auto spans = ParseEntities(formatted);
	CHECK(spans.size() == 2);
	CHECK(spans[0].style == kBold && text.substr(spans[0].start, spans[0].length) == "bold");
	CHECK(spans[1].style == kLink && spans[1].url == "https://haiku-os.org");
	CHECK(text.substr(spans[1].start, spans[1].length) == "link");

	auto back = DecodeSpans(EncodeSpans(spans));
	CHECK(back.size() == 2 && back[1].url == "https://haiku-os.org" && back[0].length == 4);
	CHECK(DecodeSpans("").empty() && DecodeSpans("garbage\n").empty());

	auto links = FindLinks("see https://example.org/a. and www.haiku-os.org, (http://x.y)");
	CHECK(links.size() == 3);
	CHECK(links[0].url == "https://example.org/a");
	CHECK(links[1].url == "https://www.haiku-os.org");
	CHECK(links[2].url == "http://x.y");
	CHECK(FindLinks("nohttp here and shttp://x").empty());

	// Messages carry spans: entities, or links found in plain text.
	json message = Msg(42, 1, "");
	message["content"] = {{"@type", "messageText"}, {"text", formatted}};
	Message m = ParseMessage(message);
	CHECK(m.spans.size() == 2);
	message["content"] = {{"@type", "messageText"}, {"text", {{"text", "go to www.a.org now"}}}};
	m = ParseMessage(message);
	CHECK(m.spans.size() == 1 && m.spans[0].url == "https://www.a.org");
}


static void
TestPresence()
{
	Presence p;
	p.Apply({{"@type", "updateNewChat"}, {"chat", {{"id", 7},
		{"type", {{"@type", "chatTypePrivate"}, {"user_id", 70}}}}}}, 0);
	p.Apply({{"@type", "updateNewChat"}, {"chat", {{"id", -5},
		{"type", {{"@type", "chatTypeSupergroup"}, {"supergroup_id", 50}, {"is_channel", false}}}}}}, 0);
	p.Apply({{"@type", "updateNewChat"}, {"chat", {{"id", -6},
		{"type", {{"@type", "chatTypeBasicGroup"}, {"basic_group_id", 60}}}}}}, 0);
	CHECK(p.PrivateUser(7) == 70 && p.PrivateUser(-5) == 0);
	CHECK(p.MemberCount(-5) == 0);
	CHECK(p.Apply({{"@type", "updateSupergroup"}, {"supergroup", {{"id", 50}, {"member_count", 128}}}}, 0));
	p.Apply({{"@type", "updateBasicGroup"}, {"basic_group", {{"id", 60}, {"member_count", 3}}}}, 0);
	CHECK(p.MemberCount(-5) == 128 && p.MemberCount(-6) == 3 && !p.IsChannel(-5));

	p.Apply({{"@type", "updateUser"}, {"user", {{"id", 70},
		{"status", {{"@type", "userStatusOffline"}, {"was_online", 1000}}}}}}, 0);
	CHECK(p.Status(70).kind == UserStatus::kOffline && p.Status(70).time == 1000);
	p.Apply({{"@type", "updateUserStatus"}, {"user_id", 70},
		{"status", {{"@type", "userStatusOnline"}, {"expires", 2000}}}}, 0);
	CHECK(p.Status(70).kind == UserStatus::kOnline);
	CHECK(p.Status(99).kind == UserStatus::kUnknown);

	auto action = [](int64_t chat, int64_t user, const char* type) {
		return json{{"@type", "updateChatAction"}, {"chat_id", chat},
			{"sender_id", {{"@type", "messageSenderUser"}, {"user_id", user}}},
			{"action", {{"@type", type}}}};
	};
	p.Apply(action(7, 70, "chatActionTyping"), 100);
	std::map<int64_t, std::string> names = {{70, "Ann Example"}, {71, "Bo Sample"}, {72, "Cy"}};
	CHECK(ActionText(p.Actions(7, 103), names, true) == "typing…");
	CHECK(p.Actions(7, 107).empty());  // stale after 6 s
	p.Apply(action(-5, 70, "chatActionRecordingVoiceNote"), 200);
	CHECK(ActionText(p.Actions(-5, 201), names, false) == "Ann is recording a voice message…");
	p.Apply(action(-5, 71, "chatActionTyping"), 200);
	CHECK(ActionText(p.Actions(-5, 201), names, false).find(" and ") != std::string::npos);
	p.Apply(action(-5, 72, "chatActionTyping"), 200);
	CHECK(ActionText(p.Actions(-5, 201), names, false) == "3 people are typing…");
	p.Apply(action(-5, 72, "chatActionCancel"), 201);
	CHECK(p.Actions(-5, 201).size() == 2);
	CHECK(ActionText({}, names, false).empty());
}


static void
TestDrafts()
{
	ChatList list;
	json chat = NewChat(1, "Friend", "100");
	chat["chat"]["draft_message"] = {{"content", {{"@type", "draftMessageContentText"},
		{"text", {{"text", "half a thought"}}}}}};
	list.Apply(chat);
	CHECK(list.Find(1)->draft == "half a thought");
	CHECK(list.Apply({{"@type", "updateChatDraftMessage"}, {"chat_id", 1},
		{"positions", json::array()}}));
	CHECK(list.Find(1)->draft.empty());
	list.Apply({{"@type", "updateChatDraftMessage"}, {"chat_id", 1},
		{"draft_message", {{"content", {{"@type", "draftMessageContentVoiceNote"}}}}}});
	CHECK(list.Find(1)->draft.empty());  // voice drafts aren't text

	json save = ChatList::DraftRequest(1, "later");
	CHECK(save["draft_message"]["content"]["text"]["text"] == "later");
	CHECK(!ChatList::DraftRequest(1, "").contains("draft_message"));
	CHECK(request::SendText(1, "x")["input_message_content"]["clear_draft"] == true);
}


static void
TestUnreadAndNewer()
{
	ChatList list;
	json chat = NewChat(1, "Friend", "100", Msg(1, 50, "latest"));
	chat["chat"]["unread_count"] = 3;
	chat["chat"]["last_read_inbox_message_id"] = 47;
	list.Apply(chat);
	CHECK(list.Find(1)->lastMessageId == 50 && list.Find(1)->lastReadInbox == 47);
	list.Apply({{"@type", "updateChatReadInbox"}, {"chat_id", 1}, {"unread_count", 0},
		{"last_read_inbox_message_id", 50}});
	CHECK(list.Find(1)->lastReadInbox == 50);

	json around = request::HistoryAround(1, 47, 20, 39);
	CHECK(around["offset"] == -39 && around["limit"] == 60 && around["from_message_id"] == 47);
	CHECK(request::HistoryNewer(1, 47, 40)["limit"] == 41);

	// Away from the latest messages, new ones aren't added (no gaps).
	ChatHistory h(1);
	h.AddHistory({{"messages", json::array({Msg(1, 40, "a"), Msg(1, 41, "b")})}});
	CHECK(h.NewestId() == 41 && h.OldestId() == 40);
	h.SetAtLatest(false);
	CHECK(!h.Apply({{"@type", "updateNewMessage"}, {"message", Msg(1, 60, "far")}}));
	h.SetAtLatest(true);
	CHECK(h.Apply({{"@type", "updateNewMessage"}, {"message", Msg(1, 61, "here")}}));
	CHECK(h.NewestId() == 61);
}


static void
TestLists()
{
	ChatList list;
	list.Apply(NewChat(1, "Main chat", "100"));
	list.Apply(NewChat(2, "Archived", nullptr));
	CHECK(list.Apply({{"@type", "updateChatPosition"}, {"chat_id", 2}, {"position",
		{{"list", {{"@type", "chatListArchive"}}}, {"order", "50"}, {"is_pinned", true}}}}));
	list.Apply({{"@type", "updateChatPosition"}, {"chat_id", 1}, {"position",
		{{"list", {{"@type", "chatListFolder"}, {"chat_folder_id", 3}}}, {"order", "10"}}}});
	CHECK(list.Sorted("main").size() == 1 && list.Sorted("main")[0].id == 1);
	CHECK(list.Sorted("archive").size() == 1 && list.Sorted("archive")[0].pinned);
	CHECK(list.Sorted("folder:3").size() == 1);
	CHECK(list.Find(2)->InList("archive") && !list.Find(2)->InList("main"));
	CHECK(ListKey({{"@type", "chatListFolder"}, {"chat_folder_id", 7}}) == "folder:7");
	CHECK(ListKey(json()).empty());

	CHECK(list.Apply({{"@type", "updateChatIsMarkedAsUnread"}, {"chat_id", 1},
		{"is_marked_as_unread", true}}));
	CHECK(list.Find(1)->markedUnread);

	CHECK(list.Apply({{"@type", "updateChatFolders"}, {"chat_folders", json::array({
		{{"id", 3}, {"name", {{"text", {{"text", "Work"}}}}}},
		{{"id", 5}, {"name", {{"text", {{"text", "Family"}}}}}}})}}));
	CHECK(list.Folders().size() == 2 && list.Folders()[1].name == "Family");
	CHECK(ChatList::LoadRequest(50, "archive")["chat_list"]["@type"] == "chatListArchive");
	CHECK(ChatList::LoadRequest(50)["chat_list"]["@type"] == "chatListMain");

	Chat c = *list.Find(1);
	c.lastMessageId = 9;
	auto read = ChatList::MarkReadRequests(c);
	CHECK(read.size() == 2 && read[0]["force_read"] == true && read[1]["is_marked_as_unread"] == false);
	// Mentions: cleared by Mark as read, and counted down as they're read.
	c.unreadMentions = 2;
	read = ChatList::MarkReadRequests(c);
	CHECK(read.size() == 3 && read[2]["@type"] == "readAllChatMentions");
	CHECK(list.Apply({{"@type", "updateChatUnreadMentionCount"}, {"chat_id", 1}, {"unread_mention_count", 2}}));
	CHECK(list.Find(1)->unreadMentions == 2);
	list.Apply({{"@type", "updateMessageMentionRead"}, {"chat_id", 1}, {"message_id", 5},
		{"unread_mention_count", 0}});
	CHECK(list.Find(1)->unreadMentions == 0);
	json mute = ChatList::MuteRequest(1, true);
	CHECK(mute["notification_settings"]["mute_for"] == 366 * 86400);
	CHECK(mute["notification_settings"]["use_default_sound"] == true);
	CHECK(ChatList::MuteRequest(1, false)["notification_settings"]["use_default_mute_for"] == true);
	CHECK(ChatList::PinRequest(1, "archive", true)["chat_list"]["@type"] == "chatListArchive");
	CHECK(ChatList::MoveRequest(1, "folder:3")["chat_list"]["chat_folder_id"] == 3);
	c.kind = Chat::kPrivate;
	CHECK(ChatList::DeleteRequests(c).size() == 1);
	c.kind = Chat::kGroup;
	auto leave = ChatList::DeleteRequests(c);
	CHECK(leave.size() == 2 && leave[0]["@type"] == "leaveChat");
}


static void
TestPollsPlaces()
{
	json poll = {{"@type", "messagePoll"}, {"poll", {{"question", {{"text", "Lunch?"}}},
		{"options", json::array({
			{{"text", {{"text", "Pizza"}}}, {"vote_percentage", 75}, {"is_chosen", true}},
			{{"text", {{"text", "Soup"}}}, {"vote_percentage", 25}}})},
		{"total_voter_count", 4}, {"can_see_results", true}}}};
	std::string text = MessageText(poll);
	CHECK(text.find("Poll: Lunch?") == 0);
	CHECK(text.find("Pizza \xE2\x80\x93 75% (your vote)") != std::string::npos);
	CHECK(text.find("4 votes") != std::string::npos);
	poll["poll"]["can_see_results"] = false;
	CHECK(MessageText(poll).find("%") == std::string::npos);
	CHECK(MessagePreview(poll) == "Poll: Lunch?");

	json location = {{"@type", "messageLocation"}, {"location", {{"latitude", 52.52}, {"longitude", 13.405}}}};
	text = MessageText(location);
	CHECK(text.find("Location: 52.52000, 13.40500") == 0);
	auto links = FindLinks(text);
	CHECK(links.size() == 1 && links[0].url.find("openstreetmap.org/?mlat=52.52000") != std::string::npos);
	json venue = {{"@type", "messageVenue"}, {"venue", {{"title", "Cafe"}, {"address", "Main St 1"},
		{"location", {{"latitude", 1.0}, {"longitude", 2.0}}}}}};
	CHECK(MessageText(venue).find("Place: Cafe\nMain St 1\nhttps://") == 0);
	json contact = {{"@type", "messageContact"}, {"contact", {{"first_name", "Ann"},
		{"last_name", "Example"}, {"phone_number", "+15550100"}}}};
	CHECK(MessageText(contact) == "Contact: Ann Example\n+15550100");
	CHECK(MessagePreview(contact) == "Contact: Ann");

	// The link in a location message is clickable.
	json message = Msg(42, 1, "");
	message["content"] = location;
	CHECK(ParseMessage(message).spans.size() == 1);
}


static void
TestVoice()
{
	// Levels 31 0 17 5 1 30 2 16, five bits each, low bits first.
	CHECK(UnpackWaveform(DecodeBase64("H8QSvIA=")) == std::string("\x1f\x00\x11\x05\x01\x1e\x02\x10", 8));
	CHECK(UnpackWaveform("").empty());

	json message = Msg(42, 1, "");
	message["content"] = {{"@type", "messageVoiceNote"},
		{"voice_note", {{"duration", 7}, {"waveform", "H8QSvIA="}, {"mime_type", "audio/ogg"},
			{"voice", File(77)}}},
		{"caption", {{"text", "listen"}}}};
	Message parsed = ParseMessage(message);
	CHECK(parsed.media.kind == Media::kVoice && parsed.media.duration == 7);
	CHECK(parsed.media.fullFileId == 77 && parsed.media.fileId == 0);
	CHECK(parsed.media.waveform.size() == 8 && parsed.text == "listen");
}


static void
TestChatInfo()
{
	// A supergroup asks for its info, members and photos; a channel
	// doesn't ask for members.
	std::vector<json> group = request::ChatInfo(-100, 0, 0, 55, false);
	CHECK(group.size() == 3 && group[0]["@type"] == "getSupergroupFullInfo"
		&& group[1]["@type"] == "getSupergroupMembers" && group[2]["@type"] == "searchChatMessages");
	CHECK(group[2]["filter"]["@type"] == "searchMessagesFilterPhoto");
	CHECK(group[1]["@extra"] == "info:-100");
	CHECK(request::ChatInfo(-100, 0, 0, 55, true).size() == 2);
	CHECK(request::ChatInfo(7, 7, 0, 0, false)[0]["@type"] == "getUserFullInfo");

	ChatInfo info;
	CHECK(info.Apply({{"@type", "userFullInfo"}, {"bio", {{"text", "Haiku fan"}}}}));
	CHECK(info.about == "Haiku fan");
	info.Apply({{"@type", "supergroupFullInfo"}, {"description", "Testers"}, {"member_count", 120}});
	CHECK(info.about == "Testers" && info.memberCount == 120);
	info.Apply({{"@type", "chatMembers"}, {"total_count", 118}, {"members", json::array({
		{{"member_id", {{"@type", "messageSenderUser"}, {"user_id", 5}}}},
		{{"member_id", {{"@type", "messageSenderChat"}, {"chat_id", -9}}}},
		{{"member_id", {{"@type", "messageSenderUser"}, {"user_id", 6}}}}})}});
	CHECK(info.members == std::vector<int64_t>({5, 6}) && info.memberCount == 120);
	info.Apply({{"@type", "basicGroupFullInfo"}, {"description", ""}, {"members", json::array({
		{{"member_id", {{"@type", "messageSenderUser"}, {"user_id", 8}}}}})}});
	CHECK(info.memberCount == 1 && info.members.size() == 1);

	json photo = {{"sizes", json::array({{{"type", "m"}, {"width", 320}, {"height", 240},
		{"photo", File(3)}}})}};
	json found = {{"@type", "foundChatMessages"}, {"messages", json::array({Msg(1, 1, "hi"), Msg(1, 2, "")})}};
	found["messages"][1]["content"] = {{"@type", "messagePhoto"}, {"photo", photo}};
	CHECK(info.Apply(found) && info.photos.size() == 1 && info.photos[0].id == 2);
	CHECK(!info.Apply({{"@type", "ok"}}));
}


static void
TestEmojiMessages()
{
	// One emoji on its own: a big emoji, no text; the chat list shows it.
	json content = {{"@type", "messageAnimatedEmoji"}, {"emoji", "\xF0\x9F\xA7\x90"},
		{"animated_emoji", {{"sticker", {{"emoji", "\xF0\x9F\xA7\x90"}}}}}};
	json message = Msg(42, 1, "");
	message["content"] = content;
	Message parsed = ParseMessage(message);
	CHECK(parsed.media.kind == Media::kSticker && parsed.media.bigEmoji);
	CHECK(parsed.media.emoji == "\xF0\x9F\xA7\x90" && parsed.text.empty());
	CHECK(parsed.media.fileId == 0);
	CHECK(MessagePreview(content) == "\xF0\x9F\xA7\x90");
	// With a still image Haiku can show, that instead (the emoji stays for
	// summaries).
	content["animated_emoji"]["sticker"] = {{"emoji", "\xF0\x9F\xA7\x90"}, {"width", 512},
		{"height", 512}, {"format", {{"@type", "stickerFormatWebp"}}}, {"sticker", File(31)}};
	message["content"] = content;
	parsed = ParseMessage(message);
	CHECK(parsed.media.fileId == 31 && parsed.media.bigEmoji && parsed.media.emoji == "\xF0\x9F\xA7\x90");

	CHECK(MessagePreview({{"@type", "messageDice"}, {"emoji", "\xF0\x9F\x8E\xAF"}, {"value", 6}})
		== "\xF0\x9F\x8E\xAF 6");
	CHECK(MessagePreview({{"@type", "messageChatChangeTitle"}, {"title", "Testers"}})
		== "changed the group name to \"Testers\"");
	CHECK(MessagePreview({{"@type", "messageChatDeleteMember"}}) == "left the group");
}


static void
TestLinks()
{
	// Which links TDLib looks at; the rest go to the browser.
	CHECK(IsTelegramLink("https://t.me/haiku_os"));
	CHECK(IsTelegramLink("http://www.telegram.me/+AbC"));
	CHECK(IsTelegramLink("t.me/joinchat/xyz"));
	CHECK(IsTelegramLink("tg://resolve?domain=durov"));
	CHECK(IsTelegramLink("HTTPS://T.ME"));
	CHECK(!IsTelegramLink("https://t.meadow.com/x"));
	CHECK(!IsTelegramLink("https://example.com/t.me/x"));
	CHECK(!IsTelegramLink("https://www.haiku-os.org"));
	CHECK(!IsTelegramLink("mailto:a@t.me"));

	// What people type in "Open username or link…".
	CHECK(NormalizeLink(" @haiku_os ") == "https://t.me/haiku_os");
	CHECK(NormalizeLink("haiku_os") == "https://t.me/haiku_os");
	CHECK(NormalizeLink("+AbC-d_9") == "https://t.me/+AbC-d_9");
	CHECK(NormalizeLink("t.me/haiku_os/12") == "https://t.me/haiku_os/12");
	CHECK(NormalizeLink("https://t.me/c/123/45") == "https://t.me/c/123/45");
	CHECK(NormalizeLink("tg://resolve?domain=x") == "tg://resolve?domain=x");
	CHECK(NormalizeLink("two words").empty() && NormalizeLink("@").empty()
		&& NormalizeLink("").empty() && NormalizeLink("+").empty());

	LinkAction a = ParseLinkType({{"@type", "internalLinkTypePublicChat"},
		{"chat_username", "haiku_os"}, {"draft_text", ""}, {"open_profile", false}});
	CHECK(a.kind == LinkAction::kPublicChat && a.value == "haiku_os");
	a = ParseLinkType({{"@type", "internalLinkTypeChatInvite"}, {"invite_link", "https://t.me/+AbC"}});
	CHECK(a.kind == LinkAction::kInvite && a.value == "https://t.me/+AbC");
	a = ParseLinkType({{"@type", "internalLinkTypeMessage"}, {"url", "tg://privatepost?channel=1&post=2"}});
	CHECK(a.kind == LinkAction::kMessage && a.value == "tg://privatepost?channel=1&post=2");
	CHECK(ParseLinkType({{"@type", "internalLinkTypeSettings"}}).kind == LinkAction::kBrowser);
	CHECK(MentionedUser("tg://user?id=777") == 777 && MentionedUser("https://t.me/x") == 0);

	// Mentions in messages are links into the app.
	json text = {{"text", "Ask @mira or Jonas"}, {"entities", json::array({
		{{"offset", 4}, {"length", 5}, {"type", {{"@type", "textEntityTypeMention"}}}},
		{{"offset", 13}, {"length", 5}, {"type", {{"@type", "textEntityTypeMentionName"},
			{"user_id", 31}}}}})}};
	std::vector<Span> spans = ParseEntities(text);
	CHECK(spans.size() == 2 && spans[0].style == kLink && spans[0].url == "https://t.me/mira");
	CHECK(spans[1].url == "tg://user?id=31" && spans[1].start == 13);

	// Invite links: the question, and the chat when it can be opened already.
	json info = {{"@type", "chatInviteLinkInfo"}, {"chat_id", 0}, {"accessible_for", 0},
		{"type", {{"@type", "inviteLinkChatTypeSupergroup"}}}, {"title", "Haiku app makers"},
		{"description", ""}, {"member_count", 1204}};
	CHECK(InviteQuestion(info) == "Join the group \xE2\x80\x9CHaiku app makers\xE2\x80\x9D?\n1,204 members");
	CHECK(InviteChat(info) == 0);
	info["chat_id"] = -1005;
	CHECK(InviteChat(info) == -1005);
	info["accessible_for"] = 300;  // a preview for a while: still join first
	CHECK(InviteChat(info) == 0);
	info["type"] = {{"@type", "inviteLinkChatTypeChannel"}};
	info["member_count"] = 1;
	CHECK(InviteQuestion(info) == "Join the channel \xE2\x80\x9CHaiku app makers\xE2\x80\x9D?\n1 subscriber");

	json join = request::JoinByInvite("https://t.me/+AbC", "join");
	CHECK(join["@type"] == "joinChatByInviteLink" && join["invite_link"] == "https://t.me/+AbC");
	CHECK(request::SearchPublicChat("mira", "x")["username"] == "mira");
	CHECK(request::CreatePrivateChat(31, "x")["user_id"] == 31);

	// Joining: offered in groups and channels we aren't in.
	Presence presence;
	json chat = NewChat(-1005, "Haiku app makers", "");
	chat["chat"]["type"] = {{"@type", "chatTypeSupergroup"}, {"supergroup_id", 5}, {"is_channel", false}};
	presence.Apply(chat, 0);
	CHECK(!presence.CanJoin(-1005));  // not known yet: don't offer
	presence.Apply({{"@type", "updateSupergroup"}, {"supergroup", {{"id", 5}, {"member_count", 9},
		{"status", {{"@type", "chatMemberStatusLeft"}}}}}}, 0);
	CHECK(presence.CanJoin(-1005));
	presence.Apply({{"@type", "updateSupergroup"}, {"supergroup", {{"id", 5}, {"member_count", 10},
		{"status", {{"@type", "chatMemberStatusMember"}}}}}}, 0);
	CHECK(!presence.CanJoin(-1005));
	presence.Apply({{"@type", "updateSupergroup"}, {"supergroup", {{"id", 5},
		{"status", {{"@type", "chatMemberStatusCreator"}, {"is_member", false}}}}}}, 0);
	CHECK(presence.CanJoin(-1005));
	CHECK(!presence.CanJoin(42));  // private chats never
}


static void
TestPinned()
{
	// The newest first, the bar's place kept across refreshes, and which
	// updates mean fetching the list again.
	PinnedMessages pinned(42);
	CHECK(pinned.Shown() == nullptr);
	json found = {{"@type", "foundChatMessages"}, {"messages", json::array({
		Msg(42, 5, "Rules"), Msg(42, 9, "Meetup on Friday"), Msg(42, 7, "Wiki")})}};
	for (json& m : found["messages"])
		m["is_pinned"] = true;
	pinned.SetFound(found);
	CHECK(pinned.List().size() == 3 && pinned.Shown()->id == 9 && pinned.Shown()->pinned);
	pinned.Next();
	CHECK(pinned.Shown()->id == 7 && pinned.ShownIndex() == 1);
	pinned.SetFound(found);
	CHECK(pinned.Shown()->id == 7);  // still the same one
	pinned.Next();
	pinned.Next();
	CHECK(pinned.Shown()->id == 9);  // round again
	found["messages"].erase(0);      // 5 unpinned
	pinned.SetFound(found);
	CHECK(pinned.List().size() == 2 && pinned.Shown()->id == 9);

	CHECK(pinned.Affects({{"@type", "updateMessageIsPinned"}, {"chat_id", 42},
		{"message_id", 3}, {"is_pinned", true}}));
	CHECK(!pinned.Affects({{"@type", "updateMessageIsPinned"}, {"chat_id", 43},
		{"message_id", 3}, {"is_pinned", true}}));
	CHECK(pinned.Affects({{"@type", "updateMessageContent"}, {"chat_id", 42}, {"message_id", 9}}));
	CHECK(!pinned.Affects({{"@type", "updateMessageContent"}, {"chat_id", 42}, {"message_id", 8}}));
	CHECK(pinned.Affects({{"@type", "updateDeleteMessages"}, {"chat_id", 42},
		{"message_ids", {3, 7}}, {"is_permanent", true}}));
	CHECK(!pinned.Affects({{"@type", "updateDeleteMessages"}, {"chat_id", 42},
		{"message_ids", {7}}, {"is_permanent", false}, {"from_cache", true}}));

	json pin = request::Pin(42, 9, true);
	CHECK(pin["@type"] == "pinChatMessage" && pin["message_id"] == 9);
	CHECK(request::Pin(42, 9, false)["@type"] == "unpinChatMessage");
	CHECK(request::Pinned(42)["filter"]["@type"] == "searchMessagesFilterPinned");
	CHECK(request::Pinned(42)["@extra"] == "pinned:42");

	// Messages keep their pinned state.
	ChatHistory history(42);
	json m = Msg(42, 9, "Meetup");
	history.AddHistory({{"messages", json::array({m})}});
	CHECK(!history.Find(9)->pinned);
	history.Apply({{"@type", "updateMessageIsPinned"}, {"chat_id", 42}, {"message_id", 9},
		{"is_pinned", true}});
	CHECK(history.Find(9)->pinned);
}


static void
TestVoiceRecording()
{
	// Packing is the inverse of Telegram's unpacking.
	std::string levels;
	for (int i = 0; i < 100; i++)
		levels += (char)(i * 7 % 32);
	std::string packed = PackWaveform(levels);
	CHECK(packed.size() == 63);
	CHECK(UnpackWaveform(packed).substr(0, 100) == levels);
	CHECK(EncodeBase64("Man") == "TWFu" && EncodeBase64("Ma") == "TWE=" && EncodeBase64("M") == "TQ==");
	CHECK(DecodeBase64(EncodeBase64(packed)) == packed);

	// 44.1 kHz stereo to 48 kHz mono, in uneven chunks: the right length,
	// and a sine stays a sine.
	Resampler resampler(44100, 2);
	std::vector<float> in, out;
	for (int i = 0; i < 44100; i++) {
		float v = (float)std::sin(2 * M_PI * 440 * i / 44100.0);
		in.push_back(v);
		in.push_back(v);
	}
	size_t done = 0;
	for (size_t chunk : {1000, 1, 7000, 513, 35586}) {
		resampler.Process(in.data() + done * 2, chunk, out);
		done += chunk;
	}
	CHECK(done == 44100);
	CHECK(out.size() > 47990 && out.size() <= 48000);
	double worst = 0;
	for (size_t i = 0; i < out.size(); i++)
		worst = std::max(worst, std::fabs(out[i] - std::sin(2 * M_PI * 440 * i / 48000.0)));
	CHECK(worst < 0.01);

	// The waveform: quiet then loud gives low bars then full ones; the
	// level is the peak since it was last taken.
	WaveformMeter meter;
	std::vector<float> quiet(48000, 0.05f), loud(48000, -0.5f);
	meter.Add(quiet.data(), quiet.size());
	CHECK(std::fabs(meter.TakeLevel() - 0.05f) < 1e-6 && meter.TakeLevel() == 0);
	meter.Add(loud.data(), loud.size());
	std::string bars = meter.Levels();
	CHECK(bars.size() == 100 && bars[0] == 3 && bars[99] == 31);
	CHECK(std::fabs(meter.Seconds() - 2.0) < 1e-9);
	CHECK(WaveformMeter().Levels().empty());

	json send = request::SendVoice(42, "/tmp/v.ogg", 2, bars, 7);
	const json& note = send["input_message_content"]["voice_note"];
	CHECK(send["input_message_content"]["@type"] == "inputMessageVoiceNote");
	CHECK(note["voice_note"]["path"] == "/tmp/v.ogg" && note["duration"] == 2);
	CHECK(UnpackWaveform(DecodeBase64(note["waveform"].get<std::string>())).substr(0, 100) == bars);
	CHECK(send["reply_to"]["message_id"] == 7);
	CHECK(request::RecordingVoice(42, true)["action"]["@type"] == "chatActionRecordingVoiceNote");
}


static void
TestMentions()
{
	std::string q;
	size_t at = 0;
	CHECK(MentionQuery("hi @mi", 6, &q, &at) && q == "mi" && at == 3);
	CHECK(MentionQuery("@", 1, &q, &at) && q.empty() && at == 0);
	CHECK(MentionQuery("ask @J\xC3\xB6", 8, &q, &at) && q == "J\xC3\xB6");  // Jö
	CHECK(MentionQuery("hi @mira and", 8, &q, &at) && q == "mira");  // cursor mid-text
	CHECK(!MentionQuery("hi @mira and", 12, &q, &at));   // past the word
	CHECK(!MentionQuery("mail me@home", 12, &q, &at));   // an address
	CHECK(!MentionQuery("plain", 5, &q, &at));
	CHECK(!MentionQuery("a@", 2, &q, &at));

	size_t cursor = 0;
	CHECK(InsertMention("hi @mi", 3, 6, "Mira Okafor", "mira_o", &cursor) == "hi @mira_o "
		&& cursor == 11);
	CHECK(InsertMention("hi @an, ok", 3, 6, "Ann", "", &cursor) == "hi Ann , ok" && cursor == 7);
	CHECK(InsertMention("@j later", 0, 2, "Jonas", "", &cursor) == "Jonas later" && cursor == 6);
	CHECK(FirstName("Ann Smith") == "Ann" && FirstName("Bo") == "Bo");

	// Offsets count UTF-16 units: the emoji before "Ann" is two.
	std::string text = "\xF0\x9F\x98\x80 Ann and Bo";
	CHECK(Utf8ToUtf16(text, 4) == 2 && Utf8ToUtf16(text, text.size()) == 13);
	json entities = MentionEntities(text, {{"Ann", 5}, {"Cy", 6}, {"Bo", 7}});
	CHECK(entities.size() == 2);
	CHECK(entities[0]["offset"] == 3 && entities[0]["length"] == 3
		&& entities[0]["type"]["user_id"] == 5);
	CHECK(entities[1]["offset"] == 11 && entities[1]["type"]["@type"] == "textEntityTypeMentionName");
	json send = request::SendText(42, text, 0, entities);
	CHECK(send["input_message_content"]["text"]["entities"].size() == 2);
	CHECK(!request::SendText(42, "plain")["input_message_content"]["text"].contains("entities"));

	json members = {{"@type", "chatMembers"}, {"total_count", 3}, {"members", json::array({
		{{"member_id", {{"@type", "messageSenderUser"}, {"user_id", 5}}}},
		{{"member_id", {{"@type", "messageSenderChat"}, {"chat_id", -9}}}},
		{{"member_id", {{"@type", "messageSenderUser"}, {"user_id", 7}}}}})}};
	CHECK(MemberUserIds(members) == std::vector<int64_t>({5, 7}));
	CHECK(request::SearchMembers(-1, "mi", "m:1")["query"] == "mi");
	CHECK(request::UnreadMentions(-1, "x")["filter"]["@type"] == "searchMessagesFilterUnreadMention");

	// Usernames come with users.
	Names names;
	names.Apply({{"@type", "updateUser"}, {"user", {{"id", 5}, {"first_name", "Mira"},
		{"usernames", {{"active_usernames", {"mira_o", "mira2"}}}}}}});
	names.Apply({{"@type", "updateUser"}, {"user", {{"id", 6}, {"first_name", "Cy"}}}});
	CHECK(names.Username(5) == "mira_o" && names.Username(6).empty() && names.Username(8).empty());
}


static void
TestSendingFiles()
{
	CHECK(IsPlayableVideo("video/mp4", "/a/b") && IsPlayableVideo("", "/a/Clip.MOV"));
	CHECK(!IsPlayableVideo("video/webm", "/a/clip.webm") && !IsPlayableVideo("", "/a/notes.txt"));

	auto file = [](OutgoingFile::Kind kind, const char* path) {
		OutgoingFile f;
		f.kind = kind;
		f.path = path;
		f.width = 640;
		f.height = 480;
		f.duration = kind == OutgoingFile::kVideo ? 12 : 0;
		return f;
	};
	// One photo: a plain message with the caption and the reply.
	std::vector<json> r = request::SendFiles(42, {file(OutgoingFile::kPhoto, "/p1.jpg")}, "Hi", 7);
	CHECK(r.size() == 1 && r[0]["@type"] == "sendMessage");
	CHECK(r[0]["input_message_content"]["caption"]["text"] == "Hi" && r[0]["reply_to"]["message_id"] == 7);

	// Photo, video, photo, a document, then a photo: an album of three (caption
	// on its first), the document alone, the last photo alone; one reply.
	r = request::SendFiles(42, {file(OutgoingFile::kPhoto, "/p1.jpg"),
		file(OutgoingFile::kVideo, "/v.mp4"), file(OutgoingFile::kPhoto, "/p2.jpg"),
		file(OutgoingFile::kDocument, "/d.pdf"), file(OutgoingFile::kPhoto, "/p3.jpg")}, "Trip", 7);
	CHECK(r.size() == 3);
	CHECK(r[0]["@type"] == "sendMessageAlbum" && r[0]["input_message_contents"].size() == 3);
	const json& album = r[0]["input_message_contents"];
	CHECK(album[0]["caption"]["text"] == "Trip" && album[1]["caption"]["text"] == "");
	CHECK(album[1]["@type"] == "inputMessageVideo" && album[1]["video"]["duration"] == 12
		&& album[1]["video"]["supports_streaming"] == true && album[1]["video"]["width"] == 640);
	CHECK(r[0]["reply_to"]["message_id"] == 7 && !r[1].contains("reply_to") && !r[2].contains("reply_to"));
	CHECK(r[1]["input_message_content"]["@type"] == "inputMessageDocument");
	CHECK(r[1]["input_message_content"]["caption"]["text"] == "");
	CHECK(r[2]["@type"] == "sendMessage");

	// Twelve photos: ten, then two.
	std::vector<OutgoingFile> many(12, file(OutgoingFile::kPhoto, "/p.jpg"));
	r = request::SendFiles(42, many, "", 0);
	CHECK(r.size() == 2 && r[0]["input_message_contents"].size() == 10
		&& r[1]["input_message_contents"].size() == 2);
	// Two documents: one by one.
	r = request::SendFiles(42, {file(OutgoingFile::kDocument, "/a"), file(OutgoingFile::kDocument, "/b")}, "x");
	CHECK(r.size() == 2 && r[0]["@type"] == "sendMessage" && r[1]["@type"] == "sendMessage");
}


static void
TestUnreadChats()
{
	ChatList list;
	CHECK(list.UnreadChats() == 0);
	list.Apply(NewChat(1, "Ann", "100"));
	list.Apply(NewChat(2, "Bo", "90"));
	list.Apply(NewChat(3, "Cy", "80"));
	list.Apply({{"@type", "updateChatReadInbox"}, {"chat_id", 1}, {"last_read_inbox_message_id", 0},
		{"unread_count", 4}});
	CHECK(list.UnreadChats() == 1);
	list.Apply({{"@type", "updateChatIsMarkedAsUnread"}, {"chat_id", 2}, {"is_marked_as_unread", true}});
	CHECK(list.UnreadChats() == 2);
	// Muted: not counted, unless someone mentions you.
	list.Apply({{"@type", "updateChatNotificationSettings"}, {"chat_id", 1},
		{"notification_settings", {{"use_default_mute_for", false}, {"mute_for", 1000000}}}});
	CHECK(list.UnreadChats() == 1);
	list.Apply({{"@type", "updateChatUnreadMentionCount"}, {"chat_id", 1}, {"unread_mention_count", 1}});
	CHECK(list.UnreadChats() == 2);
}


static void
TestProfile()
{
	Profile me;
	json user = {{"@type", "user"}, {"id", 77}, {"first_name", "Ian"}, {"last_name", "D"},
		{"usernames", {{"active_usernames", {"ian_d"}}}},
		{"profile_photo", {{"id", "5551234567890"}, {"small", File(40)}, {"big", File(41)},
			{"minithumbnail", {{"data", "AAEC"}}}}}};
	CHECK(me.Apply(user));
	CHECK(me.userId == 77 && me.firstName == "Ian" && me.lastName == "D" && me.username == "ian_d");
	CHECK(me.photoId == 5551234567890LL && me.photoFileId == 41 && me.photoPreview.size() == 3);
	// Someone else's update isn't ours; ours is.
	json other = {{"@type", "updateUser"}, {"user", {{"id", 78}, {"first_name", "Bo"}}}};
	CHECK(!me.Apply(other) && me.firstName == "Ian");
	json renamed = {{"@type", "updateUser"}, {"user", {{"id", 77}, {"first_name", "Ian"},
		{"last_name", ""}}}};
	CHECK(me.Apply(renamed) && me.lastName.empty() && me.photoId == 0 && me.username.empty());
	CHECK(me.Apply({{"@type", "userFullInfo"}, {"bio", {{"text", "Haiku fan"}}}}) && me.bio == "Haiku fan");
	CHECK(me.Apply({{"@type", "updateUserFullInfo"}, {"user_id", 77},
		{"user_full_info", {{"bio", {{"text", "New"}}}}}}) && me.bio == "New");
	CHECK(!me.Apply({{"@type", "updateUserFullInfo"}, {"user_id", 78},
		{"user_full_info", {{"bio", {{"text", "Not mine"}}}}}}) && me.bio == "New");

	CHECK(ProfileProblem("", "x", "") == "Enter a first name.");
	CHECK(ProfileProblem("  ", "", "") == "Enter a first name.");
	CHECK(ProfileProblem("Ian", "", "").empty());
	CHECK(!ProfileProblem(std::string(65, 'a'), "", "").empty());
	std::string bio;
	for (int i = 0; i < 70; i++)
		bio += "\xC3\xA9";   // é: 70 characters, 140 bytes
	CHECK(CharacterCount(bio) == 70 && ProfileProblem("Ian", "", bio).empty());
	CHECK(!ProfileProblem("Ian", "", bio + "x").empty());

	json reply = {{"@type", "sessions"}, {"sessions", json::array({
		{{"id", "11"}, {"is_current", false}, {"application_name", "Telegram Android"},
			{"application_version", "11.2"}, {"device_model", "Pixel 8"}, {"platform", "Android"},
			{"system_version", "14"}, {"location", "Berlin, Germany"}, {"last_active_date", 900}},
		{{"id", "12"}, {"is_current", true}, {"application_name", "Powergram"},
			{"application_version", "0.2.3"}, {"device_model", "Haiku"}, {"platform", "Haiku"},
			{"system_version", "R1/beta6"}, {"last_active_date", 1000}},
		{{"id", "13"}, {"is_current", false}, {"application_name", "Telegram Desktop"},
			{"device_model", "MacBook Pro"}, {"platform", "macOS"}, {"system_version", "14.4"},
			{"last_active_date", 950}, {"is_unconfirmed", true}}})}};
	std::vector<Session> sessions = ParseSessions(reply);
	CHECK(sessions.size() == 3 && sessions[0].current && sessions[0].id == 12);
	CHECK(sessions[0].application == "Powergram 0.2.3" && sessions[0].device == "Haiku, Haiku R1/beta6");
	CHECK(sessions[1].id == 13 && sessions[1].unconfirmed && sessions[1].application == "Telegram Desktop");
	CHECK(sessions[1].device == "MacBook Pro, macOS 14.4");
	CHECK(sessions[2].device == "Pixel 8, Android 14" && sessions[2].location == "Berlin, Germany");

	CHECK(ActiveText(1000, 1100) == "online");
	CHECK(ActiveText(1000, 1000 + 12 * 60) == "12 minutes ago");
	CHECK(ActiveText(1000, 1000 + 3600) == "an hour ago");
	CHECK(ActiveText(1000, 1000 + 5 * 3600) == "5 hours ago");
	CHECK(ActiveText(1000, 1000 + 30 * 3600) == "yesterday");
	CHECK(ActiveText(1000, 1000 + 5 * 86400).empty());

	CHECK(request::SetName("Ian", "", "p")["@type"] == "setName");
	CHECK(request::SetProfilePhoto("/a.jpg", "p")["photo"]["photo"]["path"] == "/a.jpg");
	CHECK(request::DeleteProfilePhoto(5551234567890LL, "p")["profile_photo_id"] == "5551234567890");
	CHECK(request::TerminateSession(13, "p")["session_id"] == "13");
	CHECK(request::TerminateOtherSessions("p")["@type"] == "terminateAllOtherSessions");
}


static void
TestAvailableReactions()
{
	auto emoji = [](const char* e, bool premium = false) {
		return json{{"type", {{"@type", "reactionTypeEmoji"}, {"emoji", e}}}, {"needs_premium", premium}};
	};
	json available = {{"@type", "availableReactions"},
		{"top_reactions", json::array({emoji("\xF0\x9F\x91\x8D"), emoji("\xE2\x9D\xA4"),
			{{"type", {{"@type", "reactionTypeCustomEmoji"}, {"custom_emoji_id", "5"}}}}})},
		{"recent_reactions", json::array({emoji("\xE2\x9D\xA4"), emoji("\xF0\x9F\x94\xA5", true)})},
		{"popular_reactions", json::array({emoji("\xF0\x9F\x98\x82")})}};
	std::vector<std::string> list = ParseAvailableReactions(available);
	// Telegram's heart is the bare U+2764, as it gives it.
	CHECK(list == std::vector<std::string>({"\xF0\x9F\x91\x8D", "\xE2\x9D\xA4", "\xF0\x9F\x98\x82"}));
	CHECK(ParseAvailableReactions(available, 2).size() == 2);
	CHECK(ParseAvailableReactions({{"@type", "availableReactions"}, {"top_reactions", json::array()}}).empty());
	CHECK(request::AvailableReactions(1, 2, "x")["@type"] == "getMessageAvailableReactions");
}


static void
TestHaikuIcons()
{
	// An HVIF file is drawn; other documents get a card.
	json message = Msg(42, 1, "");
	message["content"] = {{"@type", "messageDocument"}, {"document", {{"file_name", "Plane.HVIF"},
		{"mime_type", "application/octet-stream"}, {"document", File(55)}}}, {"caption", {{"text", ""}}}};
	Message parsed = ParseMessage(message);
	CHECK(parsed.media.kind == Media::kIcon && parsed.media.fileId == 55 && parsed.media.fullFileId == 55);
	CHECK(parsed.text.empty() && parsed.media.fileName == "Plane.HVIF");
	CHECK(IsHaikuIcon({{"file_name", "icon"}, {"mime_type", "image/x-hvif"}}));
	CHECK(!IsHaikuIcon({{"file_name", "notes.pdf"}, {"mime_type", "application/pdf"}}));
}


int
main()
{
	TestAuthFlow();
	TestChatList();
	TestMissingFields();
	TestSendQueue();
	TestChatHistory();
	TestRepliesAndForwards();
	TestNotifications();
	TestMedia();
	TestVideosAvatarsSending();
	TestPhotosAndStickers();
	TestFormatting();
	TestPresence();
	TestDrafts();
	TestUnreadAndNewer();
	TestLists();
	TestPollsPlaces();
	TestVoice();
	TestChatInfo();
	TestEmojiMessages();
	TestHaikuIcons();
	TestLinks();
	TestPinned();
	TestVoiceRecording();
	TestMentions();
	TestSendingFiles();
	TestUnreadChats();
	TestProfile();
	TestAvailableReactions();
	if (sFailures) {
		std::fprintf(stderr, "%d check(s) failed\n", sFailures);
		return EXIT_FAILURE;
	}
	std::puts("all core tests passed");
	return EXIT_SUCCESS;
}
