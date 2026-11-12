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
#include <ControlLook.h>
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
#include <cstring>

#include "../core/ChatHistory.h"
#include "../core/ChatList.h"
#include "../core/Links.h"
#include "../core/Mentions.h"
#include "../core/SecretChat.h"
#include "BitmapUtils.h"
#include "ChatPicker.h"
#include "ComposeWindows.h"
#include "TouchScroll.h"
#include "SettingsWindow.h"
#include "Icons.h"
#include "Pickers.h"
#include "MessageView.h"
#include "OpenLinkWindow.h"
#include "MentionPopup.h"
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
	kMsgChatFilter = 'cflt',
	kMsgShowFind = 'shfd',
	kMsgHideFind = 'hdfd',
	kMsgFindGo = 'fdgo',
	kMsgFindNext = 'fdnx',
	kMsgFindPrevious = 'fdpv',
	kMsgForward = 'frwd',
	kMsgChatMenuItem = 'cmit',
	kMsgChatDeleteAnswer = 'cdan',
	kMsgShowStickers = 'shst',
	kMsgFilesChosen = 'fchs',
	kMsgSendMenu = 'sdmn',
	kMsgSendSilent = 'sdsl',
	kMsgSendScheduleAsk = 'sdsa',
	kMsgSendAt = 'sdat',
	kMsgSendWhenOnline = 'sdwo',
	kMsgChatWindow = 'cwin'
};


// The Send button: a right-click offers sending without sound, at a set
// time, or when the other person comes online.
class SendButton : public BButton {
public:
	SendButton() : BButton("send", "Send", new BMessage(kMsgSend)) {}

	void MouseDown(BPoint where) override
	{
		uint32 buttons = 0;
		if (Window()->CurrentMessage() != nullptr)
			Window()->CurrentMessage()->FindInt32("buttons", (int32*)&buttons);
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0 && IsEnabled()) {
			BMessage menu(kMsgSendMenu);
			menu.AddPoint("where", ConvertToScreen(where));
			Window()->PostMessage(&menu);
			return;
		}
		BButton::MouseDown(where);
	}
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
	{
		Set(list, index, avatar);
	}

	// New contents for the row (its height stays).
	void Set(const BMessage* list, int32 index, BBitmap* avatar)
	{
		fAvatar = avatar;
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
		fSecret = list->GetBool("secret", index, false);
		fInitials = pp::Initials(fTitle.String()).c_str();
		fKey = Key(list, index);
	}

	// Everything the row shows, to tell whether it needs redrawing.
	static BString Key(const BMessage* list, int32 index)
	{
		BString key;
		key << list->GetInt64("id", index, 0) << '\x1f' << list->GetString("title", index, "")
			<< '\x1f' << list->GetString("text", index, "") << '\x1f'
			<< list->GetString("sender", index, "") << '\x1f'
			<< FormatDate(list->GetInt64("date", index, 0)) << '\x1f'
			<< list->GetInt32("unread", index, 0) << '\x1f' << list->GetInt32("mentions", index, 0)
			<< '\x1f' << (int)list->GetBool("pinned", index, false)
			<< (int)list->GetBool("read", index, false) << (int)list->GetBool("muted", index, false)
			<< (int)list->GetBool("marked_unread", index, false)
			<< (int)list->GetBool("secret", index, false) << '\x1f'
			<< list->GetString("typing", index, "") << '\x1f' << list->GetString("draft", index, "")
			<< '\x1f' << list->GetString("photo", index, "") << '\x1f'
			<< (int32)FindBytes(list, "photo_mini", index).size();
		return key;
	}
	const BString& ItemKey() const { return fKey; }

	int64 Id() const { return fId; }
	const char* Title() const { return fTitle.String(); }
	int32 Unread() const { return fUnread; }
	int32 Mentions() const { return fMentions; }

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
		float titleLeft = left;
		if (fSecret) {
			// Secret chats: a padlock and the title in green, as in
			// Telegram's apps (the list's colours when selected).
			rgb_color green = IsSelected() ? fg : rgb_color{0, 150, 70, 255};
			owner->SetHighColor(green);
			float base = mid - fh.descent - 2;
			float size = ceilf(fh.ascent * 0.8f);
			float width = ceilf(size * 0.75f);
			BRect body(left, base - size * 0.55f, left + width, base);
			BRect shackle(left + width * 0.2f, base - size, left + width * 0.8f,
				base - size * 0.3f);
			owner->SetPenSize(std::max(1.0f, roundf(size / 7)));
			owner->StrokeArc(shackle, 0, 180);
			owner->StrokeLine(BPoint(shackle.left, shackle.top + shackle.Height() / 2),
				BPoint(shackle.left, body.top));
			owner->StrokeLine(BPoint(shackle.right, shackle.top + shackle.Height() / 2),
				BPoint(shackle.right, body.top));
			owner->SetPenSize(1);
			owner->FillRoundRect(body, 1.5f, 1.5f);
			titleLeft = body.right + roundf(size * 0.4f);
		}
		BString title(fTitle);
		bold.TruncateString(&title, B_TRUNCATE_END, right - dateWidth - pad - titleLeft);
		owner->DrawString(title, BPoint(titleLeft, mid - fh.descent - 2));
		owner->SetHighColor(fg);

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
	bool fSecret = false;   // a secret chat: padlock, green title
public:
	bool Muted() const { return fMuted; }
	bool Pinned() const { return fPinned; }
	bool HasUnread() const { return fUnread > 0 || fMarkedUnread; }
	int32 Kind() const { return fKind; }
private:
	BBitmap* fAvatar;   // round chat photo (owned by the window), or null
	BString fKey;
};

// The chat list: a right click opens the chat's menu.
class ChatListView : public BListView {
public:
	ChatListView()
		:
		BListView("chats")
	{
		// Rows put the date and badge at the right edge: when the list
		// gets wider or narrower (the divider, the window), all of each row
		// must be redrawn, not just the strip that appears; otherwise the
		// badges stay where the edge was.
		SetFlags(Flags() | B_FULL_UPDATE_ON_RESIZE);
	}

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


} // namespace


