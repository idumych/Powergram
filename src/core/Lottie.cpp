// SPDX-License-Identifier: GPL-3.0-or-later
#include "Lottie.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace pp {

namespace {

const float kPi = 3.14159265358979f;
// Control points for a quarter circle drawn with one cubic bezier.
const float kKappa = 0.5522848f;


// A number field, `fallback` when missing or not a number.
float
Float(const json& object, const char* key, float fallback)
{
	const json& v = Field(object, key);
	return v.is_number() ? v.get<float>() : fallback;
}


// #pragma mark - Animated values


struct Keyframe {
	float t = 0;
	std::vector<float> s, e;
	bool hasEnd = false;
	bool hold = false;
	std::vector<float> ix, iy, ox, oy;   // easing, per dimension or one for all
	std::vector<float> to, ti;           // spatial tangents (positions)
};

// A value that may change over time: a number, a point, a colour, or a
// path (packed as numbers, see PackPath).
struct Prop {
	std::vector<float> value;
	std::vector<Keyframe> keys;
	bool present = false;

	bool Animated() const { return !keys.empty(); }
};


std::vector<float>
Numbers(const json& value)
{
	std::vector<float> out;
	if (value.is_number())
		out.push_back(value.get<float>());
	else if (value.is_array()) {
		for (const json& v : value) {
			if (v.is_number())
				out.push_back(v.get<float>());
		}
	}
	return out;
}


// A path {"v": vertices, "i": in tangents, "o": out tangents, "c": closed}
// as numbers: closed, count, then x, y, in x, in y, out x, out y for each
// vertex. Keyframes then interpolate it like any other value.
std::vector<float>
PackPath(const json& shape)
{
	const json* s = &shape;
	if (shape.is_array() && !shape.empty())
		s = &shape[0];
	std::vector<float> out;
	const json& v = Field(*s, "v");
	const json& in = Field(*s, "i");
	const json& o = Field(*s, "o");
	if (!v.is_array())
		return out;
	out.push_back(Bool(*s, "c") ? 1 : 0);
	out.push_back((float)v.size());
	for (size_t k = 0; k < v.size(); k++) {
		std::vector<float> p = Numbers(v[k]);
		std::vector<float> pi = in.is_array() && k < in.size() ? Numbers(in[k]) : std::vector<float>();
		std::vector<float> po = o.is_array() && k < o.size() ? Numbers(o[k]) : std::vector<float>();
		p.resize(2);
		pi.resize(2);
		po.resize(2);
		out.insert(out.end(), {p[0], p[1], pi[0], pi[1], po[0], po[1]});
	}
	return out;
}


Prop
ParseProp(const json& object, bool path = false)
{
	Prop prop;
	if (!object.is_object())
		return prop;
	const json& k = Field(object, "k");
	if (k.is_null())
		return prop;
	prop.present = true;
	bool keyframes = k.is_array() && !k.empty() && k[0].is_object() && k[0].contains("t");
	if (!keyframes) {
		prop.value = path ? PackPath(k) : Numbers(k);
		return prop;
	}
	for (const json& key : k) {
		Keyframe frame;
		frame.t = Float(key, "t", 0.0f);
		const json& s = Field(key, "s");
		const json& e = Field(key, "e");
		if (!s.is_null())
			frame.s = path ? PackPath(s) : Numbers(s);
		if (!e.is_null()) {
			frame.e = path ? PackPath(e) : Numbers(e);
			frame.hasEnd = true;
		}
		frame.hold = Int(key, "h") == 1;
		frame.ix = Numbers(Field(Field(key, "i"), "x"));
		frame.iy = Numbers(Field(Field(key, "i"), "y"));
		frame.ox = Numbers(Field(Field(key, "o"), "x"));
		frame.oy = Numbers(Field(Field(key, "o"), "y"));
		frame.to = Numbers(Field(key, "to"));
		frame.ti = Numbers(Field(key, "ti"));
		prop.keys.push_back(frame);
	}
	// Old files end with a keyframe that only has a time: it holds the
	// previous one's end value.
	for (size_t i = 0; i < prop.keys.size(); i++) {
		if (!prop.keys[i].s.empty())
			continue;
		if (i > 0)
			prop.keys[i].s = prop.keys[i - 1].hasEnd ? prop.keys[i - 1].e : prop.keys[i - 1].s;
	}
	return prop;
}


float
Component(const std::vector<float>& values, size_t index, float fallback)
{
	if (values.empty())
		return fallback;
	return index < values.size() ? values[index] : values[0];
}


std::vector<float>
Evaluate(const Prop& prop, float t)
{
	if (!prop.Animated())
		return prop.value;
	const std::vector<Keyframe>& keys = prop.keys;
	if (t <= keys.front().t || keys.size() == 1)
		return keys.front().s;
	size_t i = 0;
	while (i + 1 < keys.size() && keys[i + 1].t <= t)
		i++;
	const Keyframe& a = keys[i];
	if (i + 1 >= keys.size())
		return a.hasEnd && !a.hold ? a.e : a.s;
	const Keyframe& b = keys[i + 1];
	const std::vector<float>& from = a.s;
	const std::vector<float>& to = a.hasEnd ? a.e : b.s;
	if (a.hold || to.size() != from.size())
		return from;
	float span = b.t - a.t;
	float progress = span > 0 ? (t - a.t) / span : 1;
	std::vector<float> out(from.size());
	// Positions moving along a curve: one easing for the whole point.
	bool spatial = from.size() >= 2 && from.size() <= 3 && a.to.size() >= 2 && a.ti.size() >= 2
		&& (a.to[0] != 0 || a.to[1] != 0 || a.ti[0] != 0 || a.ti[1] != 0);
	if (spatial) {
		float p = BezierEase(Component(a.ox, 0, 0), Component(a.oy, 0, 0),
			Component(a.ix, 0, 1), Component(a.iy, 0, 1), progress);
		for (size_t d = 0; d < 2; d++) {
			float p0 = from[d], p1 = from[d] + a.to[d], p2 = to[d] + a.ti[d], p3 = to[d];
			float u = 1 - p;
			out[d] = u * u * u * p0 + 3 * u * u * p * p1 + 3 * u * p * p * p2 + p * p * p * p3;
		}
		for (size_t d = 2; d < from.size(); d++)
			out[d] = from[d] + (to[d] - from[d]) * p;
		return out;
	}
	bool one = a.ox.size() <= 1;
	float shared = one ? BezierEase(Component(a.ox, 0, 0), Component(a.oy, 0, 0),
		Component(a.ix, 0, 1), Component(a.iy, 0, 1), progress) : 0;
	for (size_t d = 0; d < from.size(); d++) {
		float p = one ? shared : BezierEase(Component(a.ox, d, 0), Component(a.oy, d, 0),
			Component(a.ix, d, 1), Component(a.iy, d, 1), progress);
		out[d] = from[d] + (to[d] - from[d]) * p;
	}
	return out;
}


float
EvaluateNumber(const Prop& prop, float t, float fallback)
{
	std::vector<float> v = Evaluate(prop, t);
	return v.empty() ? fallback : v[0];
}


// #pragma mark - Geometry


struct Matrix {
	float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;

