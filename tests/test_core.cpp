// SPDX-License-Identifier: GPL-3.0-or-later
// Host tests for the portable core: `make test`.
#include <cstdio>
#include <cstdlib>

#include "../src/core/AuthFlow.h"
#include "../src/core/ChatHistory.h"
#include "../src/core/ChatList.h"
#include "../src/core/Files.h"
#include "../src/core/Format.h"
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
	CHECK(!list.Apply({{"@type", "updateChatPosition"}, {"chat_id", 2},
		{"position", {{"list", {{"@type", "chatListArchive"}}}, {"order", "0"}}}}));
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

	message["content"] = {{"@type", "messageDocument"}, {"document", {{"file_name", "notes.pdf"}}},
		{"caption", {{"text", "Read this"}}}};
	m = ParseMessage(message);
	CHECK(m.media.kind == Media::kNone && m.text == "Document: notes.pdf\nRead this");

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
		{{"offset", 0}, {"length", 2}, {"type", {{"@type", "textEntityTypeMentionName"}}}}})}};
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
	if (sFailures) {
		std::fprintf(stderr, "%d check(s) failed\n", sFailures);
		return EXIT_FAILURE;
	}
	std::puts("all core tests passed");
	return EXIT_SUCCESS;
}