// The chat's pinned message under its header, as in Telegram: an accent
// stripe, "Pinned message" (with its number when there are several) and the
// message's first line. A click jumps to it and shows the next one.
class PinnedBar : public BView {
public:
	PinnedBar()
		:
		BView("pinned bar", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		font_height fh;
		be_plain_font->GetHeight(&fh);
		float line = ceilf(fh.ascent + fh.descent + fh.leading);
		SetExplicitMinSize(BSize(be_plain_font->StringWidth("m") * 6, line * 2 + 8));
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, line * 2 + 8));
	}

	void Set(int32 index, int32 count, const BString& text)
	{
		fTitle = "Pinned message";
		if (count > 1)
			fTitle << " " << count - index << " of " << count;
		fText = text;
		fText.ReplaceAll('\n', ' ');
		Invalidate();
	}

	void Draw(BRect) override
	{
		BRect r = Bounds();
		rgb_color accent = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
		SetHighUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_1_TINT);
		StrokeLine(BPoint(r.left, r.bottom), BPoint(r.right, r.bottom));
		float x = be_control_look->DefaultItemSpacing() / 2;
		SetHighColor(accent);
		FillRect(BRect(x, 4, x + 2, r.bottom - 4));
		x += 3 + be_control_look->DefaultItemSpacing() / 2;
		font_height fh;
		be_plain_font->GetHeight(&fh);
		float line = ceilf(fh.ascent + fh.descent + fh.leading);
		BString title(fTitle), text(fText);
		be_bold_font->TruncateString(&title, B_TRUNCATE_END, r.right - x - 4);
		be_plain_font->TruncateString(&text, B_TRUNCATE_END, r.right - x - 4);
		SetFont(be_bold_font);
		DrawString(title.String(), BPoint(x, 4 + fh.ascent));
		SetFont(be_plain_font);
		SetHighUIColor(B_PANEL_TEXT_COLOR);
		DrawString(text.String(), BPoint(x, 4 + line + fh.ascent));
	}

	void MouseDown(BPoint) override
	{
		Window()->PostMessage(kMsgPinnedClicked);
	}

private:
	BString fTitle;
	BString fText;
};


namespace {

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

// On the entry: Up, Down, Enter and Tab go to the mention suggestions
// while they show.
class MentionKeyFilter : public BMessageFilter {
public:
	explicit MentionKeyFilter(MainWindow* window)
		: BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_KEY_DOWN), fWindow(window) {}

	filter_result Filter(BMessage* message, BHandler**) override
	{
		const char* bytes;
		if (message->FindString("bytes", &bytes) == B_OK && fWindow->MentionKey(bytes))
			return B_SKIP_MESSAGE;
		return B_DISPATCH_MESSAGE;
	}

private:
	MainWindow* fWindow;
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
	app->AddItem(new BMenuItem("Profile" B_UTF8_ELLIPSIS, new BMessage(kMsgShowProfile)));
	app->AddItem(new BMenuItem("Settings" B_UTF8_ELLIPSIS, new BMessage(kMsgShowSettings), ','));
	app->AddSeparatorItem();
	app->AddItem(new BMenuItem("Log out", new BMessage(kMsgLogOut)));
	app->AddSeparatorItem();
	app->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
	app->SetTargetForItems(be_app);
	bar->AddItem(app);
	// HIG: Find, Find again (Cmd+G), here Find previous too.
	BMenu* search = new BMenu("Search");
	search->AddItem(new BMenuItem("Find" B_UTF8_ELLIPSIS, new BMessage(kMsgShowFind), 'F'));
	BMessage* next = new BMessage(kMsgFindNext);
	search->AddItem(new BMenuItem("Find again", next, 'G'));
	search->AddItem(new BMenuItem("Find previous", new BMessage(kMsgFindPrevious), 'G',
		B_SHIFT_KEY));
	bar->AddItem(search);
	BMenu* chat = new BMenu("Chat");
	// Starting chats: the app opens these windows.
	BMenuItem* newChat = new BMenuItem("New chat" B_UTF8_ELLIPSIS,
		new BMessage(kMsgShowNewChat), 'N');
	BMessage* newGroup = new BMessage(kMsgShowNewChat);
	newGroup->AddString("what", "group");
	BMessage* newChannel = new BMessage(kMsgShowNewChat);
	newChannel->AddString("what", "channel");
	BMessage* addContact = new BMessage(kMsgShowNewChat);
	addContact->AddString("what", "contact");
	BMenuItem* newGroupItem = new BMenuItem("New group" B_UTF8_ELLIPSIS, newGroup);
	BMenuItem* newChannelItem = new BMenuItem("New channel" B_UTF8_ELLIPSIS, newChannel);
	BMenuItem* addContactItem = new BMenuItem("Add contact" B_UTF8_ELLIPSIS, addContact);
	for (BMenuItem* item : {newChat, newGroupItem, newChannelItem, addContactItem}) {
		item->SetTarget(be_app);
		chat->AddItem(item);
	}
	chat->AddSeparatorItem();
	fInfoItem = new BMenuItem("Info" B_UTF8_ELLIPSIS, new BMessage(kMsgShowChatInfo), 'I');
	fInfoItem->SetEnabled(false);
	chat->AddItem(fInfoItem);
	fSecretItem = new BMenuItem("New secret chat", new BMessage(kMsgNewSecretChat));
	fSecretItem->SetEnabled(false);
	chat->AddItem(fSecretItem);
	fSelfDestructMenu = new BMenu("Self-destruct timer");
	fSelfDestructMenu->SetRadioMode(true);
	for (int32 seconds : pp::SelfDestructTimes()) {
		BMessage* timer = new BMessage(kMsgSetSelfDestruct);
		timer->AddInt32("seconds", seconds);
		fSelfDestructMenu->AddItem(new BMenuItem(pp::SelfDestructText(seconds).c_str(), timer));
	}
	chat->AddItem(fSelfDestructMenu);
	fSelfDestructMenu->Superitem()->SetEnabled(false);
	chat->AddSeparatorItem();
	chat->AddItem(new BMenuItem("Open username or link" B_UTF8_ELLIPSIS,
		new BMessage(kMsgOpenLinkAsk), 'O'));
	chat->AddSeparatorItem();
	// For the open chat: the app opens these windows.
	BMessage* poll = new BMessage(kMsgChatWindow);
	poll->AddInt32("which", kMsgShowPoll);
	BMessage* location = new BMessage(kMsgChatWindow);
	location->AddInt32("which", kMsgShowLocation);
	BMessage* scheduled = new BMessage(kMsgChatWindow);
	scheduled->AddInt32("which", kMsgShowScheduled);
	fPollItem = new BMenuItem("New poll" B_UTF8_ELLIPSIS, poll);
	fLocationItem = new BMenuItem("Send location" B_UTF8_ELLIPSIS, location);
	fScheduleItem = new BMenuItem("Schedule message" B_UTF8_ELLIPSIS,
		new BMessage(kMsgSendScheduleAsk));
	fScheduledItem = new BMenuItem("Scheduled messages" B_UTF8_ELLIPSIS, scheduled);
	for (BMenuItem* item : {fPollItem, fLocationItem, fScheduleItem, fScheduledItem}) {
		item->SetEnabled(false);
		chat->AddItem(item);
	}
	bar->AddItem(chat);

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
	fChatSearch = new BTextControl("chat search", nullptr, "", nullptr);
	fChatSearch->SetModificationMessage(new BMessage(kMsgChatFilter));
	fChatSearch->TextView()->SetExplicitMinSize(BSize(be_plain_font->Size() * 6, B_SIZE_UNSET));
	fChatSearch->SetToolTip("Search chats");

