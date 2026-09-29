#!/usr/bin/env python3
"""
Turns the generated artwork (the image generator's downloads) into the skin files the plug-in
builds in: Source/Assets/Skin/<name>.png, named and sized as in Source/Assets/Skin/ASSET_SPEC.md
(drawn at 2x for 200% scaling).

    python3 tools/skin_import.py "<folder with the downloads>" [--only name1,name2] [--sheet out.png]

For every file: crop to the artwork, scale with Lanczos, centre it. The states of one element
(on/off, up/down, ...) are aligned on their opaque body so the frames match pixel for pixel.
Stretchable pieces (panels, buttons, pills, hexes) keep the artwork's own aspect: the code draws
them 9- or 3-slice, so only their height (and the corner width in ASSET_SPEC) matters.

Two fixes are done here rather than in the image generator:
  * knob_base: the scale ticks are redrawn at exactly -135..+135 degrees (11 ticks, 27 degrees apart),
    matching the knobs' rotary range (Controls.cpp);
  * knob caps: brightness is evened out around the circle, so nothing on the cap looks lit from one
    side once it turns (the code lays the fixed light on top).

Needs Pillow, numpy and scipy.
"""
import argparse, math, os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Source", "Assets", "Skin")

# name -> source file. Groups: the states of one element, aligned together.
# mode: fixed (W, H)  -> exact canvas, artwork fitted inside with `pad`
#       height H      -> artwork keeps its aspect, canvas height H (sliced pieces)
#       width W       -> artwork keeps its aspect, canvas width W
#       exact (W, H)  -> body stretched to exactly W x H (square 9-slice panels)
#       native        -> as is, only cropped to the artwork (full-window background)
GROUPS = [
    # --- 1. background and header
    dict(files={"bg_main": "download (16).webp"}, mode="native", aspect=2200 / 1600),
    dict(files={"bg_emblem": "download (3).png"}, mode="fixed", size=(1000, 1290), pad=0, body=8),
    dict(files={"header_bar": "download (15).webp"}, mode="width", size=2200, pad=0, body=8),
    dict(files={"logo": "download.webp"}, mode="fixed", size=(860, 440), pad=8, body=8),
    # --- 2. panels (9-slice)
    dict(files={"panel": "download (35).png"}, mode="exact", size=(256, 256)),
    dict(files={"panel_inset": "download (34).png"}, mode="exact", size=(128, 128)),
    dict(files={"panel_footboard": "download (33).png"}, mode="exact", size=(256, 256)),
    # --- 3. knobs
    dict(files={"knob_base": "knob_base_v2.webp"}, mode="fixed", size=(160, 160), pad=4, fix="ticks"),
    dict(files={"knob_cap": "knob_cap_v3.webp"}, mode="fixed", size=(112, 112), pad=8, fix="evenlight"),
    dict(files={"knob_cap_small": "knob_cap_v3.webp"}, mode="fixed", size=(80, 80), pad=6, fix="evenlight"),
    dict(files={"knob_big_cap": "knob_cap_v3.webp"}, mode="fixed", size=(144, 144), pad=8, fix="evenlight"),
    # --- 4. power buttons, LEDs
    dict(files={"power_off": "download (26).png", "power_on": "download (25).png"}, mode="fixed", size=(64, 64), pad=3),
    dict(files={"led_off": "download (24).png", "led_amber": "download (23).png", "led_green": "download (22).png",
                "led_red": "download (21).png"}, mode="fixed", size=(32, 32), pad=0, norm=False, ref="led_off", fitref=0.62),
    # --- 5. buttons, pills, segments, arrows
    dict(files={"button_normal": "download (18).png", "button_hover": "download (19).png", "button_down": "download (20).png"},
         mode="height", size=64, pad=4),
    dict(files={"pill_off": "download (17).png", "pill_on": "download (16).png"}, mode="height", size=44, pad=4),
    dict(files={"segment_bg": "download (15).png"}, mode="height", size=56, pad=3),
    dict(files={"segment_on": "download (14).png"}, mode="height", size=48, pad=3),
    dict(files={"arrow_left": "download (13).png", "arrow_right": "download (12).png"}, mode="fixed", size=(64, 64), pad=3),
    # --- 6. chain strip
    dict(files={"chain_hex_off": "download (11).png", "chain_hex_on": "download (10).png", "chain_hex_selected": "download (9).png"},
         mode="height", size=124, pad=6),
    dict(files={"chain_connector": "download (8).png"}, mode="height", size=12, pad=0),
    dict(files={"chain_in": "download (6).png", "chain_out": "download (7).png"}, mode="fixed", size=(64, 64), pad=3),
    # --- 7. footswitches, toggle
    dict(files={"footswitch_up": "download (32).png", "footswitch_down": "download (31).png"}, mode="fixed", size=(184, 184), pad=6, norm=False),
    dict(files={"footswitch_bypass_up": "download (30).png", "footswitch_bypass_down": "download (29).png"}, mode="fixed", size=(184, 184), pad=6, norm=False),
    dict(files={"toggle_up": "download (28).png", "toggle_down": "download (27).png"}, mode="fixed", size=(76, 100), pad=3, norm=False, align="nut"),
    # --- 8. scenes
    dict(files={"scene_off": "download (5).png", "scene_on": "download (4).png"}, mode="height", size=64, pad=4),
    # --- 9. meters
    dict(files={"meter_bg": "download (14).webp"}, mode="height", size=52, pad=2),
    # --- 10. amp and cab (aspect kept: the code slices / crops them)
    dict(files={"amp_plate_nam": "download (50).png"}, mode="height", size=220, pad=0),
    dict(files={"amp_plate_brit": "download (51).png"}, mode="height", size=220, pad=0),
    dict(files={"amp_plate_chrome": "download (52).png"}, mode="height", size=220, pad=0),
    dict(files={"amp_plate_steel": "download (53).png"}, mode="height", size=220, pad=0),
    dict(files={"amp_head_nam": "download (46).png"}, mode="fixed", size=(300, 200), pad=4),
    dict(files={"amp_head_steel": "download (47).png"}, mode="fixed", size=(300, 200), pad=4),
    dict(files={"amp_head_brit": "download (48).png"}, mode="fixed", size=(300, 200), pad=4),
    dict(files={"amp_head_chrome": "download (49).png"}, mode="fixed", size=(300, 200), pad=4),
    dict(files={"cab_grille_0": "download (45).png"}, mode="height", size=210, pad=0),
    dict(files={"cab_grille_1": "download (44).png"}, mode="height", size=210, pad=0),
    dict(files={"cab_grille_2": "download (43).png"}, mode="height", size=210, pad=0),
    dict(files={"cab_grille_3": "download (42).png"}, mode="height", size=210, pad=0),
    dict(files={"cab_grille_4": "download (41).png"}, mode="height", size=210, pad=0),
    # --- 11. tuner
    dict(files={"tuner_panel": "download (13).webp"}, mode="fixed", size=(1320, 800), pad=4),
    dict(files={"tuner_cell_off": "download (12).webp", "tuner_cell_on": "download (11).webp"}, mode="fixed", size=(64, 72), pad=2),
    dict(files={"tuner_cell_center": "download (10).webp"}, mode="fixed", size=(64, 72), pad=0),
    # --- block icons (optional, for a future mini view)
    *[dict(files={"icon_" + k: v}, mode="fixed", size=(64, 64), pad=4) for k, v in {
        "smoke": "download (9).webp", "shift": "download (8).webp", "hive": "download (2).png", "wasp": "download (7).webp",
        "amp": "download (6).webp", "cab": "download (1).png", "swarm": "download (5).webp", "wings": "download (4).webp",
        "comb": "download (3).webp", "carve": "download (1).webp", "crypt": "download.png"}.items()],
]


