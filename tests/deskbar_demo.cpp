// SPDX-License-Identifier: GPL-3.0-or-later
// Stands in for the app (same signature; don't run it with Powergram
// open): adds the Deskbar item, answers its hello with a count, hands it a
// drop of files as Tracker would and prints what comes back, then removes
// the item after `seconds`.
//   deskbar_demo <add-on> <unread> <seconds>
#include <Application.h>
#include <Deskbar.h>
#include <Entry.h>
#include <MessageRunner.h>

#include <cstdio>
#include <cstdlib>

#include "../src/app/Messages.h"

class Demo : public BApplication {
public:
	Demo(const char* addOn, int32 unread, int32 seconds)
		: BApplication(APP_SIGNATURE), fAddOn(addOn), fUnread(unread), fSeconds(seconds) {}

	void ReadyToRun() override
	{
		entry_ref ref;
		get_ref_for_path(fAddOn, &ref);
		BDeskbar deskbar;
		if (deskbar.HasItem(kDeskbarItemName))
			deskbar.RemoveItem(kDeskbarItemName);
		status_t status = deskbar.AddItem(&ref);
		printf("AddItem: %s; HasItem: %d\n", strerror(status), deskbar.HasItem(kDeskbarItemName));
		fflush(stdout);
		BMessage quit(B_QUIT_REQUESTED);
		BMessageRunner::StartSending(this, &quit, fSeconds * 1000000LL, 1);
	}

	void MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case kMsgDeskbarHello:
			{
				BMessenger item;
				message->FindMessenger("item", &item);
				printf("hello from the item (valid %d)\n", item.IsValid());
				BMessage count(kMsgDeskbarUnread);
				count.AddInt32("unread", fUnread);
				item.SendMessage(&count);
				// A drop, as Tracker sends one.
				BMessage drop(B_SIMPLE_DATA);
				entry_ref ref;
				get_ref_for_path("/boot/home/paper_plane/tests/data/photo.png", &ref);
				drop.AddRef("refs", &ref);
				item.SendMessage(&drop);
				fflush(stdout);
				break;
			}
			case kMsgDeskbarFiles:
			{
				entry_ref ref;
				for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++)
					printf("files dropped: %s\n", ref.name);
				fflush(stdout);
				break;
			}
			default:
				BApplication::MessageReceived(message);
		}
	}

	bool QuitRequested() override
	{
		BDeskbar().RemoveItem(kDeskbarItemName);
		printf("removed; HasItem: %d\n", BDeskbar().HasItem(kDeskbarItemName));
		return true;
	}

private:
	const char* fAddOn;
	int32 fUnread;
	int32 fSeconds;
};


int
main(int argc, char** argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: deskbar_demo <add-on> <unread> <seconds>\n");
		return 2;
	}
	Demo demo(argv[1], atoi(argv[2]), atoi(argv[3]));
	demo.Run();
	return 0;
}
