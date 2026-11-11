// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <DataIO.h>
#include <FilePanel.h>
#include <TranslationUtils.h>
#include <Url.h>
#include <Clipboard.h>
#include <GroupView.h>
#include <Invoker.h>
#include <MenuField.h>
#include <MessageFilter.h>
#include <PopUpMenu.h>
#include <Button.h>
#include <StringView.h>
#include <TextControl.h>
#include <DateFormat.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <ScrollView.h>
#include <String.h>
#include <TimeFormat.h>

#include <algorithm>
#include <cmath>
#include <ctime>

#include "../core/ChatList.h"
#include "BitmapUtils.h"
#include "ChatPicker.h"
#include "Icons.h"
#include "Pickers.h"
#include "MessageView.h"
#include "Messages.h"

namespace {

enum {
	kMsgSelectionChanged = 'slch',
	kMsgSend = 'send',
	kMsgEntryModified = 'entm',
	kMsgReply = 'rply',
	kMsgCopy = 'copy',
	kMsgEdit = 'edit',
	kMsgDeleteAsk = 'dask',
	kMsgDeleteAnswer = 'dans',
	kMsgCancelCompose = 'ccmp',
	kMsgAttach = 'atch',
	kMsgShowEmoji = 'shem',
	kMsgChatMenu = 'cmnu',
	kMsgListChosen = 'lcho',
	kMsgForward = 'frwd',
	kMsgChatMenuItem = 'cmit',
	kMsgChatDeleteAnswer = 'cdan',
	kMsgShowStickers = 'shst',
	kMsgFilesChosen = 'fchs'
};

// Telegram's avatar colours (Paper Plane's avatar.rs picks by id the same way).
const rgb_color kAvatarColors[] = {
	{0xe1, 0x70, 0x76, 255}, {0xfa, 0xa7, 0x74, 255}, {0xa6, 0x95, 0xe7, 255},
	{0x7b, 0xc8, 0x62, 255}, {0x6e, 0xc9, 0xcb, 255}, {0x65, 0xaa, 0xdd, 255},
	{0xee, 0x7a, 0xae, 255}
};


BString
FormatDate(int64 date)
{
	BString out;
	if (date <= 0)
		return out;
	time_t when = (time_t)date;
	time_t now = time(nullptr);
	struct tm whenTm, nowTm;
	localtime_r(&when, &whenTm);
	localtime_r(&now, &nowTm);

	if (whenTm.tm_year == nowTm.tm_year && whenTm.tm_yday == nowTm.tm_yday)
		BTimeFormat().Format(out, when, B_SHORT_TIME_FORMAT);
	else if (now - when < 6 * 24 * 3600) {
		char day[16];
		strftime(day, sizeof(day), "%a", &whenTm);
		out = day;
	} else
		BDateFormat().Format(out, when, B_SHORT_DATE_FORMAT);
	return out;
}


// One chat row, drawn like Paper Plane's sidebar row: avatar, title and
// date on the first line, last message and unread badge on the second.
class ChatItem : public BListItem {
public:
	ChatItem(const BMessage* list, int32 index, BBitmap* avatar)
		:
		fAvatar(avatar)
	{
		fId = list->GetInt64("id", index, 0);
		fTitle = list->GetString("title", index, "");
		fText = list->GetString("text", index, "");
		fSender = list->GetString("sender", index, "");
		fDate = FormatDate(list->GetInt64("date", index, 0));
		fUnread = list->GetInt32("unread", index, 0);
		fMentions = list->GetInt32("mentions", index, 0);
		fPinned = list->GetBool("pinned", index, false);
		fTyping = list->GetString("typing", index, "");
		fDraft = list->GetString("draft", index, "");
		fMuted = list->GetBool("muted", index, false);
		fMarkedUnread = list->GetBool("marked_unread", index, false);
		fKind = list->GetInt32("kind", index, 0);
		fInitials = pp::Initials(fTitle.String()).c_str();
	}

	int64 Id() const { return fId; }
	const char* Title() const { return fTitle.String(); }
	int32 Unread() const { return fUnread; }

	void Update(BView* owner, const BFont* font) override
	{
		BListItem::Update(owner, font);
		font_height fh;
		font->GetHeight(&fh);
		float line = ceilf(fh.ascent + fh.descent + fh.leading);
		// Paper Plane: 62 px rows with a 48 px avatar for 18 px lines.
		SetHeight(ceilf(line * 3.45f));
	}

