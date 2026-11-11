#!/usr/bin/env python3
"""make-hvif-icon.py — writes a Haiku vector icon (HVIF) as an .rdef resource.

Haiku has no host-side HVIF tool, so this encodes the flat icon format
directly (Haiku's src/libs/icon/flat_icon/FlatIconFormat.h): solid and
vertical-gradient styles, closed polygon paths, one path per shape. Copy it
into the app's tools/ and replace build() with the app's design (64x64
grid). The build() below is EasyPaint's icon (a grey tile, two green arcs,
three orange bars), from github.com/idumych/EasyPaint.

Check the result on Haiku: Tracker must show it (a generic icon means it
didn't parse); see references/porting.md section 5.

    python3 tools/make-icon.py icon.rdef [preview.svg]

then replace the vector_icon resource at the end of EasyPaint.rdef with the
contents of icon.rdef.
"""
import math
import struct
import sys

STYLE_SOLID = 1
STYLE_GRADIENT = 2
GRADIENT_LINEAR = 0
GRADIENT_FLAG_TRANSFORM = 1 << 1
PATH_FLAG_CLOSED = 1 << 1
PATH_FLAG_NO_CURVES = 1 << 3
SHAPE_PATH_SOURCE = 10


def coord(value):
    if value == int(value) and -32 <= value <= 95:
        return bytes([int(value) + 32])
    fixed = int(round((value + 128.0) * 102.0))
    return bytes([0x80 | (fixed >> 8), fixed & 0xff])


def float24(value):
    if value == 0:
        return b"\0\0\0"
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    sign = bits >> 31
    exponent = ((bits >> 23) & 0xff) - 127 + 32
    mantissa = (bits & 0x7fffff) >> 6
    packed = (sign << 23) | (exponent << 17) | mantissa
    return bytes([packed >> 16, (packed >> 8) & 0xff, packed & 0xff])


def solid(r, g, b, a=255):
    return {"kind": "solid", "color": (r, g, b, a)}


def vertical_gradient(top, bottom, y0, y1):
    """A linear gradient from y0 (colour top) to y1 (colour bottom)."""
    return {"kind": "gradient", "stops": [(0, top), (255, bottom)],
            "y0": y0, "y1": y1}


def encode_style(style):
    if style["kind"] == "solid":
        return bytes([STYLE_SOLID]) + bytes(style["color"])
    # Gradient space runs from -64 to 64 along x; map it onto y0..y1.
    half = (style["y1"] - style["y0"]) / 128.0
    matrix = [0.0, half, -half, 0.0, 32.0, (style["y0"] + style["y1"]) / 2]
    out = bytes([STYLE_GRADIENT, GRADIENT_LINEAR, GRADIENT_FLAG_TRANSFORM,
                 len(style["stops"])])
    for value in matrix:
        out += float24(value)
    for offset, color in style["stops"]:
        out += bytes([offset]) + bytes(color)
    return out


def encode_path(points):
    assert 2 <= len(points) <= 255
    out = bytes([PATH_FLAG_CLOSED | PATH_FLAG_NO_CURVES, len(points)])
    for x, y in points:
        out += coord(round(x, 2)) + coord(round(y, 2))
    return out


def rounded_rect(left, top, right, bottom, radius, steps=6):
    points = []
    corners = [(right - radius, top + radius, -90),
               (right - radius, bottom - radius, 0),
               (left + radius, bottom - radius, 90),
               (left + radius, top + radius, 180)]
    for cx, cy, start in corners:
        for i in range(steps + 1):
            angle = math.radians(start + 90.0 * i / steps)
            points.append((cx + radius * math.cos(angle),
                           cy + radius * math.sin(angle)))
    return points


def quarter_ring(cx, cy, inner, outer, steps=14):
    """From straight up (angle 90) round to the right (angle 0)."""
    points = []
    for i in range(steps + 1):
        angle = math.radians(90 - 90.0 * i / steps)
        points.append((cx + outer * math.cos(angle),
                       cy - outer * math.sin(angle)))
    for i in range(steps + 1):
        angle = math.radians(90.0 * i / steps)
        points.append((cx + inner * math.cos(angle),
                       cy - inner * math.sin(angle)))
    return points