def load(path):
    return np.asarray(Image.open(path).convert("RGBA")).astype(np.float64)


def bbox(mask):
    ys, xs = np.nonzero(mask)
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def to_image(a):
    return Image.fromarray(np.clip(np.round(a), 0, 255).astype(np.uint8), "RGBA")


def resample(a, scale, centre, out_size, offset):
    """Scales RGBA `a` by `scale` and puts its point `centre` at `offset` of an out_size canvas
    (edge coordinates; premultiplied Lanczos, so transparent edges don't pick up dark fringes)."""
    pm = a.copy()
    pm[..., :3] *= pm[..., 3:4] / 255.0
    img = Image.fromarray(np.clip(np.round(pm), 0, 255).astype(np.uint8), "RGBA")
    scaled = img.resize((max(1, round(img.width * scale)), max(1, round(img.height * scale))), Image.LANCZOS)
    sx, sy = scaled.width / img.width, scaled.height / img.height
    # the sub-pixel shift that puts the centre where it belongs (PIL's affine maps output -> input)
    tx, ty = centre[0] * sx - offset[0], centre[1] * sy - offset[1]
    out = np.asarray(scaled.transform(out_size, Image.AFFINE, (1, 0, tx, 0, 1, ty), resample=Image.BICUBIC)).astype(np.float64)
    alpha = out[..., 3:4]
    out[..., :3] = np.where(alpha > 0, out[..., :3] * 255.0 / np.maximum(alpha, 1e-6), 0)
    return out