	void DrawItem(BView* owner, BRect frame, bool) override
	{
		rgb_color bg = ui_color(IsSelected()
			? B_LIST_SELECTED_BACKGROUND_COLOR : B_LIST_BACKGROUND_COLOR);
		rgb_color fg = ui_color(IsSelected()
			? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
		rgb_color dim = mix_color(fg, bg, 110);
		owner->SetLowColor(bg);
		owner->SetHighColor(bg);
		owner->FillRect(frame);

		BFont plain(be_plain_font);
		BFont bold(be_bold_font);
		font_height fh;
		plain.GetHeight(&fh);
		float line = ceilf(fh.ascent + fh.descent + fh.leading);
		float pad = roundf(line * 0.67f);
		float avatar = roundf(frame.Height() * 0.77f);

		// Avatar: a coloured circle with initials.
		BRect circle(frame.left + pad, frame.top + (frame.Height() - avatar) / 2,
			frame.left + pad + avatar, frame.top + (frame.Height() + avatar) / 2);
		owner->SetDrawingMode(B_OP_ALPHA);
		if (fAvatar != nullptr) {
			owner->DrawBitmap(fAvatar, fAvatar->Bounds(), circle, B_FILTER_BITMAP_BILINEAR);
		} else {
		owner->SetHighColor(kAvatarColors[(uint64)(fId < 0 ? -fId : fId) % 7]);
		owner->FillEllipse(circle);
		BFont initials(be_bold_font);
		initials.SetSize(avatar * 0.36f);
		owner->SetFont(&initials);
		font_height ih;
		initials.GetHeight(&ih);
		owner->SetHighColor(255, 255, 255);
		owner->SetLowColor(kAvatarColors[(uint64)(fId < 0 ? -fId : fId) % 7]);
		float iw = initials.StringWidth(fInitials);
		owner->DrawString(fInitials, BPoint(circle.left + (avatar - iw) / 2,
			circle.top + (avatar + ih.ascent - ih.descent) / 2));
		}
		owner->SetLowColor(bg);

		float left = circle.right + pad;
		float right = frame.right - pad;
		float mid = frame.top + frame.Height() / 2;

		// First line: title, date on the right.
		owner->SetFont(&plain);
		float dateWidth = plain.StringWidth(fDate);
		owner->SetHighColor(dim);
		owner->DrawString(fDate, BPoint(right - dateWidth, mid - fh.descent - 2));

		owner->SetFont(&bold);
		owner->SetHighColor(fg);
		BString title(fTitle);
		bold.TruncateString(&title, B_TRUNCATE_END, right - dateWidth - pad - left);
		owner->DrawString(title, BPoint(left, mid - fh.descent - 2));

		// Second line: "You: text", unread badge or pin on the right.
		float badgeRight = right;
		BString badge;
		if (fUnread > 0)
			badge << fUnread;
		if (fMentions > 0)
			badge = "@";
		if (badge.IsEmpty() && fMarkedUnread)
			badge = " ";  // marked as unread: a dot
		float textRight = right;
		if (!badge.IsEmpty()) {
			float h = line + 2;
			float w = std::max(h, plain.StringWidth(badge) + line * 0.7f);
			BRect pill(badgeRight - w, mid + 3, badgeRight, mid + 3 + h);
			// Muted chats count in grey, as in Telegram.
			rgb_color accent = fMuted ? mix_color(fg, bg, 110) : ui_color(B_CONTROL_HIGHLIGHT_COLOR);
			if (badge == " ")
				pill.left = pill.right - h;
			owner->SetHighColor(accent);
			owner->FillRoundRect(pill, h / 2, h / 2);
			owner->SetHighColor(255, 255, 255);
			owner->SetLowColor(accent);
			owner->DrawString(badge, BPoint(pill.left + (w - plain.StringWidth(badge)) / 2,
				pill.top + (h + fh.ascent - fh.descent) / 2));
			owner->SetLowColor(bg);
			textRight = pill.left - pad / 2;
		} else if (fPinned) {
			// A small pin mark, as Paper Plane shows for pinned chats.
			float s = line * 0.5f;
			BPoint c(right - s, mid + 3 + line / 2);
			owner->SetHighColor(dim);
			owner->FillEllipse(BRect(c.x - s / 2, c.y - s / 2 - 2, c.x + s / 2, c.y + s / 2 - 2));
			owner->StrokeLine(BPoint(c.x, c.y), BPoint(c.x, c.y + s));
			textRight = right - s * 2 - pad / 2;
		}

		BString text;
		float draftLabel = 0;
		if (!fTyping.IsEmpty())
			text = fTyping;
		else if (!fDraft.IsEmpty()) {
			// "Draft:" in red, then the text, as Telegram shows it.
			const char* label = "Draft: ";
			draftLabel = plain.StringWidth(label);
			owner->SetFont(&plain);
			owner->SetHighColor(ui_color(B_FAILURE_COLOR));
			owner->DrawString(label, BPoint(left, mid + 3 + fh.ascent));
			text = fDraft;
			text.ReplaceAll('\n', ' ');
		} else {
			if (!fSender.IsEmpty())
				text << fSender << ": ";
			text << fText;
		}
		plain.TruncateString(&text, B_TRUNCATE_END, textRight - left - draftLabel);
		owner->SetFont(&plain);
		owner->SetHighColor(fTyping.IsEmpty() ? dim : ui_color(B_CONTROL_HIGHLIGHT_COLOR));
		owner->DrawString(text, BPoint(left + draftLabel, mid + 3 + fh.ascent));
	}

private:
	int64 fId;
	BString fTitle, fText, fSender, fDate, fInitials;
	int32 fUnread, fMentions;
	bool fPinned;
	BString fTyping;    // "typing…" instead of the last message
	BString fDraft;     // "Draft: …" instead of the last message
	bool fMuted = false;
	bool fMarkedUnread = false;
	int32 fKind = 0;    // pp::Chat::Kind: 0 private, 1 group, 2 channel
public:
	bool Muted() const { return fMuted; }
	bool Pinned() const { return fPinned; }
	bool HasUnread() const { return fUnread > 0 || fMarkedUnread; }
	int32 Kind() const { return fKind; }
private:
	BBitmap* fAvatar;   // round chat photo (owned by the window), or null
};

// The chat list: a right click opens the chat's menu.
class ChatListView : public BListView {
public:
	ChatListView() : BListView("chats") {}