	LottiePoint Apply(float x, float y) const
	{
		return {a * x + c * y + tx, b * x + d * y + ty};
	}
	// this × other: other is applied first.
	Matrix operator*(const Matrix& o) const
	{
		Matrix m;
		m.a = a * o.a + c * o.b;
		m.b = b * o.a + d * o.b;
		m.c = a * o.c + c * o.d;
		m.d = b * o.c + d * o.d;
		m.tx = a * o.tx + c * o.ty + tx;
		m.ty = b * o.tx + d * o.ty + ty;
		return m;
	}
	float Scale() const { return std::sqrt(std::fabs(a * d - b * c)); }
};


struct Transform {
	Prop anchor, position, positionX, positionY, scale, rotation, opacity;
	bool splitPosition = false;

	void Parse(const json& ks)
	{
		anchor = ParseProp(Field(ks, "a"));
		const json& p = Field(ks, "p");
		if (Bool(p, "s")) {
			splitPosition = true;
			positionX = ParseProp(Field(p, "x"));
			positionY = ParseProp(Field(p, "y"));
		} else
			position = ParseProp(p);
		scale = ParseProp(Field(ks, "s"));
		rotation = ParseProp(Field(ks, "r"));
		if (!rotation.present)
			rotation = ParseProp(Field(ks, "rz"));
		opacity = ParseProp(Field(ks, "o"));
	}

	Matrix At(float t) const
	{
		std::vector<float> a = Evaluate(anchor, t);
		std::vector<float> p;
		if (splitPosition)
			p = {EvaluateNumber(positionX, t, 0), EvaluateNumber(positionY, t, 0)};
		else
			p = Evaluate(position, t);
		std::vector<float> s = Evaluate(scale, t);
		float r = EvaluateNumber(rotation, t, 0) * kPi / 180;
		a.resize(2, 0);
		p.resize(2, 0);
		if (s.empty())
			s = {100, 100};
		s.resize(2, s[0]);
		// position × rotation × scale × −anchor
		float cs = std::cos(r), sn = std::sin(r);
		float sx = s[0] / 100, sy = s[1] / 100;
		Matrix m;
		m.a = cs * sx;
		m.b = sn * sx;
		m.c = -sn * sy;
		m.d = cs * sy;
		m.tx = p[0] - (m.a * a[0] + m.c * a[1]);
		m.ty = p[1] - (m.b * a[0] + m.d * a[1]);
		return m;
	}

