// SPDX-License-Identifier: GPL-3.0-or-later
// Prints every field of the mouse messages a window gets, to see what a
// touch screen (or a VM's tablet pointer) sends: touch_probe, then touch,
// click and drag in its window; Alt+Q quits.
#include <Application.h>
#include <Message.h>
#include <View.h>
#include <Window.h>

#include <cstdio>

class ProbeView : public BView {
public:
	ProbeView(BRect frame) : BView(frame, "probe", B_FOLLOW_ALL, B_WILL_DRAW) {}

	void Draw(BRect) override
	{
		DrawString("Touch, click or drag here; the fields print in Terminal.", BPoint(10, 20));
	}
};

class ProbeWindow : public BWindow {
public:
	ProbeWindow()
		: BWindow(BRect(100, 100, 600, 400), "Touch probe", B_TITLED_WINDOW,
			B_QUIT_ON_WINDOW_CLOSE)
	{
		AddChild(new ProbeView(Bounds()));
	}

	void DispatchMessage(BMessage* message, BHandler* target) override
	{
		if (message->what == B_MOUSE_DOWN || message->what == B_MOUSE_UP
			|| message->what == B_MOUSE_MOVED || message->what == B_MOUSE_WHEEL_CHANGED) {
			printf("%s:", message->what == B_MOUSE_DOWN ? "down" : message->what == B_MOUSE_UP
				? "up" : message->what == B_MOUSE_MOVED ? "moved" : "wheel");
			char* name;
			type_code type;
			int32 count;
			for (int32 i = 0; message->GetInfo(B_ANY_TYPE, i, &name, &type, &count) == B_OK; i++) {
				printf(" %s=", name);
				if (type == B_INT32_TYPE)
					printf("%d", (int)message->GetInt32(name, 0));
				else if (type == B_INT64_TYPE)
					printf("%lld", (long long)message->GetInt64(name, 0));
				else if (type == B_FLOAT_TYPE)
					printf("%.3f", message->GetFloat(name, 0));
				else if (type == B_BOOL_TYPE)
					printf("%d", message->GetBool(name, false));
				else if (type == B_POINT_TYPE) {
					BPoint p = message->GetPoint(name, BPoint());
					printf("(%.1f,%.1f)", p.x, p.y);
				} else
					printf("<%.4s>", (char*)&type);
			}
			printf("\n");
			fflush(stdout);
		}
		BWindow::DispatchMessage(message, target);
	}
};

int
main()
{
	BApplication app("application/x-vnd.Powergram-touch-probe");
	(new ProbeWindow)->Show();
	app.Run();
	return 0;
}
