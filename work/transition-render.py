"""Compose the human-facing recordings for transition-compare.ts.

Input: the manifest JSON written by the tool (one entry per sampled progress
point, with one PNG tile per side), and the output directory.

Writes, for each transition:
  <out>/<id>-side-by-side.gif   console | native, labels burned in
  <out>/<id>-contact-sheet.png  every 4th sampled point, both rows
  <out>/<id>.mp4                only when ffmpeg is on PATH

Endpoint transitions (one settled frame per side) get <id>-endpoint.png instead
of a one-frame GIF.
"""
import json
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

HDR = 30          # header bar above each tile row
FOOT = 26         # footer bar with the frame label
GAP = 6


def font(size):
    for p in (r"C:\Windows\Fonts\segoeui.ttf", r"C:\Windows\Fonts\arial.ttf"):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def compose(entry, manifest, f_title, f_label):
    tile = manifest["tile"]
    tw, th = tile
    sides = [k for k in ("console", "native") if k in entry["tiles"]]
    width = len(sides) * tw + (len(sides) - 1) * GAP
    height = HDR + th + FOOT
    canvas = Image.new("RGB", (width, height), (18, 18, 18))
    draw = ImageDraw.Draw(canvas)
    draw.text((8, 6), f'{manifest["id"]}  {manifest["label"]}', font=f_title, fill=(240, 240, 240))
    for i, side in enumerate(sides):
        x = i * (tw + GAP)
        tile_img = Image.open(entry["tiles"][side]).convert("RGB")
        canvas.paste(tile_img, (x, HDR))
        draw.text((x + 8, HDR + th + 4), f'{side.upper()}  {entry["labels"].get(side, "")}', font=f_label, fill=(220, 220, 220))
    return canvas


def main():
    manifest_path, out_dir = sys.argv[1], sys.argv[2]
    manifest = json.load(open(manifest_path, "r", encoding="utf-8"))
    tid = manifest["id"]
    f_title, f_label = font(17), font(14)
    os.makedirs(out_dir, exist_ok=True)
    frames = manifest["frames"]
    written = []

    if manifest.get("mode") == "endpoint" or len(frames) == 1:
        img = compose(frames[0], manifest, f_title, f_label)
        path = os.path.join(out_dir, f"{tid}-endpoint.png")
        img.save(path)
        return print(path)

    composed = []
    for entry in frames:
        img = compose(entry, manifest, f_title, f_label)
        composed.append(img)
        img.save(os.path.join(os.path.dirname(manifest_path), f"sbs-{entry['index']:03d}.png"))

    # GIF: one frame per sampled progress point, 120 ms each
    gif = os.path.join(out_dir, f"{tid}-side-by-side.gif")
    pal = [img.convert("P", palette=Image.ADAPTIVE, colors=128) for img in composed]
    pal[0].save(gif, save_all=True, append_images=pal[1:], duration=120, loop=0, optimize=False)
    written.append(gif)

    # Contact sheet: every 4th sampled point, console row above native row
    step = 4
    picks = list(range(0, len(composed), step))
    if picks[-1] != len(composed) - 1:
        picks.append(len(composed) - 1)
    cw, ch = composed[0].size
    sheet = Image.new("RGB", (cw, (ch + GAP) * len(picks)), (12, 12, 12))
    for r, i in enumerate(picks):
        sheet.paste(composed[i], (0, r * (ch + GAP)))
    sheet_path = os.path.join(out_dir, f"{tid}-contact-sheet.png")
    sheet.save(sheet_path)
    written.append(sheet_path)

    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg:
        mp4 = os.path.join(out_dir, f"{tid}.mp4")
        cmd = [ffmpeg, "-y", "-loglevel", "error", "-framerate", "8", "-i",
               os.path.join(os.path.dirname(manifest_path), "sbs-%03d.png"),
               "-pix_fmt", "yuv420p", mp4]
        if subprocess.run(cmd).returncode == 0:
            written.append(mp4)
    for path in written:
        print(path)


if __name__ == "__main__":
    main()
