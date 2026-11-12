// SPDX-License-Identifier: GPL-3.0-or-later
#include "ComposeWindows.h"

#include <Alert.h>
#include <Button.h>
#include <CheckBox.h>
#include <DateFormat.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TimeFormat.h>
#include <Url.h>

#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

#include "../core/Compose.h"
#include "Messages.h"
#include "TouchScroll.h"

namespace {

enum {
	kMsgEdited = 'cedt',
	kMsgSchedule = 'csch',
	kMsgSelected = 'csel',
	kMsgSendNow = 'csnw',
	kMsgMove = 'cmov',
	kMsgMoveTo = 'cmvt',
	kMsgDelete = 'cdel',
	kMsgDeleteConfirmed = 'cdlc',
	kMsgOptionEdited = 'copt',
	kMsgSend = 'csnd',
	kMsgShowMap = 'cmap'
};

const int32 kScheduleDays = 31;


int64
MidnightOf(time_t when)
{
	struct tm t;
	localtime_r(&when, &t);
	t.tm_hour = t.tm_min = t.tm_sec = 0;
	t.tm_isdst = -1;
	return (int64)mktime(&t);
}


// `days` after midnight `today`, at `minutes` past midnight, local time
// (mktime takes care of months and summer time).
int64
LocalTime(int64 today, int32 days, int32 minutes)
{
	time_t base = (time_t)today;
	struct tm t;
	localtime_r(&base, &t);
	t.tm_mday += days;
	t.tm_hour = minutes / 60;
	t.tm_min = minutes % 60;
	t.tm_sec = 0;
	t.tm_isdst = -1;
	return (int64)mktime(&t);
}


BString
ClockText(int64 when)
{
	BString clock;
	BTimeFormat().Format(clock, (time_t)when, B_SHORT_TIME_FORMAT);
	return clock;
}


// "Today at 21:00", "Tomorrow at 9:00", "Nov 20, 2026 at 9:00".
BString
WhenText(int64 when)
{
	if (when == 0)
		return "When they come online";
	int64 today = MidnightOf(time(nullptr));
	int64 day = MidnightOf((time_t)when);
	BString text;
	if (day == today)
		text = "Today";
	else if (day == LocalTime(today, 1, 0))
		text = "Tomorrow";
	else
		BDateFormat().Format(text, (time_t)when, B_MEDIUM_DATE_FORMAT);
	text << " at " << ClockText(when);
	return text;
}


// Two lines: when (bold), then what.
class ScheduledItem : public BListItem {
public:
	ScheduledItem(int64 id, int64 sendAt, const BString& preview)
		: fId(id), fSendAt(sendAt), fPreview(preview) {}

	int64 Id() const { return fId; }
	int64 SendAt() const { return fSendAt; }

	void Update(BView* owner, const BFont* font) override
	{
		BListItem::Update(owner, font);
		font_height fh;
		font->GetHeight(&fh);
		SetHeight(ceilf((fh.ascent + fh.descent + fh.leading) * 2.4f));
	}