	fFindText = new BTextControl("find", "Find:", "", new BMessage(kMsgFindGo));
	fFindResult = new BStringView("find result", "");
	fFindBar = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(static_cast<BGroupView*>(fFindBar))
		.SetInsets(B_USE_SMALL_SPACING, 0, B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
		.Add(fFindText, 1.0f)
		.Add(fFindResult)
		.Add(new BButton("previous", "Previous", new BMessage(kMsgFindPrevious)))
		.Add(new BButton("next", "Next", new BMessage(kMsgFindNext)))
		.Add(new BButton("close find", "Close", new BMessage(kMsgHideFind)));
	fFindBar->Hide();

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
	fSendButton = new SendButton();
	fSendButton->SetToolTip("Right-click to send without sound or later");
	// Icon buttons with tooltips, like Paper Plane's entry row.
	fAttachButton = _IconButton("attach", kIconAttach, kMsgAttach, "Attach files");
	fEmojiButton = _IconButton("emoji", kIconEmoji, kMsgShowEmoji, "Emoji");
	fStickerButton = _IconButton("stickers", kIconSticker, kMsgShowStickers, "Stickers");
	fMicButton = _IconButton("record", kIconMic, kMsgVoiceRecord,
		"Record a voice message (click again to send)");
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
	fPinnedBar = new PinnedBar();
	fPinnedBar->SetToolTip("Go to the pinned message");
	fPinnedBar->Hide();
	// Escape cancels a reply or an edit (window shortcuts always need Command).
	AddCommonFilter(new EscapeFilter());
	AddTouchScrolling(this);
	fEntry->TextView()->AddFilter(new MentionKeyFilter(this));

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
				.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
					.SetInsets(B_USE_SMALL_SPACING)
					.Add(fListField)
					.Add(fChatSearch, 1.0f)
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
				.Add(fPinnedBar)
				.Add(fFindBar)
				.Add(messageScroll, 1.0f)
				.Add(fComposeBar)
				// The right inset keeps Send clear of the document window's
				// resize knob in the bottom right corner (it covered Send).
				.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
					.SetInsets(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING,
						B_V_SCROLL_BAR_WIDTH + be_control_look->DefaultItemSpacing() / 2,
						B_USE_SMALL_SPACING)
					.Add(fAttachButton)
					.Add(fEntry, 1.0f)
					.Add(fEmojiButton)
					.Add(fStickerButton)
					.Add(fMicButton)
					.Add(fSendButton)
				.End()
			.End()
		.End();

	SetSizeLimits(em * 40, B_SIZE_UNLIMITED, em * 20, B_SIZE_UNLIMITED);
}


void
MainWindow::_UpdateChats(const BMessage* incoming)
{
	// Keep the whole list; show the chats whose names match the search.
	if (incoming != &fLastChats)
		fLastChats = *incoming;
	BMessage filtered(kMsgChatList);
	BString query(fChatSearch->Text());
	query.Trim();
	const BMessage* list = &fLastChats;
	if (!query.IsEmpty()) {
		int32 total = 0;
		fLastChats.GetInfo("id", nullptr, &total);
		char* name;
		type_code type;
		int32 entries;
		for (int32 i = 0; i < total; i++) {
			if (BString(fLastChats.GetString("title", i, "")).IFindFirst(query) < 0)
				continue;
			for (int32 f = 0; fLastChats.GetInfo(B_ANY_TYPE, f, &name, &type, &entries) == B_OK; f++) {
				const void* data;
				ssize_t size;
				if (entries == total && fLastChats.FindData(name, type, i, &data, &size) == B_OK)
					filtered.AddData(name, type, data, size, false);
			}
		}
		list = &filtered;
	}
	int64 selected = -1;
	if (ChatItem* item = dynamic_cast<ChatItem*>(fList->ItemAt(fList->CurrentSelection())))
		selected = item->Id();

	// Only rows that changed are replaced (and redrawn): rebuilding the
	// whole list on every update, every few seconds, made it flicker.
	int32 count = 0;
	list->GetInfo("id", nullptr, &count);
	while (fList->CountItems() > count)
		delete fList->RemoveItem(fList->CountItems() - 1);
	for (int32 i = 0; i < count; i++) {
		ChatItem* old = static_cast<ChatItem*>(fList->ItemAt(i));
		if (old != nullptr && old->ItemKey() == ChatItem::Key(list, i))
			continue;
		if (old == nullptr)
			fList->AddItem(new ChatItem(list, i, _Avatar(list, i)));
		else {
			// The row object stays (BListView::ReplaceItem on beta6 leaves
			// the new row without a height); it takes the new contents.
			old->Set(list, i, _Avatar(list, i));
			fList->InvalidateItem(i);
		}
	}

	for (int32 i = 0; i < count; i++) {
		if (static_cast<ChatItem*>(fList->ItemAt(i))->Id() == selected)
			fList->Select(i);
		// The open chat's unread count, for the jump button's badge.
		if (list->GetInt64("id", i, 0) == fOpenChat) {
			fMessages->SetUnreadCount(list->GetInt32("unread", i, 0));
			fMessages->SetUnreadMentions(list->GetInt32("mentions", i, 0));
		}
	}
}


void
MainWindow::_SelectionChanged()
{
	int64 id = 0;
	if (ChatItem* item = dynamic_cast<ChatItem*>(fList->ItemAt(fList->CurrentSelection())))
		id = item->Id();
	// A row that went away (filtered out, moved to another list) doesn't
	// close the chat being read.
	if (id == fOpenChat || id == 0)
		return;
	_SaveDraft();
	fOpenChat = id;
	fInfoItem->SetEnabled(id != 0);
	fTitle->SetText(id != 0 ? static_cast<ChatItem*>(
		fList->ItemAt(fList->CurrentSelection()))->Title() : "");
	fSubtitle->SetText("");
	ChatItem* opened = dynamic_cast<ChatItem*>(fList->ItemAt(fList->CurrentSelection()));
	fMessages->SetUnreadCount(opened != nullptr ? opened->Unread() : 0);
	fMessages->SetUnreadMentions(opened != nullptr ? opened->Mentions() : 0);
	fMessages->SetMoreBelow(false);
	fMessages->SetMessages({}, true);
	fMessages->SetEmptyText(id != 0 ? "Loading messages…"
		: "Select a chat to start messaging");
	_CancelCompose();
	if (!fFindBar->IsHidden())
		fFindBar->Hide();
	fFindText->SetText("");
	fFindResult->SetText("");
	fMentioned.clear();
	_CloseMentions();
	fEntry->SetText("");
	if (!fPinnedBar->IsHidden())
		fPinnedBar->Hide();
	fCanJoin = false;
	fWriteBlocked = false;
	fSecretChat = fCanStartSecret = fCanSendWhenOnline = false;
	fSelfDestruct = 0;
	_UpdateSecretMenu();
	if (fRecording)
		_SetRecording(false, 0, 0);   // the app stops it when the chat changes
	fSendButton->SetLabel("Send");
	fEntry->SetEnabled(id != 0);
	fSendButton->SetEnabled(id != 0);
	fAttachButton->SetEnabled(id != 0);
	fEmojiButton->SetEnabled(id != 0);
	fStickerButton->SetEnabled(id != 0);
	fMicButton->SetEnabled(id != 0);
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
		row.animationPath = history->GetString("animation", i, "");
		row.animationFormat = history->GetInt32("animation_format", i, 0);
		row.duration = history->GetInt32("duration", i, 0);
		row.waveform = FindBytes(history, "waveform", i);
		row.fileName = history->GetString("file_name", i, "");
		row.fileTitle = history->GetString("file_title", i, "");
		row.fileType = history->GetString("file_type", i, "");
		row.fileSize = history->GetInt64("file_size", i, 0);
		row.fileDone = history->GetInt64("file_done", i, -1);
		row.showAvatar = history->GetBool("show_avatar", i, false);
		row.senderKey = history->GetInt64("sender_key", i, 0);
		row.avatarPath = history->GetString("avatar", i, "");
		row.avatarPreview = FindBytes(history, "avatar_mini", i);
		row.avatarInitials = history->GetString("initials", i, "");
		row.spans = pp::DecodeSpans(history->GetString("spans", i, ""));
		// "emoji\tcount\tchosen" per line
		BString reactions = history->GetString("reactions", i, "");
		int32 start = 0;
		while (start < reactions.Length()) {
			int32 end = reactions.FindFirst('\n', start);
			if (end < 0)
				end = reactions.Length();
			BString entry;
			reactions.CopyInto(entry, start, end - start);
			start = end + 1;
			int32 t1 = entry.FindFirst('\t'), t2 = entry.FindLast('\t');
			if (t1 < 0 || t2 <= t1)
				continue;
			MessageRow::Reaction r;
			entry.CopyInto(r.emoji, 0, t1);
			r.count = atoi(entry.String() + t1 + 1);
			r.chosen = entry[t2 + 1] == '1';
			row.reactions.push_back(r);
		}
		rows.push_back(row);
	}
	fMessages->SetEmptyText("No messages here yet");
	fMessages->SetMoreBelow(history->GetBool("more_below", false));
	fMessages->SetHighlight(history->GetInt64("highlight", 0));
	fMessages->SetMessages(rows, history->GetBool("scroll_end", false),
		history->GetInt64("scroll_to", 0));
}