	float Opacity(float t) const { return EvaluateNumber(opacity, t, 100) / 100; }
};


struct Contour {
	std::vector<LottiePoint> points;
	bool closed = false;
};


// Adds the cubic bezier p0..p3 (already transformed) to `out`, in enough
// pieces that each is about a pixel or two long.
void
FlattenCubic(LottiePoint p0, LottiePoint p1, LottiePoint p2, LottiePoint p3,
	std::vector<LottiePoint>& out)
{
	float length = std::hypot(p1.x - p0.x, p1.y - p0.y) + std::hypot(p2.x - p1.x, p2.y - p1.y)
		+ std::hypot(p3.x - p2.x, p3.y - p2.y);
	bool straight = p1.x == p0.x && p1.y == p0.y && p2.x == p3.x && p2.y == p3.y;
	int steps = straight ? 1 : std::clamp((int)(length / 2), 2, 48);
	for (int i = 1; i <= steps; i++) {
		float t = (float)i / steps, u = 1 - t;
		out.push_back({u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x
				+ t * t * t * p3.x,
			u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y});
	}
}


// A packed path (see PackPath), transformed and flattened.
Contour
FlattenPath(const std::vector<float>& packed, const Matrix& m)
{
	Contour contour;
	if (packed.size() < 2)
		return contour;
	contour.closed = packed[0] > 0.5f;
	size_t count = (size_t)packed[1];
	if (packed.size() < 2 + count * 6 || count == 0)
		return contour;
	auto vertex = [&](size_t k, int field) {
		const float* v = &packed[2 + k * 6];
		float x = v[0] + (field == 1 ? v[2] : field == 2 ? v[4] : 0);
		float y = v[1] + (field == 1 ? v[3] : field == 2 ? v[5] : 0);
		return m.Apply(x, y);
	};
	contour.points.push_back(vertex(0, 0));
	size_t segments = contour.closed ? count : count - 1;
	for (size_t k = 0; k < segments; k++) {
		size_t next = (k + 1) % count;
		FlattenCubic(vertex(k, 0), vertex(k, 2), vertex(next, 1), vertex(next, 0),
			contour.points);
	}
	if (contour.closed && contour.points.size() > 1) {
		// The closing segment ends where it started.
		contour.points.pop_back();
	}
	return contour;
}


// A path for a rectangle or ellipse, packed like PackPath's, clockwise
// from the top (as After Effects draws them).
std::vector<float>
RectPath(float cx, float cy, float w, float h, float radius)
{
	float l = cx - w / 2, r = cx + w / 2, t = cy - h / 2, b = cy + h / 2;
	radius = std::min(radius, std::min(w, h) / 2);
	std::vector<float> out = {1};
	auto add = [&](float x, float y, float ix, float iy, float ox, float oy) {
		out.insert(out.end(), {x, y, ix, iy, ox, oy});
	};
	if (radius <= 0) {
		out.push_back(4);
		add(r, t, 0, 0, 0, 0);
		add(r, b, 0, 0, 0, 0);
		add(l, b, 0, 0, 0, 0);
		add(l, t, 0, 0, 0, 0);
		return out;
	}
	float k = radius * kKappa;
	out.push_back(8);
	add(r - radius, t, 0, 0, k, 0);
	add(r, t + radius, 0, -k, 0, 0);
	add(r, b - radius, 0, 0, 0, k);
	add(r - radius, b, k, 0, 0, 0);
	add(l + radius, b, 0, 0, -k, 0);
	add(l, b - radius, 0, k, 0, 0);
	add(l, t + radius, 0, 0, 0, -k);
	add(l + radius, t, -k, 0, 0, 0);
	return out;
}


std::vector<float>
EllipsePath(float cx, float cy, float w, float h)
{
	float rx = w / 2, ry = h / 2, kx = rx * kKappa, ky = ry * kKappa;
	return {1, 4,
		cx, cy - ry, -kx, 0, kx, 0,
		cx + rx, cy, 0, -ky, 0, ky,
		cx, cy + ry, kx, 0, -kx, 0,
		cx - rx, cy, 0, ky, 0, -ky};
}


float
ContourLength(const Contour& c)
{
	float length = 0;
	for (size_t i = 1; i < c.points.size(); i++)
		length += std::hypot(c.points[i].x - c.points[i - 1].x, c.points[i].y - c.points[i - 1].y);
	if (c.closed && c.points.size() > 1)
		length += std::hypot(c.points[0].x - c.points.back().x, c.points[0].y - c.points.back().y);
	return length;
}


// The part of a contour from `from` to `to` (distances along it, from <=
// to, both within its length), as an open polyline.
std::vector<LottiePoint>
SubContour(const Contour& c, float from, float to)
{
	std::vector<LottiePoint> points = c.points;
	if (c.closed && !points.empty())
		points.push_back(points[0]);
	std::vector<LottiePoint> out;
	float walked = 0;
	for (size_t i = 1; i < points.size(); i++) {
		LottiePoint a = points[i - 1], b = points[i];
		float length = std::hypot(b.x - a.x, b.y - a.y);
		float start = walked, end = walked + length;
		walked = end;
		if (end < from || start > to || length <= 0)
			continue;
		auto at = [&](float d) {
			float f = std::clamp((d - start) / length, 0.0f, 1.0f);
			return LottiePoint{a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
		};
		if (out.empty())
			out.push_back(at(std::max(from, start)));
		out.push_back(at(std::min(to, end)));
	}
	return out;
}


// Trims contours to the part from `start` to `end` (fractions; may pass 1,
// wrapping round), each on its own or all as one.
std::vector<Contour>
Trim(const std::vector<Contour>& contours, float start, float end, bool together)
{
	if (end - start >= 1)
		return contours;
	std::vector<Contour> out;
	if (end <= start)
		return out;
	auto piece = [&](const Contour& c, float from, float to) {
		Contour part;
		part.points = SubContour(c, from, to);
		if (part.points.size() >= 2)
			out.push_back(part);
	};
	// The span [start, end) of `length`, as up to two ranges.
	auto spans = [&](float length, float base, float total, const Contour& c) {
		float a = start * total - base, b = end * total - base;
		for (float shift : {0.0f, -total}) {
			float from = std::max(a + shift, 0.0f), to = std::min(b + shift, length);
			if (to > from)
				piece(c, from, to);
		}
	};
	if (!together) {
		for (const Contour& c : contours) {
			float length = ContourLength(c);
			spans(length, 0, length, c);
		}
		return out;
	}
	float total = 0;
	for (const Contour& c : contours)
		total += ContourLength(c);
	float base = 0;
	for (const Contour& c : contours) {
		float length = ContourLength(c);
		spans(length, base, total, c);
		base += length;
	}
	return out;
}


// #pragma mark - Shapes and layers


struct ShapeItem {
	std::string type;     // "gr", "sh", "rc", "el", "fl", "gf", "st", "gs", "tr", "tm"
	std::vector<ShapeItem> items;   // groups
	std::map<std::string, Prop> props;
	Transform transform;            // "tr"
	int32_t gradientStops = 0;
	int32_t gradientType = 1;
	int32_t fillRule = 1;
	int32_t cap = 2, join = 2;
	float miterLimit = 4;
	int32_t trimMode = 1;
	int32_t direction = 1;   // 3: counter-clockwise (rect, ellipse)

	const Prop& P(const char* name) const
	{
		static const Prop kEmpty;
		auto it = props.find(name);
		return it != props.end() ? it->second : kEmpty;
	}
};


ShapeItem
ParseShape(const json& shape)
{
	ShapeItem item;
	item.type = Str(shape, "ty");
	if (Bool(shape, "hd")) {
		item.type = "hidden";
		return item;
	}
	if (item.type == "gr") {
		const json& it = Field(shape, "it");
		if (it.is_array()) {
			for (const json& child : it)
				item.items.push_back(ParseShape(child));
		}
	} else if (item.type == "sh")
		item.props["ks"] = ParseProp(Field(shape, "ks"), true);
	else if (item.type == "tr")
		item.transform.Parse(shape);
	else {
		for (const char* name : {"p", "s", "r", "c", "o", "w", "e", "g"}) {
			const json& value = Field(shape, name);
			if (name[0] == 'g' && value.is_object()) {
				// "g": {"p": stop count, "k": {the animated values}}
				item.gradientStops = (int32_t)Int(value, "p");
				item.props["g"] = ParseProp(Field(value, "k"));
			} else if (value.is_object())
				item.props[name] = ParseProp(value);
		}
		item.gradientType = (int32_t)Int(shape, "t", 1);
		item.fillRule = (int32_t)Int(shape, "r", 1);
		item.cap = (int32_t)Int(shape, "lc", 2);
		item.join = (int32_t)Int(shape, "lj", 2);
		item.miterLimit = (float)Int(shape, "ml", 4);
		item.trimMode = (int32_t)Int(shape, "m", 1);
		item.direction = (int32_t)Int(shape, "d", 1);
	}
	return item;
}


// A layer mask: a path that keeps (adds) or cuts away (subtracts) part
// of the layer.
struct Mask {
	Prop path;
	Prop opacity;
	bool subtract = false;
	bool inverted = false;
};


struct Layer {
	std::vector<Mask> masks;
	int32_t index = -1;
	int32_t parent = -1;
	int32_t type = 4;
	float in = 0, out = 0, start = 0, stretch = 1;
	bool hidden = false;
	bool isMatte = false;
	int32_t matteMode = 0;
	Transform transform;
	std::vector<ShapeItem> shapes;
	std::string refId;
	float solidWidth = 0, solidHeight = 0;
	float solidColor[3] = {0, 0, 0};
};


struct Composition {
	std::vector<Layer> layers;
};


Layer
ParseLayer(const json& object)
{
	Layer layer;
	layer.index = (int32_t)Int(object, "ind", -1);
	layer.parent = (int32_t)Int(object, "parent", -1);
	layer.type = (int32_t)Int(object, "ty", 4);
	layer.in = Float(object, "ip", 0.0f);
	layer.out = Float(object, "op", 0.0f);
	layer.start = Float(object, "st", 0.0f);
	layer.stretch = Float(object, "sr", 1.0f);
	if (layer.stretch == 0)
		layer.stretch = 1;
	layer.hidden = Bool(object, "hd");
	layer.isMatte = Int(object, "td") != 0;
	layer.matteMode = (int32_t)Int(object, "tt");
	layer.transform.Parse(Field(object, "ks"));
	layer.refId = Str(object, "refId");
	const json& masks = Field(object, "masksProperties");
	if (masks.is_array()) {
		for (const json& m : masks) {
			std::string mode = Str(m, "mode", "a");
			if (mode == "n")
				continue;
			Mask mask;
			mask.path = ParseProp(Field(m, "pt"), true);
			mask.opacity = ParseProp(Field(m, "o"));
			mask.subtract = mode == "s";
			mask.inverted = Bool(m, "inv");
			layer.masks.push_back(mask);
		}
	}
	const json& shapes = Field(object, "shapes");
	if (shapes.is_array()) {
		for (const json& shape : shapes)
			layer.shapes.push_back(ParseShape(shape));
	}
	if (layer.type == 1) {
		layer.solidWidth = Float(object, "sw", 0.0f);
		layer.solidHeight = Float(object, "sh", 0.0f);
		std::string color = Str(object, "sc");   // "#rrggbb"
		if (color.size() == 7 && color[0] == '#') {
			for (int i = 0; i < 3; i++)
				layer.solidColor[i] = std::strtol(color.substr(1 + i * 2, 2).c_str(), nullptr, 16) / 255.0f;
		}
	}
	return layer;
}


Composition
ParseComposition(const json& layers)
{
	Composition comp;
	if (layers.is_array()) {
		for (const json& layer : layers)
			comp.layers.push_back(ParseLayer(layer));
	}
	return comp;
}

} // namespace


struct LottieAnimation::Data {
	Composition root;
	std::map<std::string, Composition> assets;