	void MouseDown(BPoint where) override
	{
		uint32 buttons = 0;
		if (BMessage* current = Window()->CurrentMessage())
			current->FindInt32("buttons", (int32*)&buttons);
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0) {
			BListView::MouseDown(where);
			return;
		}
		int32 index = IndexOf(where);
		if (index < 0)
			return;
		BMessage menu(kMsgChatMenu);
		menu.AddInt32("index", index);
		menu.AddPoint("where", ConvertToScreen(where));
		Window()->PostMessage(&menu);
	}
};


class EscapeFilter : public BMessageFilter {
public:
	EscapeFilter() : BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_KEY_DOWN) {}

	filter_result Filter(BMessage* message, BHandler**) override
	{
		const char* bytes;
		if (message->FindString("bytes", &bytes) == B_OK && bytes[0] == B_ESCAPE
			&& (message->GetInt32("modifiers", 0)
				& (B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY | B_SHIFT_KEY)) == 0) {
			Looper()->PostMessage(kMsgCancelCompose);
			return B_SKIP_MESSAGE;
		}
		return B_DISPATCH_MESSAGE;
	}
};

} // namespace


MainWindow::MainWindow(BRect frame, bool testDc,
	const std::vector<std::string>& recentEmoji)
	:
	BWindow(frame, testDc ? APP_NAME " (test servers)" : APP_NAME,
		B_DOCUMENT_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	fEmojiWindow(nullptr),
	fStickerWindow(nullptr),
	fRecentEmoji(recentEmoji),
	fFilePanel(nullptr),
	fOpenChat(0),
	fReplyTo(0),
	fEditing(0)
{
	BMenuBar* bar = new BMenuBar("menu");
	BMenu* app = new BMenu(APP_NAME);
	app->AddItem(new BMenuItem("About " APP_NAME B_UTF8_ELLIPSIS,
		new BMessage(B_ABOUT_REQUESTED)));
	app->AddSeparatorItem();
	app->AddItem(new BMenuItem("Log out", new BMessage(kMsgLogOut)));
	app->AddSeparatorItem();
	app->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
	app->SetTargetForItems(be_app);
	bar->AddItem(app);

	fList = new ChatListView();
	fList->SetSelectionMessage(new BMessage(kMsgSelectionChanged));
	BScrollView* listScroll = new BScrollView("chat scroll", fList, 0, false,
		true, B_NO_BORDER);
	fListMenu = new BPopUpMenu("lists");
	BMessage* all = new BMessage(kMsgListChosen);
	all->AddString("key", "main");
	fListMenu->AddItem(new BMenuItem("All chats", all));
	fListMenu->ItemAt(0)->SetMarked(true);
	fListField = new BMenuField("list field", nullptr, fListMenu);

	fTitle = new BStringView("title", "");
	fTitle->SetFont(be_bold_font);
	fTitle->SetTruncation(B_TRUNCATE_END);
	fSubtitle = new BStringView("subtitle", "");
	fSubtitle->SetTruncation(B_TRUNCATE_END);
	fSubtitle->SetExplicitMinSize(BSize(be_plain_font->StringWidth("m") * 6, B_SIZE_UNSET));
	fSubtitle->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fSubtitle->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
	fTitle->SetExplicitMinSize(BSize(be_plain_font->StringWidth("m") * 6, B_SIZE_UNSET));
	fTitle->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fMessages = new MessageView();
	fMessages->SetEmptyText("Select a chat to start messaging");
	BScrollView* messageScroll = new BScrollView("message scroll", fMessages,
		0, false, true, B_NO_BORDER);

	fEntry = new BTextControl("entry", nullptr, "", new BMessage(kMsgSend));
	fEntry->SetModificationMessage(new BMessage(kMsgEntryModified));
	fSendButton = new BButton("send", "Send", new BMessage(kMsgSend));
	// Icon buttons with tooltips, like Paper Plane's entry row.
	fAttachButton = _IconButton("attach", kIconAttach, kMsgAttach, "Attach files");
	fEmojiButton = _IconButton("emoji", kIconEmoji, kMsgShowEmoji, "Emoji");
	fStickerButton = _IconButton("stickers", kIconSticker, kMsgShowStickers, "Stickers");
	fEntry->SetEnabled(false);
	fSendButton->SetEnabled(false);

	fComposeLabel = new BStringView("compose label", "");
	// A long message being replied to is cut with "…", not allowed to
	// widen the layout (it squeezed the chat list and grew the window).
	fComposeLabel->SetTruncation(B_TRUNCATE_END);
	fComposeLabel->SetExplicitMinSize(BSize(be_plain_font->StringWidth("m") * 6, B_SIZE_UNSET));
	fComposeLabel->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fComposeBar = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(static_cast<BGroupView*>(fComposeBar))
		.SetInsets(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING, B_USE_SMALL_SPACING, 0)
		.Add(fComposeLabel, 1.0f)
		.Add(new BButton("cancel compose", "Cancel", new BMessage(kMsgCancelCompose)));
	fComposeBar->Hide();
	// Escape cancels a reply or an edit (window shortcuts always need Command).
	AddCommonFilter(new EscapeFilter());

	// Paper Plane: a 365 px sidebar next to the chat, for 14.6 px text.
	float em = be_plain_font->Size();
	listScroll->SetExplicitPreferredSize(BSize(roundf(em * 25), B_SIZE_UNSET));
	// Minimum heights too: without them the bar over the entry made the
	// layout's minimum taller than the window, which then grew.
	listScroll->SetExplicitMinSize(BSize(roundf(em * 16), roundf(em * 8)));
	messageScroll->SetExplicitMinSize(BSize(roundf(em * 15), roundf(em * 8)));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(bar)
		.AddSplit(B_HORIZONTAL, 0, 1.0f)
			.AddGroup(B_VERTICAL, 0, 0.4f)
				.AddGroup(B_HORIZONTAL)
					.SetInsets(B_USE_SMALL_SPACING)
					.Add(fListField)
				.End()
				.Add(listScroll, 1.0f)
			.End()
			.AddGroup(B_VERTICAL, 0, 0.6f)
				.AddGroup(B_VERTICAL, 0)
					.SetInsets(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING,
						B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
					.Add(fTitle)
					.Add(fSubtitle)
				.End()
				.Add(messageScroll, 1.0f)
				.Add(fComposeBar)
				.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
					.SetInsets(B_USE_SMALL_SPACING)
					.Add(fAttachButton)
					.Add(fEntry, 1.0f)
					.Add(fEmojiButton)
					.Add(fStickerButton)
					.Add(fSendButton)
				.End()
			.End()
		.End();

	SetSizeLimits(em * 40, B_SIZE_UNLIMITED, em * 20, B_SIZE_UNLIMITED);
}


void
MainWindow::_UpdateChats(const BMessage* list)
{
	int64 selected = -1;
	if (ChatItem* item = dynamic_cast<ChatItem*>(fList->ItemAt(fList->CurrentSelection())))
		selected = item->Id();

	while (BListItem* item = fList->RemoveItem((int32)0))
		delete item;

	int32 count = 0;
	list->GetInfo("id", nullptr, &count);
	BList items(count);
	for (int32 i = 0; i < count; i++)
		items.AddItem(new ChatItem(list, i, _Avatar(list, i)));
	fList->AddList(&items);

	for (int32 i = 0; i < count; i++) {
		if (static_cast<ChatItem*>(fList->ItemAt(i))->Id() == selected)
			fList->Select(i);
		// The open chat's unread count, for the jump button's badge.
		if (list->GetInt64("id", i, 0) == fOpenChat)
			fMessages->SetUnreadCount(list->GetInt32("unread", i, 0));
	}
}


void
MainWindow::_SelectionChanged()
{
	int64 id = 0;
	if (ChatItem* item = dynamic_cast<ChatItem*>(fList->ItemAt(fList->CurrentSelection())))
		id = item->Id();
	if (id == fOpenChat)
		return;
	_SaveDraft();
	fOpenChat = id;
	fTitle->SetText(id != 0 ? static_cast<ChatItem*>(
		fList->ItemAt(fList->CurrentSelection()))->Title() : "");
	fSubtitle->SetText("");
	ChatItem* opened = dynamic_cast<ChatItem*>(fList->ItemAt(fList->CurrentSelection()));
	fMessages->SetUnreadCount(opened != nullptr ? opened->Unread() : 0);
	fMessages->SetMoreBelow(false);
	fMessages->SetMessages({}, true);
	fMessages->SetEmptyText(id != 0 ? "Loading messages…"
		: "Select a chat to start messaging");
	_CancelCompose();
	fEntry->SetText("");
	fEntry->SetEnabled(id != 0);
	fSendButton->SetEnabled(id != 0);
	fAttachButton->SetEnabled(id != 0);
	fEmojiButton->SetEnabled(id != 0);
	fStickerButton->SetEnabled(id != 0);
	if (id != 0)
		fEntry->MakeFocus(true);

	BMessage open(kMsgOpenChat);
	open.AddInt64("id", id);
	be_app->PostMessage(&open);
}


void
MainWindow::_UpdateHistory(const BMessage* history)
{
	if (history->GetInt64("chat_id", 0) != fOpenChat)
		return;
	std::vector<MessageRow> rows;
	int32 count = 0;
	history->GetInfo("id", nullptr, &count);
	for (int32 i = 0; i < count; i++) {
		MessageRow row;
		row.id = history->GetInt64("id", i, 0);
		row.sender = history->GetString("sender", i, "");
		row.text = history->GetString("text", i, "");
		row.time = history->GetString("time", i, "");
		row.outgoing = history->GetBool("outgoing", i, false);
		row.read = history->GetBool("read", i, false);
		row.pending = history->GetBool("pending", i, false);
		row.failed = history->GetBool("failed", i, false);
		row.edited = history->GetBool("edited", i, false);
		row.service = history->GetBool("service", i, false);
		row.forward = history->GetString("forward", i, "");
		row.replySender = history->GetString("reply_sender", i, "");
		row.replyText = history->GetString("reply_text", i, "");
		row.mediaKind = history->GetInt32("media_kind", i, 0);
		row.imagePath = history->GetString("image", i, "");
		row.minithumbnail = FindBytes(history, "mini", i);
		row.mediaWidth = history->GetInt32("media_w", i, 0);
		row.mediaHeight = history->GetInt32("media_h", i, 0);
		row.spoiler = history->GetBool("spoiler", i, false);
		row.secret = history->GetBool("secret", i, false);
		row.emoji = history->GetString("emoji", i, "");
		row.duration = history->GetInt32("duration", i, 0);
		row.showAvatar = history->GetBool("show_avatar", i, false);
		row.senderKey = history->GetInt64("sender_key", i, 0);
		row.avatarPath = history->GetString("avatar", i, "");
		row.avatarPreview = FindBytes(history, "avatar_mini", i);
		row.avatarInitials = history->GetString("initials", i, "");
		row.spans = pp::DecodeSpans(history->GetString("spans", i, ""));
		rows.push_back(row);
	}
	fMessages->SetEmptyText("No messages here yet");
	fMessages->SetMoreBelow(history->GetBool("more_below", false));
	fMessages->SetMessages(rows, history->GetBool("scroll_end", false),
		history->GetInt64("scroll_to", 0));
}


void
MainWindow::_Send()
{
	BString text(fEntry->Text());
	if (fOpenChat == 0 || text.Trim().IsEmpty())
		return;
	if (fEditing != 0) {
		BMessage edit(kMsgEditText);
		edit.AddInt64("chat_id", fOpenChat);
		edit.AddInt64("message_id", fEditing);
		edit.AddString("text", fEntry->Text());
		be_app->PostMessage(&edit);
	} else {
		BMessage send(kMsgSendText);
		send.AddInt64("chat_id", fOpenChat);
		send.AddString("text", fEntry->Text());
		send.AddInt64("reply_to", fReplyTo);
		be_app->PostMessage(&send);
	}
	_CancelCompose();
	fEntry->SetText("");
}


void
MainWindow::_ShowMessageMenu(const BMessage* properties)
{
	if (properties->GetInt64("chat_id", 0) != fOpenChat)
		return;
	int64 id = properties->GetInt64("message_id", 0);
	const MessageRow* row = fMessages->Row(id);
	if (row == nullptr)
		return;

	BPopUpMenu* menu = new BPopUpMenu("message", false, false);
	auto item = [&](const char* label, uint32 what, bool enabled) {
		BMessage* message = new BMessage(what);
		message->AddInt64("message_id", id);
		BMenuItem* menuItem = new BMenuItem(label, message);
		menuItem->SetEnabled(enabled);
		menu->AddItem(menuItem);
		return message;
	};
	item("Reply", kMsgReply, properties->GetBool("can_reply", false));
	item("Copy", kMsgCopy, properties->GetBool("can_copy", true) && !row->text.IsEmpty());
	item("Forward" B_UTF8_ELLIPSIS, kMsgForward, properties->GetBool("can_forward", false));
	bool canSelf = properties->GetBool("can_delete_self", false);
	bool canAll = properties->GetBool("can_delete_all", false);
	if (properties->GetBool("can_edit", false)) {
		menu->AddSeparatorItem();
		item("Edit", kMsgEdit, true);
	}
	if (canSelf || canAll) {
		if (!properties->GetBool("can_edit", false))
			menu->AddSeparatorItem();
		BMessage* del = item("Delete" B_UTF8_ELLIPSIS, kMsgDeleteAsk, true);
		del->AddBool("can_self", canSelf);
		del->AddBool("can_all", canAll);
	}
	menu->SetTargetForItems(this);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(fMenuWhere, true, false, true);
}


void
MainWindow::_StartReply(int64 messageId)
{
	const MessageRow* row = fMessages->Row(messageId);
	if (row == nullptr)
		return;
	_CancelCompose();
	fReplyTo = messageId;
	BString label("Reply to ");
	label << (row->outgoing ? BString("your message") : row->sender.IsEmpty()
		? BString(fTitle->Text()) : row->sender);
	label << ": " << row->text;
	label.ReplaceAll('\n', ' ');
	fComposeLabel->SetText(label);
	fComposeBar->Show();
	fEntry->MakeFocus(true);
}


void
MainWindow::_StartEdit(int64 messageId)
{
	const MessageRow* row = fMessages->Row(messageId);
	if (row == nullptr)
		return;
	_CancelCompose();
	fEditing = messageId;
	fComposeLabel->SetText("Edit message");
	fComposeBar->Show();
	fSendButton->SetLabel("Save");
	fEntry->SetText(row->text);
	fEntry->MakeFocus(true);
	fEntry->TextView()->SelectAll();
}


void
MainWindow::_CancelCompose()
{
	bool wasEditing = fEditing != 0;
	fReplyTo = 0;
	fEditing = 0;
	if (!fComposeBar->IsHidden())
		fComposeBar->Hide();
	fSendButton->SetLabel("Send");
	if (wasEditing)
		fEntry->SetText("");
}


void
MainWindow::_AskDelete(int64 messageId, bool canSelf, bool canAll)
{
	BAlert* alert;
	if (canSelf && canAll) {
		alert = new BAlert("Delete", "Delete this message? This cannot be undone.",
			"Delete for everyone", "Delete for me", "Cancel",
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	} else {
		alert = new BAlert("Delete", canAll
			? "Delete this message for everyone? This cannot be undone."
			: "Delete this message for you? Others in the chat still see it.",
			"Delete", "Cancel", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	}
	alert->SetShortcut(alert->CountButtons() - 1, B_ESCAPE);
	BMessage* answer = new BMessage(kMsgDeleteAnswer);
	answer->AddInt64("chat_id", fOpenChat);
	answer->AddInt64("message_id", messageId);
	answer->AddBool("can_self", canSelf);
	answer->AddBool("can_all", canAll);
	alert->Go(new BInvoker(answer, this));
}


void
MainWindow::_SendFiles(const BMessage* message)
{
	if (fOpenChat == 0 || !message->HasRef("refs"))
		return;
	BMessage send(kMsgSendFiles);
	send.AddInt64("chat_id", fOpenChat);
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++)
		send.AddRef("refs", &ref);
	// Text in the entry becomes the caption, a pending reply applies.
	send.AddString("caption", fEntry->Text());
	send.AddInt64("reply_to", fReplyTo);
	be_app->PostMessage(&send);
	_CancelCompose();
	fEntry->SetText("");
}


BBitmap*
MainWindow::_Avatar(const BMessage* list, int32 index)
{
	// Decoded once and cut round; the small photo replaces its preview.
	BString path = list->GetString("photo", index, "");
	int64 id = list->GetInt64("id", index, 0);
	std::string key = !path.IsEmpty() ? std::string(path.String())
		: "mini:" + std::to_string(id);
	auto it = fAvatars.find(key);
	if (it != fAvatars.end())
		return it->second;

	BBitmap* square = nullptr;
	if (!path.IsEmpty())
		square = BTranslationUtils::GetBitmap(path.String());
	else {
		std::string preview = FindBytes(list, "photo_mini", index);
		if (!preview.empty()) {
			BMemoryIO io(preview.data(), preview.size());
			square = BTranslationUtils::GetBitmap(&io);
		}
	}
	BBitmap* round = RoundBitmap(square);
	delete square;
	if (round != nullptr)
		fAvatars[key] = round;
	return round;
}


BButton*
MainWindow::_IconButton(const char* name, int kind, uint32 what, const char* tip)
{
	BButton* button = new BButton(name, nullptr, new BMessage(what));
	font_height fh;
	be_plain_font->GetHeight(&fh);
	float size = roundf((fh.ascent + fh.descent) * 1.3f);
	BBitmap* icon = MakeIcon((IconKind)kind, size, ui_color(B_CONTROL_TEXT_COLOR));
	button->SetIcon(icon);
	delete icon;  // the button keeps its own copy
	button->SetToolTip(tip);
	button->SetEnabled(false);
	return button;
}


void
MainWindow::_InsertEmoji(const char* emoji)
{
	if (fOpenChat == 0 || emoji[0] == '\0')
		return;
	BTextView* text = fEntry->TextView();
	int32 start, end;
	text->GetSelection(&start, &end);
	text->Delete(start, end);
	text->Insert(start, emoji, strlen(emoji));
	text->Select(start + strlen(emoji), start + strlen(emoji));

	// Most recent first, each once, at most three rows of the picker.
	auto it = std::find(fRecentEmoji.begin(), fRecentEmoji.end(), emoji);
	if (it != fRecentEmoji.end())
		fRecentEmoji.erase(it);
	fRecentEmoji.insert(fRecentEmoji.begin(), emoji);
	if (fRecentEmoji.size() > 27)
		fRecentEmoji.resize(27);
	BMessage used(kMsgEmojiUsed);
	used.AddString("emoji", emoji);
	be_app->PostMessage(&used);
}


void
MainWindow::_SaveDraft()
{
	// What's typed but not sent stays with the chat (and syncs to the
	// user's other devices). Text being edited isn't a draft.
	if (fOpenChat == 0 || fEditing != 0)
		return;
	BMessage save(kMsgSaveDraft);
	save.AddInt64("chat_id", fOpenChat);
	save.AddString("text", fEntry->Text());
	be_app->PostMessage(&save);
}


void
MainWindow::_ShowChatMenu(int32 index, BPoint where)
{
	ChatItem* item = dynamic_cast<ChatItem*>(fList->ItemAt(index));
	if (item == nullptr)
		return;
	BPopUpMenu* menu = new BPopUpMenu("chat", false, false);
	auto add = [&](const char* label, const char* action) {
		BMessage* message = new BMessage(kMsgChatMenuItem);
		message->AddInt64("chat_id", item->Id());
		message->AddString("action", action);
		message->AddString("title", item->Title());
		message->AddInt32("kind", item->Kind());
		menu->AddItem(new BMenuItem(label, message));
	};
	if (item->HasUnread())
		add("Mark as read", "read");
	else
		add("Mark as unread", "unread");
	add(item->Muted() ? "Unmute" : "Mute", item->Muted() ? "unmute" : "mute");
	add(item->Pinned() ? "Unpin" : "Pin", item->Pinned() ? "unpin" : "pin");
	add(fShownList == "archive" ? "Unarchive" : "Archive",
		fShownList == "archive" ? "unarchive" : "archive");
	menu->AddSeparatorItem();
	add(item->Kind() == 0 ? "Delete chat" B_UTF8_ELLIPSIS
		: item->Kind() == 2 ? "Leave channel" B_UTF8_ELLIPSIS : "Leave group" B_UTF8_ELLIPSIS,
		"delete");
	menu->SetTargetForItems(this);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(where, true, false, true);
}


void
MainWindow::_AskDeleteChat(int64 chatId, const char* title, int32 kind)
{
	BString text;
	if (kind == 0)
		text << "Delete the chat with " << title << "? Its messages are deleted for you; "
			"this cannot be undone.";
	else
		text << "Leave " << title << " and remove it from your chats?";
	BAlert* alert = new BAlert("Delete", text, kind == 0 ? "Delete" : "Leave", "Cancel",
		nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(1, B_ESCAPE);
	BMessage* answer = new BMessage(kMsgChatDeleteAnswer);
	answer->AddInt64("chat_id", chatId);
	alert->Go(new BInvoker(answer, this));
}


void
MainWindow::_SelectChat(int64 chatId)
{
	for (int32 i = 0; i < fList->CountItems(); i++) {
		if (static_cast<ChatItem*>(fList->ItemAt(i))->Id() == chatId) {
			fList->Select(i);
			fList->ScrollToSelection();
			break;
		}
	}
	Activate();
}


void
MainWindow::WindowActivated(bool active)
{
	BWindow::WindowActivated(active);
	BMessage message(kMsgWindowActive);
	message.AddBool("active", active);
	be_app->PostMessage(&message);
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgChatList:
			_UpdateChats(message);
			break;
		case kMsgHistory:
			_UpdateHistory(message);
			break;
		case kMsgDraft:
			if (message->GetInt64("chat_id", 0) == fOpenChat && fEntry->Text()[0] == '\0') {
				fEntry->SetText(message->GetString("text", ""));
				fEntry->TextView()->Select(fEntry->TextView()->TextLength(),
					fEntry->TextView()->TextLength());
			}
			break;
		case kMsgChatStatus:
			if (message->GetInt64("chat_id", 0) == fOpenChat) {
				fSubtitle->SetText(message->GetString("status", ""));
				if (message->GetBool("active", false))
					fSubtitle->SetHighUIColor(B_CONTROL_HIGHLIGHT_COLOR);
				else
					fSubtitle->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
			}
			break;
		case kMsgSelectionChanged:
			_SelectionChanged();
			break;
		case kMsgSend:
			_Send();
			break;
		case kMsgEntryModified:
		{
			if (fOpenChat == 0)
				break;
			BMessage changed(kMsgEntryChanged);
			changed.AddInt64("chat_id", fOpenChat);
			changed.AddBool("empty", fEntry->Text()[0] == '\0');
			be_app->PostMessage(&changed);
			break;
		}
		case kMsgLoadOlder:
		case kMsgLoadNewer:
		case kMsgJumpToLatest:
			be_app->PostMessage(message);
			break;

		case kMsgMessageMenu:
		{
			fMenuWhere = message->GetPoint("where", BPoint());
			BMessage ask(kMsgMessageProperties);
			ask.AddInt64("chat_id", fOpenChat);
			ask.AddInt64("message_id", message->GetInt64("message_id", 0));
			be_app->PostMessage(&ask);
			break;
		}
		case kMsgMessageProperties:
			_ShowMessageMenu(message);
			break;
		case kMsgOpenUrl:
		{
			// The user's browser (or mail app for mailto:) opens it.
			BUrl url(message->GetString("url", ""), true);
			if (url.IsValid())
				url.OpenWithPreferredApplication(false);
			break;
		}
		case kMsgOpenMedia:
			message->RemoveName("chat_id");
			message->AddInt64("chat_id", fOpenChat);
			be_app->PostMessage(message);
			break;
		case kMsgReply:
			_StartReply(message->GetInt64("message_id", 0));
			break;
		case kMsgEdit:
			_StartEdit(message->GetInt64("message_id", 0));
			break;
		case kMsgCopy:
		{
			const MessageRow* row = fMessages->Row(message->GetInt64("message_id", 0));
			if (row == nullptr || !be_clipboard->Lock())
				break;
			be_clipboard->Clear();
			be_clipboard->Data()->AddData("text/plain", B_MIME_TYPE,
				row->text.String(), row->text.Length());
			be_clipboard->Commit();
			be_clipboard->Unlock();
			break;
		}
		case kMsgDeleteAsk:
			_AskDelete(message->GetInt64("message_id", 0),
				message->GetBool("can_self", false), message->GetBool("can_all", false));
			break;
		case kMsgDeleteAnswer:
		{
			bool canSelf = message->GetBool("can_self", false);
			bool canAll = message->GetBool("can_all", false);
			int32 which = message->GetInt32("which", -1);
			bool forEveryone;
			if (canSelf && canAll) {
				if (which == 2 || which < 0)
					break;  // Cancel
				forEveryone = which == 0;
			} else {
				if (which != 0)
					break;
				forEveryone = canAll;
			}
			BMessage del(kMsgDeleteMessage);
			del.AddInt64("chat_id", message->GetInt64("chat_id", 0));
			del.AddInt64("message_id", message->GetInt64("message_id", 0));
			del.AddBool("for_everyone", forEveryone);
			be_app->PostMessage(&del);
			break;
		}
		case kMsgCancelCompose:
			_CancelCompose();
			break;
		case kMsgForward:
		{
			// Choose where to, from the chats in the list on screen.
			std::vector<ChatPicker::Entry> chats;
			for (int32 i = 0; i < fList->CountItems(); i++) {
				ChatItem* item = static_cast<ChatItem*>(fList->ItemAt(i));
				chats.push_back({item->Id(), item->Title()});
			}
			BMessage* reply = new BMessage(kMsgForwardMessage);
			reply->AddInt64("from_chat_id", fOpenChat);
			reply->AddInt64("message_id", message->GetInt64("message_id", 0));
			(new ChatPicker(this, chats, reply))->Show();
			break;
		}
		case kMsgForwardMessage:
			be_app->PostMessage(message);
			break;
		case kMsgChatLists:
		{
			// Rebuild the menu, keeping the shown list marked.
			while (BMenuItem* item = fListMenu->RemoveItem((int32)0))
				delete item;
			const char* key;
			for (int32 i = 0; message->FindString("key", i, &key) == B_OK; i++) {
				BMessage* choose = new BMessage(kMsgListChosen);
				choose->AddString("key", key);
				BMenuItem* item = new BMenuItem(message->GetString("name", i, key), choose);
				item->SetTarget(this);
				item->SetMarked(fShownList == key);
				fListMenu->AddItem(item);
			}
			if (fListMenu->FindMarked() == nullptr && fListMenu->CountItems() > 0)
				fListMenu->ItemAt(0)->SetMarked(true);
			break;
		}
		case kMsgListChosen:
		{
			fShownList = message->GetString("key", "main");
			BMessage show(kMsgShowList);
			show.AddString("key", fShownList);
			be_app->PostMessage(&show);
			break;
		}
		case kMsgChatMenu:
			_ShowChatMenu(message->GetInt32("index", -1), message->GetPoint("where", BPoint()));
			break;
		case kMsgChatMenuItem:
		{
			BString action = message->GetString("action", "");
			if (action == "delete") {
				_AskDeleteChat(message->GetInt64("chat_id", 0), message->GetString("title", ""),
					message->GetInt32("kind", 0));
				break;
			}
			BMessage act(kMsgChatAction);
			act.AddInt64("chat_id", message->GetInt64("chat_id", 0));
			act.AddString("action", action);
			be_app->PostMessage(&act);
			break;
		}
		case kMsgChatDeleteAnswer:
			if (message->GetInt32("which", 1) == 0) {
				BMessage act(kMsgChatAction);
				act.AddInt64("chat_id", message->GetInt64("chat_id", 0));
				act.AddString("action", "delete");
				be_app->PostMessage(&act);
			}
			break;
		case kMsgAttach:
		{
			if (fOpenChat == 0)
				break;
			if (fFilePanel == nullptr) {
				fFilePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
					nullptr, B_FILE_NODE, true, new BMessage(kMsgFilesChosen));
				fFilePanel->Window()->SetTitle(APP_NAME ": Attach files");
				fFilePanel->SetButtonLabel(B_DEFAULT_BUTTON, "Attach");
			}
			fFilePanel->Show();
			break;
		}
		case kMsgShowEmoji:
		case kMsgShowStickers:
		{
			if (fOpenChat == 0)
				break;
			BButton* button = message->what == kMsgShowEmoji ? fEmojiButton : fStickerButton;
			BPoint corner = button->ConvertToScreen(button->Bounds().RightTop());
			if (message->what == kMsgShowEmoji) {
				if (fEmojiWindow == nullptr)
					fEmojiWindow = new EmojiWindow(BMessenger(this));
				fEmojiWindow->SetRecent(fRecentEmoji);
				fEmojiWindow->ShowAt(corner);
			} else {
				if (fStickerWindow == nullptr)
					fStickerWindow = new StickerWindow(BMessenger(this), be_app_messenger);
				fStickerWindow->ShowAt(corner);
			}
			break;
		}
		case kMsgInsertEmoji:
			_InsertEmoji(message->GetString("emoji", ""));
			break;
		case kMsgSendSticker:
		{
			if (fOpenChat == 0)
				break;
			message->AddInt64("chat_id", fOpenChat);
			message->AddInt64("reply_to", fReplyTo);
			be_app->PostMessage(message);
			_CancelCompose();
			break;
		}
		case kMsgStickerSets:
		case kMsgStickers:
			if (fStickerWindow != nullptr)
				fStickerWindow->PostMessage(message);
			break;

		case kMsgFilesChosen:
		case B_SIMPLE_DATA:
			// From the file panel, or files dropped on the chat from Tracker.
			_SendFiles(message);
			break;
		case kMsgSelectChat:
			_SelectChat(message->GetInt64("id", 0));
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


bool
MainWindow::QuitRequested()
{
	_SaveDraft();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return false;
}