	void DrawItem(BView* owner, BRect frame, bool) override
	{
		bool selected = IsSelected();
		owner->SetHighUIColor(selected ? B_LIST_SELECTED_BACKGROUND_COLOR
			: B_LIST_BACKGROUND_COLOR);
		owner->FillRect(frame);
		font_height fh;
		be_plain_font->GetHeight(&fh);
		float line = ceilf(fh.ascent + fh.descent + fh.leading);
		float x = frame.left + 6;
		float y = frame.top + (frame.Height() - 2 * line) / 2 + fh.ascent;
		BString when = WhenText(fSendAt);
		be_bold_font->TruncateString(&when, B_TRUNCATE_END, frame.Width() - 12);
		owner->SetFont(be_bold_font);
		owner->SetHighUIColor(selected ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
		owner->DrawString(when.String(), BPoint(x, y));
		BString preview(fPreview);
		be_plain_font->TruncateString(&preview, B_TRUNCATE_END, frame.Width() - 12);
		owner->SetFont(be_plain_font);
		owner->SetHighUIColor(selected ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR,
			selected ? B_NO_TINT : B_DARKEN_2_TINT);
		owner->DrawString(preview.String(), BPoint(x, y + line));
	}

private:
	int64 fId;
	int64 fSendAt;
	BString fPreview;
};

} // namespace


// #pragma mark - ScheduleWindow


ScheduleWindow::ScheduleWindow(BMessenger target, BMessage* reply, BRect parentFrame,
	const char* title, int64 initial)
	:
	BWindow(BRect(0, 0, 300, 120), title, B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fReply(reply)
{
	time_t now = time(nullptr);
	fToday = MidnightOf(now);
	if (initial == 0) {
		// An hour from now, on the next five minutes.
		initial = ((int64)now + 3600 + 299) / 300 * 300;
	}
	int32 initialDay = 0;
	for (int32 day = 0; day < kScheduleDays; day++) {
		if (LocalTime(fToday, day + 1, 0) > initial) {
			initialDay = day;
			break;
		}
	}
	fDays = new BPopUpMenu("days");
	for (int32 day = 0; day < kScheduleDays; day++) {
		BString label;
		if (day == 0)
			label = "Today";
		else if (day == 1)
			label = "Tomorrow";
		else
			BDateFormat().Format(label, (time_t)LocalTime(fToday, day, 12 * 60), B_FULL_DATE_FORMAT);
		BMessage* chosen = new BMessage(kMsgEdited);
		chosen->AddInt32("day", day);
		fDays->AddItem(new BMenuItem(label, chosen));
	}
	fDays->ItemAt(initialDay)->SetMarked(true);
	BMenuField* dayField = new BMenuField("day", "Day:", fDays);
	fTime = new BTextControl("time", "Time:", ClockText(initial), nullptr);
	fTime->SetModificationMessage(new BMessage(kMsgEdited));
	fTime->SetToolTip("e.g. 21:30 or 9:30 pm");
	fProblem = new BStringView("problem", "");
	fProblem->SetHighUIColor(B_FAILURE_COLOR);
	fSchedule = new BButton("schedule", "Schedule", new BMessage(kMsgSchedule));
	float em = be_plain_font->Size();
	fProblem->SetExplicitMinSize(BSize(em * 20, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGrid(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
			.AddMenuField(dayField, 0, 0)
			.AddTextControl(fTime, 0, 1)
		.End()
		.Add(fProblem)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)))
			.Add(fSchedule)
		.End();
	SetDefaultButton(fSchedule);
	_Check();
	fTime->MakeFocus(true);
	CenterIn(parentFrame);
}


ScheduleWindow::~ScheduleWindow()
{
	delete fReply;
}


int64
ScheduleWindow::_Chosen() const
{
	int32 minutes = pp::ParseClock(fTime->Text());
	BMenuItem* day = fDays->FindMarked();
	if (minutes < 0 || day == nullptr)
		return 0;
	return LocalTime(fToday, day->Message()->GetInt32("day", 0), minutes);
}


void
ScheduleWindow::_Check()
{
	std::string problem;
	int64 when = _Chosen();
	if (pp::ParseClock(fTime->Text()) < 0)
		problem = "Type a time, like 21:30 or 9:30 pm.";
	else
		problem = pp::ScheduleProblem(when, time(nullptr));
	fProblem->SetText(problem.c_str());
	fSchedule->SetEnabled(problem.empty());
}


void
ScheduleWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgEdited:
			_Check();
			break;
		case kMsgSchedule:
		{
			_Check();
			if (!fSchedule->IsEnabled())
				break;
			fReply->AddInt64("send_at", _Chosen());
			fTarget.SendMessage(fReply);
			Quit();
			return;
		}
		default:
			BWindow::MessageReceived(message);
	}
}


// #pragma mark - ScheduledWindow


ScheduledWindow::ScheduledWindow(BMessenger app, int64 chatId, const char* chatTitle,
	BRect parentFrame)
	:
	BWindow(BRect(0, 0, 360, 360), BString("Scheduled messages: ") << chatTitle,
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fApp(app),
	fChatId(chatId)
{
	fList = new BListView("scheduled");
	fList->SetSelectionMessage(new BMessage(kMsgSelected));
	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, true);
	float em = be_plain_font->Size();
	scroll->SetExplicitMinSize(BSize(em * 24, em * 16));
	fEmpty = new BStringView("empty", "Loading" B_UTF8_ELLIPSIS);
	fEmpty->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);
	fSendNow = new BButton("send now", "Send now", new BMessage(kMsgSendNow));
	fReschedule = new BButton("reschedule", "Reschedule" B_UTF8_ELLIPSIS, new BMessage(kMsgMove));
	fDelete = new BButton("delete", "Delete", new BMessage(kMsgDelete));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(scroll, 1.0f)
		.Add(fEmpty)
		.AddGroup(B_HORIZONTAL)
			.Add(fDelete)
			.AddGlue()
			.Add(fReschedule)
			.Add(fSendNow)
		.End();
	_Selected();
	CenterIn(parentFrame);
	AddTouchScrolling(this);
}