void
MainWindow::_Send(bool silent, int64 sendAt, bool whenOnline)
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
		send.AddBool("silent", silent);
		send.AddInt64("send_at", sendAt);
		send.AddBool("when_online", whenOnline);
		for (const auto& mention : fMentioned) {
			send.AddString("mention_name", mention.first.c_str());
			send.AddInt64("mention_user", mention.second);
		}
		be_app->PostMessage(&send);
	}
	fMentioned.clear();
	_CloseMentions();
	_CancelCompose();
	fEntry->SetText("");
}


void
MainWindow::_UpdateMentionQuery()
{
	// Groups only (TDLib's chat ids for them are negative).
	std::string query;
	int32 start = 0, end = 0;
	fEntry->TextView()->GetSelection(&start, &end);
	if (fOpenChat >= 0 || start != end
		|| !pp::MentionQuery(fEntry->Text(), (size_t)end, &query, &fMentionAt)) {
		_CloseMentions();
		return;
	}
	if (fMentions == nullptr)
		fMentions = new MentionPopup(this);
	BMessage search(kMsgMentionSearch);
	search.AddInt64("chat_id", fOpenChat);
	search.AddString("query", query.c_str());
	search.AddInt32("seq", ++fMentionSeq);
	be_app->PostMessage(&search);
}