	void RenderComp(const Composition& comp, float t, const Matrix& m, float opacity,
		LottieList& out, int depth) const;
	void RenderLayer(const Composition& comp, const Layer& layer, float t, const Matrix& m,
		float opacity, LottieList& out, int depth) const;
	Matrix LayerMatrix(const Composition& comp, const Layer& layer, float t, int depth) const;
	void RenderGroup(const std::vector<ShapeItem>& items, float t, const Matrix& m,
		float opacity, LottieList& out) const;
	std::vector<Contour> CollectPaths(const std::vector<ShapeItem>& items, size_t end, float t,
		const Matrix& m) const;
};


namespace {

Matrix
GroupMatrix(const std::vector<ShapeItem>& items, float t, float* opacity)
{
	for (const ShapeItem& item : items) {
		if (item.type == "tr") {
			if (opacity != nullptr)
				*opacity = item.transform.Opacity(t);
			return item.transform.At(t);
		}
	}
	if (opacity != nullptr)
		*opacity = 1;
	return Matrix();
}


void
SetColor(LottiePaint& paint, const std::vector<float>& c, float opacity)
{
	float scale = 1;
	for (size_t i = 0; i < c.size() && i < 3; i++) {
		if (c[i] > 1)
			scale = 1.0f / 255;   // some files give 0..255
	}
	paint.r = c.size() > 0 ? c[0] * scale : 0;
	paint.g = c.size() > 1 ? c[1] * scale : 0;
	paint.b = c.size() > 2 ? c[2] * scale : 0;
	paint.a = std::clamp(opacity, 0.0f, 1.0f);
}


// Gradient colours: `count` stops of offset, r, g, b, then maybe pairs of
// offset and opacity, which are applied at each colour stop.
std::vector<LottiePaint::Stop>
GradientStops(const std::vector<float>& values, int32_t count, float opacity)
{
	std::vector<LottiePaint::Stop> stops;
	if (count <= 0 || values.size() < (size_t)count * 4)
		return stops;
	std::vector<std::pair<float, float>> alphas;
	for (size_t i = count * 4; i + 1 < values.size(); i += 2)
		alphas.push_back({values[i], values[i + 1]});
	auto alphaAt = [&](float offset) {
		if (alphas.empty())
			return 1.0f;
		if (offset <= alphas.front().first)
			return alphas.front().second;
		for (size_t i = 1; i < alphas.size(); i++) {
			if (offset <= alphas[i].first) {
				float span = alphas[i].first - alphas[i - 1].first;
				float f = span > 0 ? (offset - alphas[i - 1].first) / span : 0;
				return alphas[i - 1].second + (alphas[i].second - alphas[i - 1].second) * f;
			}
		}
		return alphas.back().second;
	};
	for (int32_t i = 0; i < count; i++) {
		float offset = values[i * 4];
		stops.push_back({offset, values[i * 4 + 1], values[i * 4 + 2], values[i * 4 + 3],
			alphaAt(offset) * opacity});
	}
	return stops;
}

} // namespace


std::vector<Contour>
LottieAnimation::Data::CollectPaths(const std::vector<ShapeItem>& items, size_t end, float t,
	const Matrix& m) const
{
	std::vector<Contour> paths;
	for (size_t i = 0; i < end && i < items.size(); i++) {
		const ShapeItem& item = items[i];
		if (item.type == "sh") {
			Contour c = FlattenPath(Evaluate(item.P("ks"), t), m);
			if (c.points.size() >= 2)
				paths.push_back(c);
		} else if (item.type == "rc" || item.type == "el") {
			std::vector<float> p = Evaluate(item.P("p"), t), s = Evaluate(item.P("s"), t);
			p.resize(2, 0);
			s.resize(2, 0);
			std::vector<float> packed = item.type == "rc"
				? RectPath(p[0], p[1], s[0], s[1], EvaluateNumber(item.P("r"), t, 0))
				: EllipsePath(p[0], p[1], s[0], s[1]);
			Contour c = FlattenPath(packed, m);
			if (item.direction == 3)
				std::reverse(c.points.begin(), c.points.end());
			paths.push_back(c);
		} else if (item.type == "gr") {
			Matrix inner = m * GroupMatrix(item.items, t, nullptr);
			std::vector<Contour> more = CollectPaths(item.items, item.items.size(), t, inner);
			paths.insert(paths.end(), more.begin(), more.end());
		} else if (item.type == "tm") {
			float s = EvaluateNumber(item.P("s"), t, 0) / 100;
			float e = EvaluateNumber(item.P("e"), t, 100) / 100;
			float o = EvaluateNumber(item.P("o"), t, 0) / 360;
			if (s > e)
				std::swap(s, e);
			s += o;
			e += o;
			float shift = std::floor(s);
			s -= shift;
			e -= shift;
			paths = Trim(paths, s, e, item.trimMode == 2);
		}
	}
	return paths;
}


void
LottieAnimation::Data::RenderGroup(const std::vector<ShapeItem>& items, float t, const Matrix& m,
	float opacity, LottieList& out) const
{
	// Bottom first: later items are drawn under earlier ones. A paint
	// applies to every path above it in its group, nested ones included.
	for (size_t i = items.size(); i-- > 0;) {
		const ShapeItem& item = items[i];
		if (item.type == "gr") {
			float groupOpacity = 1;
			Matrix inner = m * GroupMatrix(item.items, t, &groupOpacity);
			RenderGroup(item.items, t, inner, opacity * groupOpacity, out);
			continue;
		}
		bool fill = item.type == "fl" || item.type == "gf";
		bool stroke = item.type == "st" || item.type == "gs";
		if (!fill && !stroke)
			continue;
		std::vector<Contour> paths = CollectPaths(items, i, t, m);
		if (paths.empty())
			continue;
		LottieItem draw;
		LottiePaint& paint = draw.shape.paint;
		paint.kind = fill ? LottiePaint::kFill : LottiePaint::kStroke;
		float alpha = opacity * EvaluateNumber(item.P("o"), t, 100) / 100;
		if (item.type == "gf" || item.type == "gs") {
			std::vector<LottiePaint::Stop> stops = GradientStops(Evaluate(item.P("g"), t),
				item.gradientStops, alpha);
			if (stops.empty())
				continue;
			if (fill) {
				std::vector<float> s = Evaluate(item.P("s"), t), e = Evaluate(item.P("e"), t);
				s.resize(2, 0);
				e.resize(2, 0);
				paint.gradient = item.gradientType == 2 ? LottiePaint::kRadial : LottiePaint::kLinear;
				paint.start = m.Apply(s[0], s[1]);
				paint.end = m.Apply(e[0], e[1]);
				paint.stops = stops;
				paint.a = alpha;
			} else {
				// Gradient strokes aren't allowed in stickers: the first colour.
				SetColor(paint, {stops[0].r, stops[0].g, stops[0].b}, stops[0].a);
			}
		} else
			SetColor(paint, Evaluate(item.P("c"), t), alpha);
		if (paint.a <= 0 && paint.gradient == LottiePaint::kNone)
			continue;
		paint.evenOdd = item.fillRule == 2;
		if (stroke) {
			paint.width = EvaluateNumber(item.P("w"), t, 1) * m.Scale();
			paint.cap = item.cap == 1 ? LottiePaint::kButt
				: item.cap == 3 ? LottiePaint::kSquare : LottiePaint::kRound;
			paint.join = item.join == 1 ? LottiePaint::kMiter
				: item.join == 3 ? LottiePaint::kBevel : LottiePaint::kRoundJoin;
			paint.miterLimit = item.miterLimit;
			if (paint.width <= 0)
				continue;
		}
		for (const Contour& c : paths) {
			draw.shape.contours.push_back(c.points);
			draw.shape.closed.push_back(c.closed);
		}
		out.push_back(std::move(draw));
	}
}


Matrix
LottieAnimation::Data::LayerMatrix(const Composition& comp, const Layer& layer, float t,
	int depth) const
{
	Matrix m = layer.transform.At(t);
	if (layer.parent < 0 || depth > 32)
		return m;
	for (const Layer& other : comp.layers) {
		if (other.index == layer.parent)
			return LayerMatrix(comp, other, t, depth + 1) * m;
	}
	return m;
}


void
LottieAnimation::Data::RenderLayer(const Composition& comp, const Layer& layer, float t,
	const Matrix& m, float opacity, LottieList& out, int depth) const
{
	if (layer.hidden || t < layer.in || t >= layer.out)
		return;
	Matrix world = m * LayerMatrix(comp, layer, t, 0);
	float alpha = opacity * layer.transform.Opacity(t);
	if (alpha <= 0)
		return;
	if (!layer.masks.empty()) {
		// Drawn without its masks, then through them: what the added ones
		// cover, less what the subtracted ones do.
		Layer bare = layer;
		bare.masks.clear();
		auto content = std::make_shared<LottieList>();
		RenderLayer(comp, bare, t, m, opacity, *content, depth);
		auto added = std::make_shared<LottieList>();
		auto cut = std::make_shared<LottieList>();
		for (const Mask& mask : layer.masks) {
			LottieItem fill;
			Contour c = FlattenPath(Evaluate(mask.path, t), world);
			if (c.points.size() < 3)
				continue;
			fill.shape.contours.push_back(c.points);
			fill.shape.closed.push_back(true);
			fill.shape.paint.a = EvaluateNumber(mask.opacity, t, 100) / 100;
			(mask.subtract != mask.inverted ? cut : added)->push_back(std::move(fill));
		}
		LottieItem item;
		if (!added->empty()) {
			item.matte = LottieItem::kAlpha;
			item.content = content;
			item.matteSource = added;
		}
		if (!cut->empty()) {
			LottieItem outer;
			outer.matte = LottieItem::kAlphaInverted;
			if (item.content) {
				outer.content = std::make_shared<LottieList>();
				outer.content->push_back(std::move(item));
			} else
				outer.content = content;
			outer.matteSource = cut;
			item = std::move(outer);
		}
		if (item.content)
			out.push_back(std::move(item));
		else
			out.insert(out.end(), content->begin(), content->end());
		return;
	}
	if (layer.type == 4)
		RenderGroup(layer.shapes, t, world, alpha, out);
	else if (layer.type == 0) {
		auto asset = assets.find(layer.refId);
		if (asset != assets.end() && depth < 16)
			RenderComp(asset->second, (t - layer.start) / layer.stretch, world, alpha, out,
				depth + 1);
	} else if (layer.type == 1 && layer.solidWidth > 0) {
		LottieItem draw;
		Contour c = FlattenPath(RectPath(layer.solidWidth / 2, layer.solidHeight / 2,
			layer.solidWidth, layer.solidHeight, 0), world);
		draw.shape.contours.push_back(c.points);
		draw.shape.closed.push_back(true);
		SetColor(draw.shape.paint, {layer.solidColor[0], layer.solidColor[1],
			layer.solidColor[2]}, alpha);
		out.push_back(std::move(draw));
	}
}


void
LottieAnimation::Data::RenderComp(const Composition& comp, float t, const Matrix& m,
	float opacity, LottieList& out, int depth) const
{
	// The first layer is on top: draw from the last.
	for (size_t i = comp.layers.size(); i-- > 0;) {
		const Layer& layer = comp.layers[i];
		if (layer.isMatte)
			continue;
		if (layer.matteMode != 0 && i > 0 && comp.layers[i - 1].isMatte) {
			LottieItem matte;
			matte.matte = (LottieItem::Matte)std::clamp(layer.matteMode, 1, 4);
			matte.content = std::make_shared<LottieList>();
			matte.matteSource = std::make_shared<LottieList>();
			RenderLayer(comp, layer, t, m, opacity, *matte.content, depth);
			// The matte is drawn whole: its own opacity shapes it.
			Layer source = comp.layers[i - 1];
			source.isMatte = false;
			RenderLayer(comp, source, t, m, 1, *matte.matteSource, depth);
			if (!matte.content->empty())
				out.push_back(std::move(matte));
			continue;
		}
		RenderLayer(comp, layer, t, m, opacity, out, depth);
	}
}


LottieAnimation::LottieAnimation()
	:
	fData(new Data)
{
}


LottieAnimation::~LottieAnimation()
{
}


bool
LottieAnimation::Load(const std::string& jsonText)
{
	json root = json::parse(jsonText, nullptr, false);
	if (!root.is_object() || !root.contains("layers"))
		return false;
	fFrameRate = Float(root, "fr", 30.0f);
	fIn = Float(root, "ip", 0.0f);
	fOut = Float(root, "op", 0.0f);
	fWidth = (int32_t)Int(root, "w");
	fHeight = (int32_t)Int(root, "h");
	if (fFrameRate <= 0 || fOut <= fIn || fWidth <= 0 || fHeight <= 0)
		return false;
	fData->root = ParseComposition(Field(root, "layers"));
	const json& assets = Field(root, "assets");
	if (assets.is_array()) {
		for (const json& asset : assets) {
			if (asset.contains("layers"))
				fData->assets[Str(asset, "id")] = ParseComposition(Field(asset, "layers"));
		}
	}
	return true;
}


int32_t
LottieAnimation::FrameCount() const
{
	return (int32_t)std::ceil(fOut - fIn);
}


LottieList
LottieAnimation::Render(float frame, float width, float height) const
{
	LottieList out;
	if (fWidth <= 0 || fHeight <= 0)
		return out;
	Matrix scale;
	scale.a = width / fWidth;
	scale.d = height / fHeight;
	fData->RenderComp(fData->root, frame, scale, 1, out, 0);
	return out;
}


std::string
GunzipTgs(const std::string& packed, size_t maxSize)
{
	z_stream stream = {};
	if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
		return std::string();
	stream.next_in = (Bytef*)packed.data();
	stream.avail_in = (uInt)packed.size();
	std::string out;
	char buffer[16384];
	int result;
	do {
		stream.next_out = (Bytef*)buffer;
		stream.avail_out = sizeof(buffer);
		result = inflate(&stream, Z_NO_FLUSH);
		if (result != Z_OK && result != Z_STREAM_END) {
			inflateEnd(&stream);
			return std::string();
		}
		out.append(buffer, sizeof(buffer) - stream.avail_out);
		if (out.size() > maxSize) {
			inflateEnd(&stream);
			return std::string();
		}
	} while (result != Z_STREAM_END && stream.avail_in > 0);
	inflateEnd(&stream);
	return result == Z_STREAM_END ? out : std::string();
}


float
BezierEase(float x1, float y1, float x2, float y2, float t)
{
	if (t <= 0)
		return 0;
	if (t >= 1)
		return 1;
	if (x1 == y1 && x2 == y2)
		return t;   // linear
	auto curve = [](float a, float b, float s) {
		// One coordinate of the bezier (0, a, b, 1) at parameter s.
		float u = 1 - s;
		return 3 * u * u * s * a + 3 * u * s * s * b + s * s * s;
	};
	// The parameter where x = t: Newton's method, then halving if it fails.
	float s = t;
	for (int i = 0; i < 8; i++) {
		float x = curve(x1, x2, s) - t;
		if (std::fabs(x) < 1e-5f)
			return curve(y1, y2, s);
		float u = 1 - s;
		float dx = 3 * u * u * x1 + 6 * u * s * (x2 - x1) + 3 * s * s * (1 - x2);
		if (std::fabs(dx) < 1e-6f)
			break;
		s -= x / dx;
	}
	float lo = 0, hi = 1;
	s = t;
	for (int i = 0; i < 40; i++) {
		float x = curve(x1, x2, s);
		if (std::fabs(x - t) < 1e-5f)
			break;
		if (x < t)
			lo = s;
		else
			hi = s;
		s = (lo + hi) / 2;
	}
	return curve(y1, y2, s);
}

} // namespace pp