bool
ScheduledWindow::QuitRequested()
{
	BMessage closed(kMsgScheduledClosed);
	closed.AddInt64("chat_id", fChatId);
	fApp.SendMessage(&closed);
	return true;
}


void
ScheduledWindow::_Selected()
{
	bool any = fList->CurrentSelection() >= 0;
	fSendNow->SetEnabled(any);
	fReschedule->SetEnabled(any);
	fDelete->SetEnabled(any);
}


void
ScheduledWindow::MessageReceived(BMessage* message)
{
	ScheduledItem* item = dynamic_cast<ScheduledItem*>(fList->ItemAt(fList->CurrentSelection()));
	auto act = [&](const char* action, int64 messageId, int64 sendAt) {
		BMessage request(kMsgScheduledAction);
		request.AddInt64("chat_id", fChatId);
		request.AddInt64("message_id", messageId);
		request.AddString("action", action);
		request.AddInt64("send_at", sendAt);
		fApp.SendMessage(&request);
	};
	switch (message->what) {
		case kMsgScheduledList:
		{
			int64 selected = item != nullptr ? item->Id() : 0;
			while (BListItem* old = fList->RemoveItem((int32)0))
				delete old;
			int64 id;
			for (int32 i = 0; message->FindInt64("id", i, &id) == B_OK; i++) {
				fList->AddItem(new ScheduledItem(id, message->GetInt64("send_at", i, 0),
					message->GetString("preview", i, "")));
				if (id == selected)
					fList->Select(i);
			}
			fEmpty->SetText(fList->IsEmpty()
				? "No scheduled messages. Right-click Send to schedule one." : "");
			_Selected();
			break;
		}
		case kMsgSelected:
			_Selected();
			break;
		case kMsgSendNow:
			if (item != nullptr)
				act("now", item->Id(), 0);
			break;
		case kMsgMove:
		{
			if (item == nullptr)
				break;
			BMessage* reply = new BMessage(kMsgMoveTo);
			reply->AddInt64("message_id", item->Id());
			(new ScheduleWindow(BMessenger(this), reply, Frame(), "Reschedule",
				item->SendAt()))->Show();
			break;
		}
		case kMsgMoveTo:
			act("move", message->GetInt64("message_id", 0), message->GetInt64("send_at", 0));
			break;
		case kMsgDelete:
		{
			if (item == nullptr)
				break;
			BAlert* alert = new BAlert("delete", "Delete this scheduled message? It won't "
				"be sent.", "Cancel", "Delete", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			BMessage* confirmed = new BMessage(kMsgDeleteConfirmed);
			confirmed->AddInt64("message_id", item->Id());
			alert->Go(new BInvoker(confirmed, this));
			break;
		}
		case kMsgDeleteConfirmed:
			if (message->GetInt32("which", 0) == 1)
				act("delete", message->GetInt64("message_id", 0), 0);
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


// #pragma mark - PollWindow


PollWindow::PollWindow(BMessenger app, int64 chatId, bool channel, int32 maxOptions,
	BRect parentFrame)
	:
	BWindow(BRect(0, 0, 360, 400), "New poll", B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fApp(app),
	fChatId(chatId),
	fChannel(channel),
	fMaxOptions(maxOptions)
{
	fQuestion = new BTextControl("question", "Question:", "", nullptr);
	fQuestion->SetModificationMessage(new BMessage(kMsgEdited));
	BStringView* answers = new BStringView("answers", "Answers:");
	BGroupView* optionsView = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	fOptionsLayout = optionsView->GroupLayout();
	fAnonymous = new BCheckBox("anonymous", "Anonymous voting", new BMessage(kMsgEdited));
	fAnonymous->SetValue(B_CONTROL_ON);
	fMultiple = new BCheckBox("multiple", "Several answers allowed", new BMessage(kMsgEdited));
	fQuiz = new BCheckBox("quiz", "Quiz: one right answer", new BMessage(kMsgEdited));
	fCorrect = new BPopUpMenu("Choose" B_UTF8_ELLIPSIS);
	fCorrectField = new BMenuField("correct", "Right answer:", fCorrect);
	fExplanation = new BTextControl("explanation", "Explanation:", "", nullptr);
	fExplanation->SetModificationMessage(new BMessage(kMsgEdited));
	fExplanation->SetToolTip("Shown to those who answer wrong (optional)");
	fProblem = new BStringView("problem", "");
	fProblem->SetHighUIColor(B_FAILURE_COLOR);
	fSendButton = new BButton("send", "Send", new BMessage(kMsgSend));
	float em = be_plain_font->Size();
	fQuestion->TextView()->SetExplicitMinSize(BSize(em * 18, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fQuestion)
		.Add(answers)
		.Add(optionsView)
		.Add(fAnonymous)
		.Add(fMultiple)
		.Add(fQuiz)
		.AddGrid(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
			.AddMenuField(fCorrectField, 0, 0)
			.AddTextControl(fExplanation, 0, 1)
		.End()
		.Add(fProblem)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)))
			.Add(fSendButton)
		.End();
	_AddOption();
	_AddOption();
	if (channel) {
		// Channels only take anonymous polls.
		fAnonymous->SetEnabled(false);
	}
	SetDefaultButton(fSendButton);
	_Update();
	fQuestion->MakeFocus(true);
	CenterIn(parentFrame);
}


void
PollWindow::_AddOption()
{
	BString label;
	label << (fOptions.size() + 1) << ".";
	BTextControl* option = new BTextControl("option", label, "", nullptr);
	option->SetModificationMessage(new BMessage(kMsgOptionEdited));
	fOptionsLayout->AddView(option);
	fOptions.push_back(option);
}


void
PollWindow::_Update()
{
	pp::PollDraft poll;
	poll.question = fQuestion->Text();
	for (BTextControl* option : fOptions)
		poll.options.push_back(option->Text());
	poll.anonymous = fAnonymous->Value() == B_CONTROL_ON;
	poll.quiz = fQuiz->Value() == B_CONTROL_ON;
	poll.multipleAnswers = fMultiple->Value() == B_CONTROL_ON;
	poll.explanation = fExplanation->Text();

	// The right answer is chosen among the answers typed so far.
	std::vector<std::string> answers = pp::PollOptions(poll);
	BMenuItem* marked = fCorrect->FindMarked();
	BString chosen = marked != nullptr ? marked->Label() : "";
	while (BMenuItem* item = fCorrect->RemoveItem((int32)0))
		delete item;
	poll.correctOption = -1;
	for (size_t i = 0; i < answers.size(); i++) {
		BMenuItem* item = new BMenuItem(answers[i].c_str(), new BMessage(kMsgEdited));
		fCorrect->AddItem(item);
		if (chosen == answers[i].c_str()) {
			item->SetMarked(true);
			poll.correctOption = (int32)i;
		}
	}
	if (poll.correctOption < 0)
		fCorrectField->MenuItem()->SetLabel("Choose" B_UTF8_ELLIPSIS);
	fCorrectField->SetEnabled(poll.quiz && !answers.empty());
	fExplanation->SetEnabled(poll.quiz);
	fMultiple->SetEnabled(!poll.quiz);

	std::string problem = pp::PollProblem(poll, fMaxOptions, fChannel);
	// Nothing to complain about before anything was typed.
	bool untouched = BString(fQuestion->Text()).Trim().IsEmpty() && answers.empty();
	fProblem->SetText(untouched ? "" : problem.c_str());
	fSendButton->SetEnabled(problem.empty());
}


void
PollWindow::_Send()
{
	_Update();
	if (!fSendButton->IsEnabled())
		return;
	BMessage send(kMsgSendPoll);
	send.AddInt64("chat_id", fChatId);
	send.AddString("question", fQuestion->Text());
	for (BTextControl* option : fOptions)
		send.AddString("option", option->Text());
	send.AddBool("anonymous", fAnonymous->Value() == B_CONTROL_ON);
	send.AddBool("multiple", fMultiple->Value() == B_CONTROL_ON);
	bool quiz = fQuiz->Value() == B_CONTROL_ON;
	send.AddBool("quiz", quiz);
	send.AddInt32("correct", quiz ? fCorrect->IndexOf(fCorrect->FindMarked()) : -1);
	send.AddString("explanation", fExplanation->Text());
	fApp.SendMessage(&send);
	Quit();
}


void
PollWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgOptionEdited:
			// A new empty field once the last one is filled in.
			if (!BString(fOptions.back()->Text()).IsEmpty()
				&& (int32)fOptions.size() < fMaxOptions)
				_AddOption();
			_Update();
			break;
		case kMsgEdited:
			_Update();
			break;
		case kMsgSend:
			_Send();
			return;
		default:
			BWindow::MessageReceived(message);
	}
}


// #pragma mark - LocationWindow


LocationWindow::LocationWindow(BMessenger app, int64 chatId, BRect parentFrame)
	:
	BWindow(BRect(0, 0, 380, 120), "Send location", B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fApp(app),
	fChatId(chatId)
{
	fPlace = new BTextControl("place", "Place:", "", nullptr);
	fPlace->SetModificationMessage(new BMessage(kMsgEdited));
	fPlace->SetToolTip("Coordinates such as 48.8584, 2.2945, or a link copied from "
		"OpenStreetMap, Google Maps, Apple Maps or Bing Maps");
	fResult = new BStringView("result", "");
	fShow = new BButton("show", "Show on map", new BMessage(kMsgShowMap));
	fSendButton = new BButton("send", "Send", new BMessage(kMsgSend));
	float em = be_plain_font->Size();
	fPlace->TextView()->SetExplicitMinSize(BSize(em * 22, B_SIZE_UNSET));
	fResult->SetExplicitMinSize(BSize(em * 24, B_SIZE_UNSET));
	BStringView* hint = new BStringView("hint",
		"Paste coordinates or a link from a map website.");
	hint->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(hint)
		.Add(fPlace)
		.Add(fResult)
		.AddGroup(B_HORIZONTAL)
			.Add(fShow)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)))
			.Add(fSendButton)
		.End();
	SetDefaultButton(fSendButton);
	_Update();
	fPlace->MakeFocus(true);
	CenterIn(parentFrame);
}