void
MainWindow::_CloseMentions()
{
	fMentionSeq++;   // answers still on their way are stale
	if (fMentions != nullptr)
		fMentions->Close();
}


bool
MainWindow::MentionKey(const char* bytes)
{
	if (fMentions == nullptr || !fMentions->IsShowing())
		return false;
	switch (bytes[0]) {
		case B_UP_ARROW:
			fMentions->MoveSelection(-1);
			return true;
		case B_DOWN_ARROW:
			fMentions->MoveSelection(1);
			return true;
		case B_ENTER:
		case B_TAB:
			_ChooseMention(-1);
			return true;
	}
	return false;
}


void
MainWindow::_ChooseMention(int32 index)
{
	MentionPopup::Item item;
	if (fMentions == nullptr || !(index < 0 ? fMentions->Selected(&item)
			: fMentions->ItemAt(index, &item))) {
		_CloseMentions();
		return;
	}
	int32 start = 0, end = 0;
	fEntry->TextView()->GetSelection(&start, &end);
	std::string name = pp::FirstName(item.name.String());
	size_t cursor = 0;
	std::string text = pp::InsertMention(fEntry->Text(), fMentionAt, (size_t)end, name,
		item.username.String(), &cursor);
	if (item.username.IsEmpty())
		fMentioned.push_back({name, item.userId});
	_CloseMentions();
	fEntry->SetText(text.c_str());   // posts kMsgEntryModified: no "@" left before the cursor
	fEntry->TextView()->Select((int32)cursor, (int32)cursor);
	fEntry->MakeFocus(true);
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
	item(fMessages->SelectedText(row->id).IsEmpty() ? "Copy" : "Copy selection", kMsgCopy,
		properties->GetBool("can_copy", true) && !row->text.IsEmpty());
	item("Forward" B_UTF8_ELLIPSIS, kMsgForward, properties->GetBool("can_forward", false));
	// The reactions Telegram allows on this message (custom emoji ones,
	// which Haiku can't draw, are left out).
	BMenu* react = new BMenu("React");
	const char* emoji;
	for (int32 i = 0; properties->FindString("reaction", i, &emoji) == B_OK; i++) {
		BMessage* message = new BMessage(kMsgReact);
		message->AddInt64("message_id", id);
		message->AddString("emoji", emoji);
		message->AddBool("add", true);
		react->AddItem(new BMenuItem(emoji, message));
	}
	react->SetTargetForItems(this);
	menu->AddItem(react);
	if (react->CountItems() == 0)
		react->Superitem()->SetEnabled(false);   // reactions are off in this chat
	if (properties->GetBool("can_pin", false)) {
		bool pinned = properties->GetBool("pinned", false);
		item(pinned ? "Unpin" : "Pin", kMsgPin, true)->AddBool("pin", !pinned);
	}
	// Anything with a file behind it (not a sticker's bare emoji).
	if (row->mediaKind != 0 && !(row->mediaKind == 2 && row->imagePath.IsEmpty())) {
		menu->AddSeparatorItem();
		// Photos and videos either way, the way the settings choose first.
		bool viewable = row->mediaKind == pp::Media::kPhoto || row->mediaKind == pp::Media::kVideo
			|| row->mediaKind == pp::Media::kAnimation || row->mediaKind == pp::Media::kVideoNote;
		if (viewable) {
			const char* inViewer = "Open in Powergram";
			const char* inApp = row->mediaKind == pp::Media::kPhoto
				? "Open in image viewer" : "Open in media player";
			item(fMediaInApp ? inViewer : inApp, kMsgOpenMedia, true)
				->AddInt32("where", fMediaInApp ? kOpenInViewer : kOpenInSystemApp);
			item(fMediaInApp ? inApp : inViewer, kMsgOpenMedia, true)
				->AddInt32("where", fMediaInApp ? kOpenInSystemApp : kOpenInViewer);
		} else
			item("Open", kMsgOpenMedia, true);
		item("Save as" B_UTF8_ELLIPSIS, kMsgSaveAsk, true);
	}
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
	fSendButton->SetLabel(fCanJoin ? "Join" : "Send");
	if (wasEditing)
		fEntry->SetText("");
}


void
MainWindow::_Record(const char* action)
{
	if (fOpenChat == 0)
		return;
	if (strcmp(action, "start") == 0 && fEditing != 0)
		_CancelCompose();   // an edit can't become a voice message
	BMessage record(kMsgVoiceRecord);
	record.AddString("action", action);
	record.AddInt64("chat_id", fOpenChat);
	record.AddInt64("reply_to", fReplyTo);
	be_app->PostMessage(&record);
	if (strcmp(action, "send") == 0)
		fReplyTo = 0;   // answered
}