def rect(left, top, right, bottom):
    return [(left, top), (right, top), (right, bottom), (left, bottom)]


def grow(points, amount):
    """The polygon pushed out from its centre by `amount`, for an outline."""
    cx = sum(x for x, _ in points) / len(points)
    cy = sum(y for _, y in points) / len(points)
    out = []
    for x, y in points:
        dx, dy = x - cx, y - cy
        length = (dx * dx + dy * dy) ** 0.5 or 1
        out.append((x + dx / length * amount, y + dy / length * amount))
    return out


def build():
    """Paper Plane: a folded paper plane in front view, lit from the top
    left (Haiku Icon Guidelines), dark navy outline, shadow to the right."""
    styles = [
        solid(20, 40, 90, 110),                              # 0 shadow
        solid(24, 44, 92),                                   # 1 outline
        vertical_gradient((255, 255, 255, 255),
                          (196, 222, 250, 255), 8, 36),      # 2 upper wing
        vertical_gradient((150, 196, 245, 255),
                          (78, 140, 222, 255), 10, 56),      # 3 lower wing
        solid(52, 104, 190),                                 # 4 fold
        solid(255, 255, 255, 170),                           # 5 highlight
    ]
    upper = [(5, 30), (58, 7), (26, 37)]
    lower = [(26, 37), (58, 7), (35, 56)]
    fold = [(26, 37), (35, 56), (23, 50)]
    shapes = []
    # The shadow falls straight to the right and a little down.
    for poly in (upper, lower, fold):
        shapes.append((0, [(x + 4, y + 3) for x, y in poly]))
    for poly in (upper, lower, fold):
        shapes.append((1, grow(poly, 1.6)))
    shapes.append((2, upper))
    shapes.append((3, lower))
    shapes.append((4, fold))
    # A thin highlight along the leading edge.
    shapes.append((5, [(9, 29.5), (55, 9.5), (54, 10.5), (10, 30.2)]))
    return styles, shapes


def encode(styles, shapes):
    out = b"ncif"
    out += bytes([len(styles)])
    for style in styles:
        out += encode_style(style)
    out += bytes([len(shapes)])
    for _, points in shapes:
        out += encode_path(points)
    out += bytes([len(shapes)])
    for index, (style, _) in enumerate(shapes):
        out += bytes([SHAPE_PATH_SOURCE, style, 1, index, 0])
    return out


def svg(styles, shapes):
    lines = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" '
             'width="256" height="256">', '<defs>']
    for index, style in enumerate(styles):
        if style["kind"] == "gradient":
            (o0, c0), (o1, c1) = style["stops"]
            lines.append(
                f'<linearGradient id="g{index}" gradientUnits="userSpaceOnUse"'
                f' x1="0" y1="{style["y0"]}" x2="0" y2="{style["y1"]}">'
                f'<stop offset="0" stop-color="rgb{c0[:3]}"/>'
                f'<stop offset="1" stop-color="rgb{c1[:3]}"/>'
                '</linearGradient>')
    lines.append('</defs>')
    for style_index, points in shapes:
        style = styles[style_index]
        if style["kind"] == "gradient":
            fill = f'url(#g{style_index})'
            opacity = 1
        else:
            fill = f'rgb{style["color"][:3]}'
            opacity = style["color"][3] / 255
        path = " ".join(f"{x:.2f},{y:.2f}" for x, y in points)
        lines.append(f'<polygon points="{path}" fill="{fill}" '
                     f'fill-opacity="{opacity:.2f}"/>')
    lines.append('</svg>')
    return "\n".join(lines)


def main():
    styles, shapes = build()
    data = encode(styles, shapes)
    hex_data = data.hex().upper()
    rows = [hex_data[i:i + 64] for i in range(0, len(hex_data), 64)]
    with open(sys.argv[1], "w") as rdef:
        rdef.write("// Generated by tools/make-icon.py; don't edit.\n")
        rdef.write("resource vector_icon {\n")
        for row in rows:
            rdef.write(f'\t$"{row}"\n')
        rdef.write("};\n")
    if len(sys.argv) > 2:
        with open(sys.argv[2], "w") as preview:
            preview.write(svg(styles, shapes))
    print(f"{len(data)} bytes of HVIF")


if __name__ == "__main__":
    main()
