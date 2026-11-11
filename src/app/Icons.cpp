// SPDX-License-Identifier: GPL-3.0-or-later
#include "Icons.h"

#include <Bitmap.h>
#include <Shape.h>
#include <View.h>

#include <algorithm>
#include <cmath>

BBitmap*
MakeIcon(IconKind kind, float size, rgb_color color)
{
	BRect bounds(0, 0, ceilf(size) - 1, ceilf(size) - 1);
	BBitmap* bitmap = new BBitmap(bounds, B_RGBA32, true);
	if (!bitmap->IsValid()) {
		delete bitmap;
		return nullptr;
	}
	BView* view = new BView(bounds, "icon", B_FOLLOW_NONE, B_WILL_DRAW);
	bitmap->AddChild(view);
	bitmap->Lock();

	// Transparent background, antialiased strokes.
	view->SetDrawingMode(B_OP_COPY);
	view->SetHighColor(0, 0, 0, 0);
	view->FillRect(bounds);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
	view->SetHighColor(color);
	float s = bounds.Width() + 1;
	float pen = std::max(1.5f, roundf(s / 12));
	view->SetPenSize(pen);
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	switch (kind) {
		case kIconAttach:
		{
			// A paper clip: two nested U shapes, slanted.
			BShape clip;
			float w = s * 0.18f;
			clip.MoveTo(BPoint(s * 0.62f, s * 0.30f));
			clip.LineTo(BPoint(s * 0.62f, s * 0.70f));
			clip.BezierTo(BPoint(s * 0.62f, s * 0.92f), BPoint(s * 0.26f, s * 0.92f),
				BPoint(s * 0.26f, s * 0.70f));
			clip.LineTo(BPoint(s * 0.26f, s * 0.22f));
			clip.BezierTo(BPoint(s * 0.26f, s * 0.04f), BPoint(s * 0.26f + 2 * w + s * 0.08f, s * 0.04f),
				BPoint(s * 0.26f + 2 * w + s * 0.08f, s * 0.22f));
			clip.LineTo(BPoint(s * 0.26f + 2 * w + s * 0.08f, s * 0.66f));
			clip.BezierTo(BPoint(s * 0.26f + 2 * w + s * 0.08f, s * 0.78f), BPoint(s * 0.44f, s * 0.78f),
				BPoint(s * 0.44f, s * 0.66f));
			clip.LineTo(BPoint(s * 0.44f, s * 0.34f));
			view->StrokeShape(&clip);
			break;
		}
		case kIconEmoji:
		{
			BRect face(pen, pen, s - pen - 1, s - pen - 1);
			view->StrokeEllipse(face);
			float eye = std::max(1.5f, s * 0.07f);
			view->FillEllipse(BPoint(s * 0.36f, s * 0.40f), eye, eye);
			view->FillEllipse(BPoint(s * 0.64f, s * 0.40f), eye, eye);
			view->StrokeArc(BRect(s * 0.28f, s * 0.30f, s * 0.72f, s * 0.72f), 200, 140);
			break;
		}
		case kIconSticker:
		{
			// A rounded square whose bottom right corner is folded over.
			float m = pen + 0.5f, r = s * 0.18f, fold = s * 0.34f;
			BShape outline;
			outline.MoveTo(BPoint(m + r, m));
			outline.LineTo(BPoint(s - m - r, m));
			outline.BezierTo(BPoint(s - m, m), BPoint(s - m, m), BPoint(s - m, m + r));
			outline.LineTo(BPoint(s - m, s - m - fold));
			outline.LineTo(BPoint(s - m - fold, s - m));
			outline.LineTo(BPoint(m + r, s - m));
			outline.BezierTo(BPoint(m, s - m), BPoint(m, s - m), BPoint(m, s - m - r));
			outline.LineTo(BPoint(m, m + r));
			outline.BezierTo(BPoint(m, m), BPoint(m, m), BPoint(m + r, m));
			outline.Close();
			view->StrokeShape(&outline);
			BShape peel;
			peel.MoveTo(BPoint(s - m, s - m - fold));
			peel.BezierTo(BPoint(s - m - fold * 0.9f, s - m - fold),
				BPoint(s - m - fold, s - m - fold * 0.9f), BPoint(s - m - fold, s - m));
			view->StrokeShape(&peel);
			break;
		}
	}
	view->Sync();
	bitmap->Unlock();
	bitmap->RemoveChild(view);
	delete view;
	return bitmap;
}
