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
	// A fractional pen renders fine (antialiased, like every stroke here);
	// rounding it to the nearest pixel is what used to make the stroke grow
	// slower than the icon itself at a bigger UI scale.
	float pen = std::max(1.5f, s / 12.0f);
	view->SetPenSize(pen);
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	// _IconButton sizes these from the plain font's height, so this is the
	// small, un-scaled size below and a 2x (or bigger) RenkuOS UI scale
	// above it. The small paths are kept exactly as they always were --
	// nothing here needed fixing below this size, and the large paths'
	// own numbers (several of them literal, eyeballed pixel nudges) were
	// only ever checked against a real retina screen at the bigger size,
	// not derived to hold at every size.
	bool large = s >= 30.0f;

	switch (kind) {
		case kIconAttach:
		{
			if (!large) {
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
			// A paper clip: two nested closed loops (StrokeRoundRect, not
			// an open hook -- an open top read as a comb, not a clip),
			// stood vertical. The inner loop's edges stay clear of the
			// outer loop's at every height -- well past the pen, at most
			// s / 12 -- so the two strokes read as nested loops instead
			// of one blob, even where they cross.
			float outerW = s * 0.34f + 2, innerW = s * 0.12f;
			BRect outer(s * 0.26f, s * 0.12f, s * 0.26f + outerW, s * 0.82f);
			BRect inner(s * 0.38f + 1, s * 0.16f, s * 0.38f + 1 + innerW, s * 0.58f);
			view->StrokeRoundRect(outer, outerW / 2, outerW / 2);
			view->StrokeRoundRect(inner, innerW / 2, innerW / 2);
			break;
		}
		case kIconEmoji:
		{
			BRect face(pen, pen, s - pen - 1, s - pen - 1);
			view->StrokeEllipse(face);
			if (!large) {
				float eye = std::max(1.5f, s * 0.07f);
				view->FillEllipse(BPoint(s * 0.36f, s * 0.40f), eye, eye);
				view->FillEllipse(BPoint(s * 0.64f, s * 0.40f), eye, eye);
				view->StrokeArc(BRect(s * 0.28f, s * 0.30f, s * 0.72f, s * 0.72f), 200, 140);
				break;
			}
			// Every other shape is placed from this one centre, computed
			// from the same rect the face itself was stroked from -- not
			// a second, independently-guessed point -- so there is
			// nothing left to go asymmetric between the eyes or around
			// the mouth.
			float cx = (face.left + face.right) / 2.0f;
			float cy = (face.top + face.bottom) / 2.0f;
			float eye = std::max(1.5f, s * 0.05f);
			float eyeOffset = s * 0.14f;
			float eyeY = cy - s * 0.12f;
			view->FillEllipse(BPoint(cx - eyeOffset, eyeY), eye, eye);
			view->FillEllipse(BPoint(cx + eyeOffset - 1, eyeY), eye, eye);
			float mouth = s * 0.42f;
			float mouthCx = cx - 1;
			float mouthCy = cy + s * 0.02f - 1;
			view->StrokeArc(BRect(mouthCx - mouth / 2, mouthCy - mouth / 2,
				mouthCx + mouth / 2, mouthCy + mouth / 2), 200, 140);
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
		case kIconMic:
		{
			if (!large) {
				// A capsule on a stand: the mic, the cradle around it, the stem.
				float w = s * 0.26f;
				BRect capsule(s / 2 - w / 2, s * 0.10f, s / 2 + w / 2, s * 0.58f);
				view->StrokeRoundRect(capsule, w / 2, w / 2);
				view->StrokeArc(BRect(s * 0.24f, s * 0.22f, s * 0.76f, s * 0.70f), 180, 180);
				view->StrokeLine(BPoint(s / 2, s * 0.70f), BPoint(s / 2, s * 0.86f));
				view->StrokeLine(BPoint(s * 0.36f, s * 0.86f), BPoint(s * 0.64f, s * 0.86f));
				break;
			}
			// A capsule for the mic head, a wider cradle wrapping around
			// its lower half, a stem and a base -- the usual mic glyph.
			// The cradle overlaps the capsule's height on purpose (that
			// is what "wraps around" means), but its radius is wide
			// enough that it never gets near the capsule's own edge,
			// well past the pen, at most s / 12.
			float cx = s * 0.5f;
			float w = s * 0.18f;
			float capsuleTop = s * 0.10f - 1, capsuleBottom = s * 0.52f - 1;
			BRect capsule(cx - w / 2, capsuleTop, cx + w / 2, capsuleBottom);
			view->StrokeRoundRect(capsule, w / 2, w / 2);
			// A square bounding box, so the arc is a true semicircle.
			float cradleR = s * 0.20f + 3;
			float cradleCy = capsuleBottom - s * 0.06f;
			BRect cradleRect(cx - cradleR, cradleCy - cradleR, cx + cradleR, cradleCy + cradleR);
			view->StrokeArc(cradleRect, 180, 180);
			view->StrokeLine(BPoint(cx, cradleRect.bottom), BPoint(cx, s * 0.86f));
			view->StrokeLine(BPoint(cx - s * 0.14f, s * 0.86f), BPoint(cx + s * 0.14f, s * 0.86f));
			break;
		}
	}
	view->Sync();
	bitmap->Unlock();
	bitmap->RemoveChild(view);
	delete view;
	return bitmap;
}