def fix_ticks(a):
    """knob_base: erase the drawn ticks and stamp the 12 o'clock one at -135..+135 deg, 27 deg apart."""
    al = a[..., 3]
    x0, y0, x1, y1 = bbox(al > 128)
    cx, cy = (x0 + x1 - 1) / 2, (y0 + y1 - 1) / 2
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    core = ndimage.binary_opening((r - b > 90) & (r > 150) & (al > 128))
    glow = ndimage.binary_dilation(core, iterations=10) & (r - b > 18)
    tick = ndimage.binary_dilation(core | glow, iterations=3)
    h, w = al.shape
    Y, X = np.mgrid[:h, :w]
    ang = np.arctan2(X - cx, -(Y - cy))
    rad = np.hypot(X - cx, Y - cy)

    def sample(src, angle_of_px, radius):   # colour at (angle, radius) of src, bilinear
        sx = cx + radius * np.sin(angle_of_px)
        sy = cy - radius * np.cos(angle_of_px)
        return np.stack([ndimage.map_coordinates(src[..., k], [sy, sx], order=1, mode="nearest") for k in range(4)], -1)

    # 1) erase: fill each tick from the ring half-way to the neighbour tick (same radius)
    clean = a.copy()
    for shift in (np.radians(12.0), np.radians(-12.0)):
        src = sample(a, ang + shift, rad)
        srcTick = sample(tick[..., None].astype(float).repeat(4, -1), ang + shift, rad)[..., 0] > 0.5
        ok = tick & ~srcTick & (np.abs(clean - a).sum(-1) == 0)
        clean[ok] = src[ok]
    # 2) stamp: the top tick (with its glow) rotated to each position, blended with a soft mask
    top = tick & (np.abs(ang) < np.radians(8))
    soft = ndimage.gaussian_filter(top.astype(float), 1.5)
    out = clean.copy()
    for k in range(11):
        t = np.radians(-135 + 27 * k)
        src = sample(a, ang - t, rad)
        m = sample(np.repeat(soft[..., None], 4, -1), ang - t, rad)[..., 0:1]
        out = out * (1 - m) + src * m
    return out


def even_light(a):
    """Caps: divide out the brightness that depends on the angle (ring by ring), keep the texture."""
    al = a[..., 3]
    x0, y0, x1, y1 = bbox(al > 128)
    cx, cy = (x0 + x1 - 1) / 2, (y0 + y1 - 1) / 2
    R = max(x1 - x0, y1 - y0) / 2
    h, w = al.shape
    Y, X = np.mgrid[:h, :w]
    d = np.hypot(X - cx, Y - cy) / R
    ang = ((np.degrees(np.arctan2(X - cx, -(Y - cy))) + 360) % 360).astype(int) % 360
    L = a[..., :3].mean(-1)
    pointer = ndimage.binary_dilation((a[..., 0] - a[..., 2] > 100) & (al > 128), iterations=6)
    ok = (al > 200) & ~pointer
    gain = np.ones((h, w))
    step = 0.03
    for r0 in np.arange(0, 1.05, step):
        band = (d >= r0) & (d < r0 + step)
        s = ok & band
        if s.sum() < 400:
            continue
        cnt = np.bincount(ang[s], minlength=360)
        prof = np.bincount(ang[s], weights=L[s], minlength=360) / np.maximum(cnt, 1)
        idx = np.arange(360)
        have = cnt > 0
        prof = np.interp(idx, idx[have], prof[have], period=360)
        prof = ndimage.gaussian_filter1d(prof, 6, mode="wrap")
        sel = band & ~pointer
        gain[sel] = prof.mean() / np.maximum(prof[ang[sel]], 1)
    out = a.copy()
    out[..., :3] = np.clip(a[..., :3] * gain[..., None], 0, 255)
    return out