void
MainWindow::_SetRecording(bool recording, int32 seconds, float level)
{
	if (recording) {
		// "● Recording 0:07" and a level meter of eight blocks.
		BString text("\xE2\x97\x8F Recording ");
		text << seconds / 60 << ":" << (seconds % 60 < 10 ? "0" : "") << seconds % 60 << "   ";
		int blocks = (int)roundf(std::min(1.0f, level * 2) * 8);
		for (int i = 0; i < 8; i++)
			text << (i < blocks ? "\xE2\x96\xAE" : "\xE2\x96\xAF");
		fComposeLabel->SetText(text);
	}
	if (recording == fRecording)
		return;
	fRecording = recording;
	if (recording) {
		fComposeLabel->SetHighUIColor(B_FAILURE_COLOR);
		if (fComposeBar->IsHidden())
			fComposeBar->Show();
		fSendButton->SetLabel("Send");
		fMicButton->SetToolTip("Send the voice message");
	} else {
		fComposeLabel->SetHighUIColor(B_PANEL_TEXT_COLOR);
		fMicButton->SetToolTip("Record a voice message (click again to send)");
		// Back to the reply it was recorded for, if any.
		if (fReplyTo != 0)
			_StartReply(fReplyTo);
		else if (!fComposeBar->IsHidden())
			fComposeBar->Hide();
		fSendButton->SetLabel(fCanJoin ? "Join" : fEditing != 0 ? "Save" : "Send");
	}
	fEntry->SetEnabled(!recording && !fCanJoin);
	fAttachButton->SetEnabled(!recording && !fCanJoin);
	fEmojiButton->SetEnabled(!recording && !fCanJoin);
	fStickerButton->SetEnabled(!recording && !fCanJoin);
}


void
MainWindow::_SetWriteBlocked(bool blocked)
{
	if (blocked == fWriteBlocked || fOpenChat == 0)
		return;
	fWriteBlocked = blocked;
	if (blocked) {
		_CancelCompose();
		fEntry->MakeFocus(false);   // no caret left blinking in it
	}
	fSendButton->SetEnabled(!blocked);
	fEntry->SetEnabled(!blocked);
	fAttachButton->SetEnabled(!blocked);
	fEmojiButton->SetEnabled(!blocked);
	fStickerButton->SetEnabled(!blocked);
	fMicButton->SetEnabled(!blocked);
	if (!blocked)
		fEntry->MakeFocus(true);
}


void
MainWindow::_UpdateSecretMenu()
{
	fSecretItem->SetEnabled(fCanStartSecret);
	// Polls, places and scheduling: not in secret chats, nor where one
	// can't write.
	bool canSend = fOpenChat != 0 && !fCanJoin && !fWriteBlocked;
	fPollItem->SetEnabled(canSend && !fSecretChat);
	fLocationItem->SetEnabled(canSend);
	fScheduleItem->SetEnabled(canSend && !fSecretChat);
	fScheduledItem->SetEnabled(fOpenChat != 0 && !fSecretChat);
	fSelfDestructMenu->Superitem()->SetEnabled(fSecretChat && !fWriteBlocked);
	for (int32 i = 0; BMenuItem* item = fSelfDestructMenu->ItemAt(i); i++)
		item->SetMarked(item->Message()->GetInt32("seconds", -1) == fSelfDestruct);
}


