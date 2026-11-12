// SPDX-License-Identifier: GPL-3.0-or-later
#include "LottieRenderer.h"

#include <Bitmap.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <Shape.h>
#include <View.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

rgb_color
Color(float r, float g, float b, float a)
{
	auto byte = [](float v) { return (uint8)std::clamp((int)lroundf(v * 255), 0, 255); };
	return make_color(byte(r), byte(g), byte(b), byte(a));
}


void
Clear(BBitmap* bitmap)
{
	memset(bitmap->Bits(), 0, bitmap->BitsLength());
}

} // namespace


// A bitmap with a view to draw into it, set up to blend onto transparency.
struct LottieRenderer::Canvas {
	BBitmap* bitmap;
	BView* view;

	Canvas(int32 width, int32 height)
	{
		bitmap = new BBitmap(BRect(0, 0, width - 1, height - 1), B_BITMAP_ACCEPTS_VIEWS,
			B_RGBA32);
		view = new BView(bitmap->Bounds(), "lottie", B_FOLLOW_NONE, 0);
		bitmap->AddChild(view);
		bitmap->Lock();
		view->SetDrawingMode(B_OP_ALPHA);
		// Composite: drawing on a transparent bitmap keeps what's under the
		// shape and builds up the alpha.
		view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
		bitmap->Unlock();
	}

	~Canvas()
	{
		delete bitmap;   // and the view with it
	}
};


LottieRenderer::LottieRenderer(int32 width, int32 height)
	:
	fWidth(std::max<int32>(width, 1)),
	fHeight(std::max<int32>(height, 1))
{
}


LottieRenderer::~LottieRenderer()
{
}


LottieRenderer::Canvas*
LottieRenderer::_Canvas(size_t depth)
{
	while (fCanvases.size() <= depth)
		fCanvases.emplace_back(new Canvas(fWidth, fHeight));
	return fCanvases[depth].get();
}


const BBitmap*
LottieRenderer::Draw(const pp::LottieList& frame)
{
	Canvas* canvas = _Canvas(0);
	canvas->bitmap->Lock();
	canvas->view->Sync();
	Clear(canvas->bitmap);
	_DrawList(canvas, frame, 0);
	canvas->view->Sync();
	canvas->bitmap->Unlock();
	return canvas->bitmap;
}


void
LottieRenderer::_DrawList(Canvas* canvas, const pp::LottieList& list, size_t depth)
{
	BView* view = canvas->view;
	for (const pp::LottieItem& item : list) {
		if (item.matte != pp::LottieItem::kNoMatte && item.content && item.matteSource
			&& depth < 8) {
			// The content and the matte each in a bitmap of their own, the
			// matte's alpha (or luminance) applied, then drawn here.
			view->Sync();
			Canvas* content = _Canvas(depth * 2 + 1);
			Canvas* matte = _Canvas(depth * 2 + 2);
			for (Canvas* c : {content, matte}) {
				if (c->bitmap->Lock()) {
					c->view->Sync();
					Clear(c->bitmap);
					c->bitmap->Unlock();
				}
			}
			content->bitmap->Lock();
			_DrawList(content, *item.content, depth * 2 + 1);
			content->view->Sync();
			content->bitmap->Unlock();
			matte->bitmap->Lock();
			_DrawList(matte, *item.matteSource, depth * 2 + 2);
			matte->view->Sync();
			matte->bitmap->Unlock();

			uint8* c = (uint8*)content->bitmap->Bits();
			const uint8* m = (const uint8*)matte->bitmap->Bits();
			int32 rowBytes = content->bitmap->BytesPerRow();
			bool luma = item.matte == pp::LottieItem::kLuma
				|| item.matte == pp::LottieItem::kLumaInverted;
			bool inverted = item.matte == pp::LottieItem::kAlphaInverted
				|| item.matte == pp::LottieItem::kLumaInverted;
			for (int32 y = 0; y < fHeight; y++) {
				uint8* cp = c + y * rowBytes;
				const uint8* mp = m + y * rowBytes;
				for (int32 x = 0; x < fWidth; x++, cp += 4, mp += 4) {
					// B_RGBA32 is B, G, R, A in memory.
					int32 amount = mp[3];
					if (luma)
						amount = (mp[2] * 77 + mp[1] * 150 + mp[0] * 29) / 256 * mp[3] / 255;
					if (inverted)
						amount = 255 - amount;
					cp[3] = (uint8)(cp[3] * amount / 255);
				}
			}
			view->DrawBitmap(content->bitmap, BPoint(0, 0));
			continue;
		}

		const pp::LottieShape& shape = item.shape;
		BShape path;
		bool any = false;
		for (size_t i = 0; i < shape.contours.size(); i++) {
			const std::vector<pp::LottiePoint>& points = shape.contours[i];
			if (points.size() < 2)
				continue;
			path.MoveTo(BPoint(points[0].x, points[0].y));
			for (size_t k = 1; k < points.size(); k++)
				path.LineTo(BPoint(points[k].x, points[k].y));
			if (i < shape.closed.size() && shape.closed[i])
				path.Close();
			any = true;
		}
		if (!any)
			continue;
		const pp::LottiePaint& paint = shape.paint;
		if (paint.kind == pp::LottiePaint::kStroke) {
			view->SetHighColor(Color(paint.r, paint.g, paint.b, paint.a));
			view->SetPenSize(paint.width);
			view->SetLineMode(paint.cap == pp::LottiePaint::kRound ? B_ROUND_CAP
					: paint.cap == pp::LottiePaint::kSquare ? B_SQUARE_CAP : B_BUTT_CAP,
				paint.join == pp::LottiePaint::kRoundJoin ? B_ROUND_JOIN
					: paint.join == pp::LottiePaint::kBevel ? B_BEVEL_JOIN : B_MITER_JOIN,
				paint.miterLimit);
			view->StrokeShape(&path);
			continue;
		}
		view->SetFillRule(paint.evenOdd ? B_EVEN_ODD : B_NONZERO);
		if (paint.gradient == pp::LottiePaint::kNone) {
			view->SetHighColor(Color(paint.r, paint.g, paint.b, paint.a));
			view->FillShape(&path);
			continue;
		}
		BGradientLinear linear(BPoint(paint.start.x, paint.start.y),
			BPoint(paint.end.x, paint.end.y));
		float radius = std::hypot(paint.end.x - paint.start.x, paint.end.y - paint.start.y);
		BGradientRadial radial(BPoint(paint.start.x, paint.start.y), std::max(radius, 0.5f));
		BGradient& gradient = paint.gradient == pp::LottiePaint::kRadial
			? (BGradient&)radial : (BGradient&)linear;
		for (const pp::LottiePaint::Stop& stop : paint.stops) {
			gradient.AddColor(Color(stop.r, stop.g, stop.b, stop.a),
				std::clamp(stop.offset, 0.0f, 1.0f) * 255);
		}
		view->FillShape(&path, gradient);
	}
}