def process(group, src_dir):
    names = list(group["files"].keys())
    arrays = {n: load(os.path.join(src_dir, group["files"][n])) for n in names}
    mode = group["mode"]
    pad = group.get("pad", 0)
    if group.get("fix") == "ticks":
        arrays = {n: fix_ticks(a) for n, a in arrays.items()}
    if group.get("fix") == "evenlight":
        arrays = {n: even_light(a) for n, a in arrays.items()}

    if mode == "native":
        a = arrays[names[0]]
        if a[..., 3].min() < 250:
            a = a[slice(*bbox(a[..., 3] > 8)[1::2]), slice(*bbox(a[..., 3] > 8)[0::2])]
        h, w = a.shape[:2]
        want = group["aspect"]
        if w / h > want:
            cw = round(h * want); a = a[:, (w - cw) // 2:(w - cw) // 2 + cw]
        else:
            ch = round(w / want); a = a[(h - ch) // 2:(h - ch) // 2 + ch]
        a[..., 3] = 255
        return {names[0]: to_image(a)}

    if mode == "exact":
        a = arrays[names[0]]
        x0, y0, x1, y1 = bbox(a[..., 3] > 8)
        img = to_image(a[y0:y1, x0:x1])
        return {names[0]: img.resize(group["size"], Image.LANCZOS)}

    # body = opaque part (glows and shadows around it are kept but don't count for alignment)
    thr = 255 * 0.8 if group.get("body") is None else group["body"]
    info = {}
    for n, a in arrays.items():
        full = bbox(a[..., 3] > 8)
        if group.get("align") == "canvas":
            body = bbox(np.any([arrays[m][..., 3] > 8 for m in names], axis=0))
        else:
            body = bbox(a[..., 3] > thr)
        c = ((body[0] + body[2]) / 2, (body[1] + body[3]) / 2)
        if group.get("align") == "nut":   # the widest row (the nut) stays put, the lever moves
            rows = (a[..., 3] > thr).sum(1)
            c = (c[0], float(np.median(np.nonzero(rows >= rows.max() * 0.97)[0])))
        info[n] = dict(full=full, body=body, c=c, bh=body[3] - body[1], bw=body[2] - body[0])
    ref = group.get("ref", names[0])
    norm = {n: (info[ref]["bh"] / info[n]["bh"] if group.get("norm", True) else 1.0) for n in names}
    # extent of everything (glow included) around the body centre, in the reference's units
    ex = max(max(info[n]["c"][0] - info[n]["full"][0], info[n]["full"][2] - info[n]["c"][0]) * norm[n] for n in names)
    ey = max(max(info[n]["c"][1] - info[n]["full"][1], info[n]["full"][3] - info[n]["c"][1]) * norm[n] for n in names)
    if group.get("fitref"):   # size by the body (the glows may be cut at the edge)
        ex = ey = max(info[ref]["bw"], info[ref]["bh"]) / 2 / group["fitref"]

    if mode == "fixed":
        W, H = group["size"]
        s = min((W / 2 - pad) / ex, (H / 2 - pad) / ey)
    elif mode == "height":
        H = group["size"]
        s = (H / 2 - pad) / ey
        W = int(math.ceil(2 * (ex * s + pad)))
    elif mode == "width":
        W = group["size"]
        s = (W / 2 - pad) / ex
        H = int(math.ceil(2 * (ey * s + pad)))
    else:
        raise ValueError(mode)

    return {n: to_image(resample(arrays[n], s * norm[n], info[n]["c"], (W, H), (W / 2, H / 2))) for n in names}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--only", default="")
    ap.add_argument("--sheet", default="")
    args = ap.parse_args()
    only = set(filter(None, args.only.split(",")))
    os.makedirs(OUT, exist_ok=True)
    made = []
    for group in GROUPS:
        if only and not (only & set(group["files"])):
            continue
        missing = [f for f in group["files"].values() if not os.path.exists(os.path.join(args.src, f))]
        if missing:
            print("skip", list(group["files"]), "- missing", missing)
            continue
        for name, img in process(group, args.src).items():
            path = os.path.join(OUT, name + ".png")
            img.save(path, optimize=True)
            made.append((name, img))
            print(f"{name:24s} {img.width}x{img.height}")
    if args.sheet:
        cols, cell = 6, 240
        rows = (len(made) + cols - 1) // cols
        sheet = Image.new("RGBA", (cols * cell, rows * (cell + 16)), (28, 22, 16, 255))
        from PIL import ImageDraw
        d = ImageDraw.Draw(sheet)
        for i, (name, img) in enumerate(made):
            t = img.copy(); t.thumbnail((cell - 10, cell - 10))
            x, y = (i % cols) * cell, (i // cols) * (cell + 16)
            sheet.alpha_composite(t, (x + (cell - t.width) // 2, y + 16 + (cell - t.height) // 2))
            d.text((x + 4, y + 2), f"{name} {img.width}x{img.height}", fill=(255, 220, 0, 255))
        sheet.save(args.sheet)


if __name__ == "__main__":
    sys.exit(main())
