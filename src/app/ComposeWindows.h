// SPDX-License-Identifier: GPL-3.0-or-later
// Windows for the extra ways to send: choosing when a message goes, the
// chat's scheduled messages, a poll or quiz, a location. They talk to the
// app (or the window that opened them) by messages only (Messages.h).
#pragma once

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <vector>

class BButton;
class BCheckBox;
class BGroupLayout;
class BListView;
class BMenuField;
class BPopUpMenu;
class BStringView;
class BTextControl;

// "Schedule message": a day and a time. Sends `reply` to `target` with
// "send_at" (int64, unix time) added.
class ScheduleWindow : public BWindow {
public:
	// `initial`: the time to start from, 0 for an hour from now.
	ScheduleWindow(BMessenger target, BMessage* reply, BRect parentFrame,
		const char* title = "Schedule message", int64 initial = 0);
	~ScheduleWindow() override;

	void MessageReceived(BMessage* message) override;

private:
	int64 _Chosen() const;
	void _Check();

	BMessenger fTarget;
	BMessage* fReply;
	BPopUpMenu* fDays;
	BTextControl* fTime;
	BStringView* fProblem;
	BButton* fSchedule;
	int64 fToday;     // midnight today, local time
};

// A chat's scheduled messages: send one now, move it, or delete it.
class ScheduledWindow : public BWindow {
public:
	ScheduledWindow(BMessenger app, int64 chatId, const char* chatTitle, BRect parentFrame);

	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;
	int64 ChatId() const { return fChatId; }

private:
	void _Selected();

	BMessenger fApp;
	int64 fChatId;
	BListView* fList;
	BStringView* fEmpty;
	BButton* fSendNow;
	BButton* fReschedule;
	BButton* fDelete;
};

// A poll or quiz: the question, the answers (a new empty field appears as
// the last one is filled), and how people vote.
class PollWindow : public BWindow {
public:
	PollWindow(BMessenger app, int64 chatId, bool channel, int32 maxOptions,
		BRect parentFrame);

	void MessageReceived(BMessage* message) override;

private:
	void _AddOption();
	void _Update();
	void _Send();

	BMessenger fApp;
	int64 fChatId;
	bool fChannel;
	int32 fMaxOptions;
	BTextControl* fQuestion;
	BGroupLayout* fOptionsLayout;
	std::vector<BTextControl*> fOptions;
	BCheckBox* fAnonymous;
	BCheckBox* fMultiple;
	BCheckBox* fQuiz;
	BPopUpMenu* fCorrect;
	BMenuField* fCorrectField;
	BTextControl* fExplanation;
	BStringView* fProblem;
	BButton* fSendButton;
};

// A location: coordinates or a map link, checked as they're typed.
class LocationWindow : public BWindow {
public:
	LocationWindow(BMessenger app, int64 chatId, BRect parentFrame);

	void MessageReceived(BMessage* message) override;

private:
	bool _Parse(double* latitude, double* longitude) const;
	void _Update();

	BMessenger fApp;
	int64 fChatId;
	BTextControl* fPlace;
	BStringView* fResult;
	BButton* fShow;
	BButton* fSendButton;
};