bool
LocationWindow::_Parse(double* latitude, double* longitude) const
{
	return pp::ParseLocation(fPlace->Text(), latitude, longitude);
}


void
LocationWindow::_Update()
{
	double latitude, longitude;
	bool ok = _Parse(&latitude, &longitude);
	if (ok) {
		char text[96];
		snprintf(text, sizeof(text), "%.5f\xC2\xB0 %s, %.5f\xC2\xB0 %s", std::fabs(latitude),
			latitude >= 0 ? "N" : "S", std::fabs(longitude), longitude >= 0 ? "E" : "W");
		fResult->SetText(text);
		fResult->SetHighUIColor(B_PANEL_TEXT_COLOR);
	} else {
		fResult->SetText(BString(fPlace->Text()).Trim().IsEmpty() ? ""
			: "No coordinates found in that.");
		fResult->SetHighUIColor(B_FAILURE_COLOR);
	}
	fShow->SetEnabled(ok);
	fSendButton->SetEnabled(ok);
}


void
LocationWindow::MessageReceived(BMessage* message)
{
	double latitude, longitude;
	switch (message->what) {
		case kMsgEdited:
			_Update();
			break;
		case kMsgShowMap:
			if (_Parse(&latitude, &longitude))
				BUrl(pp::MapLink(latitude, longitude).c_str(), true).OpenWithPreferredApplication(false);
			break;
		case kMsgSend:
		{
			if (!_Parse(&latitude, &longitude))
				break;
			BMessage send(kMsgSendLocation);
			send.AddInt64("chat_id", fChatId);
			send.AddDouble("latitude", latitude);
			send.AddDouble("longitude", longitude);
			fApp.SendMessage(&send);
			Quit();
			return;
		}
		default:
			BWindow::MessageReceived(message);
	}
}