void
MainWindow::_SetCanJoin(bool canJoin)
{
	if (canJoin == fCanJoin || fOpenChat == 0)
		return;
	fCanJoin = canJoin;
	if (canJoin)
		_CancelCompose();
	fSendButton->SetLabel(canJoin ? "Join" : "Send");
	fEntry->SetEnabled(!canJoin);
	fAttachButton->SetEnabled(!canJoin);
	fEmojiButton->SetEnabled(!canJoin);
	fStickerButton->SetEnabled(!canJoin);
	fMicButton->SetEnabled(!canJoin);
	if (!canJoin)
		fEntry->MakeFocus(true);
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


// A theme chosen while the app runs: Haiku recolours the fields, but text
// already typed keeps its colour (light on white after leaving a dark
// theme), and the icons were drawn in the old text colour.
void
MainWindow::_UpdateColors()
{
	for (BTextControl* control : {fEntry, fChatSearch, fFindText}) {
		BTextView* text = control->TextView();
		BFont font;
		rgb_color old;
		text->GetFontAndColor(0, &font, &old);
		rgb_color color = ui_color(B_DOCUMENT_TEXT_COLOR);
		if (!control->IsEnabled())
			color = tint_color(color, B_DISABLED_LABEL_TINT);
		text->SetFontAndColor(0, text->TextLength(), &font, B_FONT_ALL, &color);
		// What's typed next, too.
		text->SetFontAndColor(&font, B_FONT_ALL, &color);
		text->Invalidate();
	}
	font_height fh;
	be_plain_font->GetHeight(&fh);
	float size = roundf((fh.ascent + fh.descent) * 1.3f);
	std::pair<BButton*, IconKind> buttons[] = {{fAttachButton, kIconAttach},
		{fEmojiButton, kIconEmoji}, {fStickerButton, kIconSticker}, {fMicButton, kIconMic}};
	for (const auto& [button, kind] : buttons) {
		BBitmap* icon = MakeIcon(kind, size, ui_color(B_CONTROL_TEXT_COLOR));
		button->SetIcon(icon);
		delete icon;   // the button keeps its own copy
	}
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
MainWindow::_ShowFindBar(bool show)
{
	if (show) {
		if (fFindBar->IsHidden())
			fFindBar->Show();
		fFindText->MakeFocus(true);
		fFindText->TextView()->SelectAll();
	} else {
		if (!fFindBar->IsHidden())
			fFindBar->Hide();
		fFindText->SetText("");
		fFindResult->SetText("");
		BMessage clear(kMsgFind);
		clear.AddString("query", "");
		be_app->PostMessage(&clear);
		fEntry->MakeFocus(true);
	}
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
	add("Info" B_UTF8_ELLIPSIS, "info");
	menu->AddSeparatorItem();
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
	if (!active)
		_CloseMentions();
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
		case kMsgVoiceProgress:
			fMessages->SetVoiceProgress(message->GetInt64("message_id", 0),
				message->GetFloat("progress", 0));
			break;
		case kMsgSettingsChanged:
			fMessages->SetTextScale(AppSettings::Scale(message->GetInt32("text_size", 1)));
			fMediaInApp = message->GetBool("media_in_app", false);
			SetDragScrollMode(message->GetInt32("drag_scroll", kDragScrollTouch));
			break;
		case kMsgChatFilter:
			_UpdateChats(&fLastChats);
			break;
		case kMsgShowFind:
			if (fOpenChat != 0)
				_ShowFindBar(true);
			break;
		case kMsgHideFind:
			_ShowFindBar(false);
			break;
		case kMsgFindGo:
		{
			BMessage find(kMsgFind);
			find.AddString("query", fFindText->Text());
			be_app->PostMessage(&find);
			fFindResult->SetText("Searching" B_UTF8_ELLIPSIS);
			break;
		}
		case kMsgFindNext:
		case kMsgFindPrevious:
		{
			if (fFindBar->IsHidden()) {
				_ShowFindBar(true);
				break;
			}
			// Results are newest first: "next" goes back in time.
			BMessage step(kMsgFindStep);
			step.AddInt32("delta", message->what == kMsgFindNext ? 1 : -1);
			be_app->PostMessage(&step);
			break;
		}
		case kMsgFindResult:
		{
			int32 index = message->GetInt32("index", -1), count = message->GetInt32("count", 0);
			BString text;
			if (count == 0)
				text = "No results";
			else
				text << index + 1 << " of " << count;
			fFindResult->SetText(text);
			break;
		}
		case kMsgChatStatus:
			if (message->GetInt64("chat_id", 0) == fOpenChat) {
				// A secret chat that can't be written in says why instead.
				const char* notice = message->GetString("notice", "");
				fSubtitle->SetText(notice[0] != '\0' ? notice
					: message->GetString("status", ""));
				if (message->GetBool("active", false) && notice[0] == '\0')
					fSubtitle->SetHighUIColor(B_CONTROL_HIGHLIGHT_COLOR);
				else
					fSubtitle->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
				_SetCanJoin(message->GetBool("can_join", false));
				_SetWriteBlocked(notice[0] != '\0');
				fSecretChat = message->GetBool("secret", false);
				fCanStartSecret = message->GetBool("can_start_secret", false);
				fCanSendWhenOnline = message->GetBool("can_send_when_online", false);
				fSelfDestruct = message->GetInt32("self_destruct", 0);
				_UpdateSecretMenu();
			}
			break;
		case kMsgSelectionChanged:
			_SelectionChanged();
			break;
		case kMsgSend:
			if (fRecording) {
				_Record("send");
				break;
			}
			if (fCanJoin) {
				BMessage join(kMsgJoinChat);
				join.AddInt64("chat_id", fOpenChat);
				be_app->PostMessage(&join);
				break;
			}
			_Send();
			break;
		case kMsgSendMenu:
		{
			if (fOpenChat == 0 || fCanJoin || fRecording || fEditing != 0)
				break;
			bool empty = BString(fEntry->Text()).Trim().IsEmpty();
			BPopUpMenu* menu = new BPopUpMenu("send options", false, false);
			BMenuItem* silent = new BMenuItem("Send without sound", new BMessage(kMsgSendSilent));
			BMenuItem* schedule = new BMenuItem("Schedule message" B_UTF8_ELLIPSIS,
				new BMessage(kMsgSendScheduleAsk));
			BMenuItem* online = new BMenuItem("Send when online", new BMessage(kMsgSendWhenOnline));
			silent->SetEnabled(!empty);
			// Secret chats can't schedule; "when online" needs the other
			// person's exact status.
			schedule->SetEnabled(!empty && !fSecretChat);
			online->SetEnabled(!empty && fCanSendWhenOnline);
			menu->AddItem(silent);
			menu->AddItem(schedule);
			menu->AddItem(online);
			menu->SetTargetForItems(this);
			menu->SetAsyncAutoDestruct(true);
			menu->Go(message->GetPoint("where", BPoint()), true, true, true);
			break;
		}
		case kMsgColorsChanged:
			_UpdateColors();
			break;
		case kMsgSendSilent:
			_Send(true);
			break;
		case kMsgSendWhenOnline:
			_Send(false, 0, true);
			break;
		case kMsgSendScheduleAsk:
		{
			if (fOpenChat == 0 || fSecretChat || fEditing != 0)
				break;
			if (BString(fEntry->Text()).Trim().IsEmpty()) {
				BAlert* alert = new BAlert("schedule", "Type the message first, then "
					"choose when it's sent.", "OK");
				alert->Go(nullptr);
				break;
			}
			BMessage* reply = new BMessage(kMsgSendAt);
			reply->AddInt64("chat_id", fOpenChat);
			(new ScheduleWindow(BMessenger(this), reply, Frame()))->Show();
			break;
		}
		case kMsgSendAt:
			// The chat may have changed while the time was being chosen.
			if (message->GetInt64("chat_id", 0) == fOpenChat)
				_Send(false, message->GetInt64("send_at", 0));
			break;
		case kMsgChatWindow:
		{
			if (fOpenChat == 0)
				break;
			BMessage show(message->GetInt32("which", 0));
			show.AddInt64("chat_id", fOpenChat);
			be_app->PostMessage(&show);
			break;
		}
		case kMsgEntryModified:
		{
			if (fOpenChat == 0)
				break;
			_UpdateMentionQuery();
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
		case kMsgPinned:
			if (message->GetInt64("chat_id", 0) != fOpenChat)
				break;
			if (message->GetInt64("message_id", 0) == 0) {
				if (!fPinnedBar->IsHidden())
					fPinnedBar->Hide();
				break;
			}
			fPinnedBar->Set(message->GetInt32("index", 0), message->GetInt32("count", 1),
				message->GetString("text", ""));
			if (fPinnedBar->IsHidden())
				fPinnedBar->Show();
			break;
		case kMsgPinnedClicked:
			be_app->PostMessage(message);
			break;
		case kMsgPin:
			message->RemoveName("chat_id");
			message->AddInt64("chat_id", fOpenChat);
			be_app->PostMessage(message);
			break;
		case kMsgOpenLinkAsk:
			(new OpenLinkWindow(this))->Show();
			break;
		case kMsgOpenUrl:
		{
			// Telegram links and mentions open in the app; the rest in the
			// user's browser (or mail app for mailto:).
			const char* address = message->GetString("url", "");
			if (pp::IsTelegramLink(address) || pp::MentionedUser(address) != 0) {
				BMessage open(kMsgOpenLink);
				open.AddString("url", address);
				be_app->PostMessage(&open);
				break;
			}
			BUrl url(address, true);
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
			int64 id = message->GetInt64("message_id", 0);
			const MessageRow* row = fMessages->Row(id);
			if (row == nullptr || !be_clipboard->Lock())
				break;
			// The selected part of the message, else all of it.
			BString text = fMessages->SelectedText(id);
			if (text.IsEmpty())
				text = row->text;
			be_clipboard->Clear();
			be_clipboard->Data()->AddData("text/plain", B_MIME_TYPE,
				text.String(), text.Length());
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
			// Escape: a selection goes first, then a reply or edit.
			if (fMentions != nullptr && fMentions->IsShowing())
				_CloseMentions();
			else if (fRecording)
				_Record("cancel");
			else if (!fMessages->SelectedText().IsEmpty())
				fMessages->ClearSelection();
			else
				_CancelCompose();
			break;
		case kMsgVoiceRecord:
			_Record(fRecording ? "send" : "start");
			break;
		case kMsgMentionResults:
		{
			if (message->GetInt32("seq", -1) != fMentionSeq || fMentions == nullptr)
				break;
			std::vector<MentionPopup::Item> items;
			int64 user;
			for (int32 i = 0; message->FindInt64("user_id", i, &user) == B_OK; i++)
				items.push_back({user, message->GetString("name", i, ""),
					message->GetString("username", i, "")});
			// Over the entry, as wide as it.
			BRect entry = fEntry->ConvertToScreen(fEntry->Bounds());
			fMentions->SetItems(items, BPoint(entry.left, entry.top - 2),
				std::min(entry.Width(), be_plain_font->Size() * 26));
			break;
		}
		case kMsgMentionChosen:
			_ChooseMention(message->GetInt32("index", -1));
			break;
		case kMsgJumpToMention:
		{
			BMessage jump(kMsgJumpToMention);
			jump.AddInt64("chat_id", fOpenChat);
			be_app->PostMessage(&jump);
			break;
		}
		case kMsgVoiceRecordState:
			_SetRecording(message->GetBool("recording", false),
				message->GetInt32("seconds", 0), message->GetFloat("level", 0));
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
		case kMsgReact:
			message->RemoveName("chat_id");
			message->AddInt64("chat_id", fOpenChat);
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
		case kMsgNewSecretChat:
		case kMsgSetSelfDestruct:
			if (fOpenChat != 0) {
				message->RemoveName("chat_id");
				message->AddInt64("chat_id", fOpenChat);
				be_app->PostMessage(message);
			}
			break;
		case kMsgShowChatInfo:
			if (fOpenChat != 0) {
				BMessage show(kMsgShowChatInfo);
				show.AddInt64("chat_id", fOpenChat);
				be_app->PostMessage(&show);
			}
			break;
		case kMsgChatMenu:
			_ShowChatMenu(message->GetInt32("index", -1), message->GetPoint("where", BPoint()));
			break;
		case kMsgChatMenuItem:
		{
			BString action = message->GetString("action", "");
			if (action == "info") {
				BMessage show(kMsgShowChatInfo);
				show.AddInt64("chat_id", message->GetInt64("chat_id", 0));
				be_app->PostMessage(&show);
				break;
			}
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
		case kMsgSaveAsk:
		{
			const MessageRow* row = fMessages->Row(message->GetInt64("message_id", 0));
			if (row == nullptr)
				break;
			BMessage save(kMsgSaveFile);
			save.AddInt64("message_id", row->id);
			if (fSavePanel == nullptr) {
				fSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this));
				fSavePanel->Window()->SetTitle(APP_NAME ": Save file");
			}
			fSavePanel->SetMessage(&save);
			// The sender's name for the file, or one for its kind.
			static const char* const kNames[] = {"File", "Photo.jpg", "Sticker.webp",
				"Video.mp4", "GIF.mp4", "Video message.mp4", "Voice message.ogg",
				"Icon.hvif", "File"};
			BString name = row->fileName;
			if (name.IsEmpty())
				name = kNames[std::min<int32>(std::max<int32>(row->mediaKind, 0), 8)];
			fSavePanel->SetSaveText(name.String());
			fSavePanel->Show();
			break;
		}
		case kMsgSaveFile:
			message->RemoveName("chat_id");
			message->AddInt64("chat_id", fOpenChat);
			be_app->PostMessage(message);
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

		case kMsgDeskbarFiles:
		{
			// Files from the Deskbar item or Tracker: which chat?
			std::vector<ChatPicker::Entry> chats;
			for (int32 i = 0; i < fList->CountItems(); i++) {
				ChatItem* item = static_cast<ChatItem*>(fList->ItemAt(i));
				chats.push_back({item->Id(), item->Title()});
			}
			BMessage* reply = new BMessage(kMsgSendFiles);
			entry_ref ref;
			int32 count = 0;
			for (; message->FindRef("refs", count, &ref) == B_OK; count++)
				reply->AddRef("refs", &ref);
			BString title;
			title << "Send " << count << (count == 1 ? " file to" : " files to");
			(new ChatPicker(this, chats, reply, title.String(), "Send"))->Show();
			break;
		}
		case kMsgSendFiles:
			be_app->PostMessage(message);   // a chat chosen for them
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
